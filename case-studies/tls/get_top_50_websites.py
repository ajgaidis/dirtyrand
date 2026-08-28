#!/usr/bin/env python3
"""
TLS 1.2 cipher support survey across the Tranco top 10K domains.

For each domain, performs two TLS handshakes on port 443 (no HTTP):
  - rsa   : static RSA key exchange  (legacy cipher suites)
  - ecdhe : ephemeral ECDH forward-secret (modern cipher suites)

Async + concurrent + handshake-only. Live progress prints every second so
you can tell at a glance whether the pipeline is healthy.
"""

import asyncio
import csv
import io
import socket
import ssl
import time
import zipfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from urllib.request import Request, urlopen


# --- Configuration ---------------------------------------------------------
TOP_N       = 1_000_000
CONCURRENCY = 300
TIMEOUT     = 4.0
OUTPUT_CSV  = Path("tls_survey_results.csv")

TRANCO_URL   = "https://tranco-list.eu/top-1m.csv.zip"
TRANCO_CACHE = Path("/tmp/tranco_top1m.csv")

# @SECLEVEL=0 lets us *test* servers that still offer weaker ciphers; we are
# not protecting data here, just surveying what each endpoint negotiates.
RSA_CIPHERS   = "AES128-GCM-SHA256:AES256-GCM-SHA384:AES256-SHA:AES128-SHA:@SECLEVEL=0"
ECDHE_CIPHERS = "ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-RSA-AES128-GCM-SHA256:@SECLEVEL=0"
PROFILES = [("rsa", RSA_CIPHERS), ("ecdhe", ECDHE_CIPHERS)]


@dataclass
class ProbeResult:
    domain: str
    profile: str
    ok: bool
    category: str
    cipher: str | None
    elapsed: float


def fetch_tranco(n: int) -> list[str]:
    if not TRANCO_CACHE.exists():
        print(f"[boot] downloading {TRANCO_URL}", flush=True)
        req = Request(TRANCO_URL, headers={"User-Agent": "tls-survey/1.0"})
        with urlopen(req, timeout=60) as resp:
            data = resp.read()
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            TRANCO_CACHE.write_bytes(z.read(z.namelist()[0]))
        print(f"[boot] cached {len(data)/1e6:.1f} MB -> {TRANCO_CACHE}", flush=True)
    else:
        print(f"[boot] using cached Tranco list at {TRANCO_CACHE}", flush=True)

    domains: list[str] = []
    with TRANCO_CACHE.open() as f:
        for row in csv.reader(f):
            if row:
                domains.append(row[1])
                if len(domains) >= n:
                    break
    print(f"[boot] loaded top {len(domains):,} domains", flush=True)
    return domains


def make_ctx(ciphers: str) -> ssl.SSLContext:
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLSv1_2)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ctx.set_ciphers(ciphers)
    return ctx


def classify(exc: BaseException) -> str:
    if isinstance(exc, asyncio.TimeoutError):
        return "timeout"
    if isinstance(exc, ssl.SSLError):
        msg = str(exc).lower()
        if "handshake_failure" in msg:
            return "handshake_failure"
        if any(k in msg for k in ("unsupported_protocol", "wrong_version", "protocol_version", "no_protocols_available")):
            return "tls12_unsupported"
        if "no shared cipher" in msg:
            return "no_shared_cipher"
        if "certificate" in msg:
            return "cert_error"
        return "ssl_other"
    if isinstance(exc, socket.gaierror):
        return "dns"
    if isinstance(exc, ConnectionRefusedError):
        return "refused"
    if isinstance(exc, ConnectionResetError):
        return "reset"
    if isinstance(exc, OSError):
        return "net_error"
    return "other"


async def probe_one(domain: str, profile: str, ctx: ssl.SSLContext, timeout: float) -> ProbeResult:
    start = time.monotonic()
    try:
        reader, writer = await asyncio.wait_for(
            asyncio.open_connection(domain, 443, ssl=ctx, server_hostname=domain),
            timeout=timeout,
        )
        ssl_obj = writer.get_extra_info("ssl_object")
        cipher = ssl_obj.cipher()[0] if ssl_obj else None
        writer.close()
        try:
            await asyncio.wait_for(writer.wait_closed(), timeout=1.0)
        except BaseException:
            pass
        return ProbeResult(domain, profile, True, "ok", cipher, time.monotonic() - start)
    except BaseException as e:
        return ProbeResult(domain, profile, False, classify(e), None, time.monotonic() - start)


async def probe_domain(domain: str, ctxs: dict[str, ssl.SSLContext], sem: asyncio.Semaphore) -> list[ProbeResult]:
    async with sem:
        return [await probe_one(domain, p, ctxs[p], TIMEOUT) for p, _ in PROFILES]


def fmt_eta(seconds: float) -> str:
    if seconds == float("inf"):
        return "   inf"
    if seconds < 60:
        return f"{seconds:5.0f}s"
    if seconds < 3600:
        return f"{seconds/60:5.1f}m"
    return f"{seconds/3600:5.2f}h"


async def run(domains: list[str]) -> None:
    ctxs = {name: make_ctx(c) for name, c in PROFILES}

    # Sanity probe — if this fails the rest of the run is meaningless.
    print("\n[boot] ===== sanity probe (google.com) =====", flush=True)
    for p, _ in PROFILES:
        r = await probe_one("google.com", p, ctxs[p], TIMEOUT)
        status = "OK  " if r.ok else "FAIL"
        print(f"[boot]   {p:5s} {status}  category={r.category:<20s} cipher={r.cipher}", flush=True)
    print("[boot] ======================================\n", flush=True)

    sem = asyncio.Semaphore(CONCURRENCY)
    tasks = [asyncio.create_task(probe_domain(d, ctxs, sem)) for d in domains]

    counters = {p: Counter() for p, _ in PROFILES}
    sample_ok = {p: [] for p, _ in PROFILES}
    sample_fail = {p: [] for p, _ in PROFILES}
    all_rows: list[ProbeResult] = []
    completed = 0
    total = len(domains)
    start = time.monotonic()
    last_print = start

    print(f"[run] {total:,} domains x {len(PROFILES)} profiles | concurrency={CONCURRENCY} timeout={TIMEOUT}s", flush=True)
    for fut in asyncio.as_completed(tasks):
        results = await fut
        completed += 1
        for r in results:
            counters[r.profile][r.category] += 1
            if r.ok and len(sample_ok[r.profile]) < 3:
                sample_ok[r.profile].append(r)
            elif not r.ok and len(sample_fail[r.profile]) < 3:
                sample_fail[r.profile].append(r)
            all_rows.append(r)
        now = time.monotonic()
        if now - last_print >= 1.0 or completed == total:
            elapsed = now - start
            rate = completed / elapsed if elapsed else 0
            eta = (total - completed) / rate if rate else float("inf")
            parts = []
            for p, _ in PROFILES:
                ok = counters[p]["ok"]
                pct = 100 * ok / completed if completed else 0
                parts.append(f"{p}={ok:>5}/{completed} ({pct:4.1f}%)")
            print(f"[prog] {completed:>6}/{total} | {rate:5.1f}/s | eta {fmt_eta(eta)} | " + " | ".join(parts), flush=True)
            last_print = now

    elapsed = time.monotonic() - start
    print(f"\n[done] {total:,} domains in {fmt_eta(elapsed)} ({total/elapsed:.1f}/s)", flush=True)

    print("\n=== Headline: cipher-suite support (out of {0:,} domains) ===".format(total))
    for p, _ in PROFILES:
        ok = counters[p]["ok"]
        print(f"  {p:5s}  {ok:>6d} / {total:<6d}  ({100*ok/total:6.2f}%)")
    rsa_ok = counters["rsa"]["ok"]
    print(f"\n  >>> static-RSA support: {rsa_ok:,} / {total:,} websites  ({100*rsa_ok/total:.2f}%)")

    print("\n=== Per-profile breakdown ===")
    for p, ciphers in PROFILES:
        print(f"\n[{p}]  ciphers: {ciphers}")
        c = counters[p]
        for cat, cnt in sorted(c.items(), key=lambda kv: -kv[1]):
            print(f"  {cat:<22s} {cnt:>6d}  ({100*cnt/total:5.1f}%)")
        if sample_ok[p]:
            print("  --- sample successes ---")
            for r in sample_ok[p]:
                print(f"    + {r.domain:<32s} cipher={r.cipher}")
        if sample_fail[p]:
            print("  --- sample failures ---")
            for r in sample_fail[p]:
                print(f"    - {r.domain:<32s} {r.category}")

    with OUTPUT_CSV.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["domain", "profile", "ok", "category", "cipher", "elapsed_ms"])
        for r in all_rows:
            w.writerow([r.domain, r.profile, int(r.ok), r.category, r.cipher or "", f"{r.elapsed*1000:.1f}"])
    print(f"\n[done] per-domain results -> {OUTPUT_CSV}")


if __name__ == "__main__":
    asyncio.run(run(fetch_tranco(TOP_N)))
