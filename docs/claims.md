# Claims and how to check them

Each claim the paper makes, the artifact component that supports it, and what
you should observe. Claims are ordered cheapest-first: C1 needs nothing but a
compiler, C8–C9 need two kernel builds and hours of benchmarking.

---

## C1 — The user-space library reproduces the kernel's RNG exactly

*Paper: §4.1, "Emulating the RNG".*

**Component:** `attack/lib/`
**Cost:** seconds, no VM

```bash
cd attack/lib && make
./chacha --extractions 4 --size 32          # replay from L2
./chacha --l1 --extractions 4 --size 32     # replay starting at L1
./chacha --user --extractions 2 --size 64   # model the /dev/urandom path
```

**Expect:** a deterministic byte stream for a fixed starting key. With the
default all-zero key the first L2 block begins `76 b8 e0 ad a0 f1 3d 90`, the
published ChaCha20 test vector — a quick check that the model is correct before
any kernel is involved.

This model is the oracle every later claim compares against. The kernel-canary
and SLUB case studies each carry their own linkable copy of it so they build
standalone; `attack/README.md` explains the arrangement.

---

## C2 — A single restricted write makes kernel randomness predictable

*Paper: §4.1, "Validating Prediction"; the baseline test behind Table 1.*

**Component:** `attack/primitives/kmod/`, `attack/lib/`
**Cost:** one VM

Inside the VM, zap `L1.key` and compare the kernel's subsequent output against
the library's prediction for all three API levels: `getrandom()` (user, drawn
from L2), `get_random_bytes()` (kernel, L2) and `get_random_long()` (kernel,
L3). The latter two are exercised from a test module since they are not
reachable from user space.

**Expect:** extracted values match predictions exactly, at every level.

---

## C3 — The corruption can be made to persist indefinitely

*Paper: §4.2, "Halting the Reseed Cycle".*

**Component:** `attack/reseed-halt/`
**Cost:** one VM, several minutes

Without this step, a corrupted L1 is refreshed at the next reseed, bounding the
attack window at 60 s. Setting the `pending` bit of the `crng_reseed` work item
makes the kernel treat the re-queue as a duplicate and silently drop it.

**Expect:** predictions continue to hold well past the 60 s reseed interval.
The timing side channel narrows the overwrite window from 60 s to milliseconds.

---

## C4 — Stack canaries become predictable (user and kernel)

*Paper: §5.1.1. Table 1 rows "Canaries (user)" and "Canaries (kernel)".*

**Component:** `case-studies/canary-user/`, `case-studies/canary-kernel/`
**Cost:** one VM; the user-space study also builds musl 1.1.7 and iputils

User space: zapping L1 before `execve()` fixes `AT_RANDOM`, so the canary and
musl's base address are computable offline; the exploit then drives
CVE-2015-1817 in `inet_pton()` through a setuid `ping` and ROPs to a shell.

Kernel space: a module with a synthetic stack overflow, where the task canary
drawn from L3 is predicted and reproduced in the payload.

**Expect:** a `root` shell in both cases; the canary check passes rather than
tripping the stack protector.

---

## C5 — ASLR collapses to a one-shot exploit

*Paper: §5.1.2. Appendix F, Table 5 gives the offset equations.*

**Component:** `case-studies/aslr/`
**Cost:** one VM

**Expect:** the CVE-2023-4911 "Looney Tunables" exploit, which upstream reports
as a noisy brute-force taking 30 s to 5 min, succeeds deterministically on the
first attempt because the stack base is computed rather than guessed.

---

## C6 — SLUB freelist hardening is bypassed

*Paper: §5.1.3, Appendix E.*

**Component:** `case-studies/slub-freelist/`
**Cost:** one VM, DCCP enabled

Because the DCCP cache is created *after* L1 is zapped, `s->random` is
derivable, so forged freelist pointers decode legally. The chain then
cross-caches a dangling `ccid` onto a live `cred`.

**Expect:** `getuid() == 0` in the spraying child and a root shell. Note the
privilege-granting write is performed by the kernel, not the exploit.

---

## C7 — Network-protocol randomness is predictable

*Paper: §5.2. Table 1 rows DNS, TCP, TLS.*

**Component:** `case-studies/{dns,tcp,tls}/`
**Cost:** three-VM topology (`case-studies/setup.sh`)

- **DNS** (§5.2.1): predict `systemd-resolved`'s TXID and ephemeral source
  port, win the race against the legitimate reply, poison the cache.
- **TCP** (§5.2.2): predict the ephemeral port and ISN from the planted
  `net_secret` and `table_perturb`, inject an RST off-path, tear the
  connection down.
- **TLS** (§5.2.3): recover GnuTLS's premaster secret from a predicted
  `getrandom()` seed plus a guessable second-resolution timestamp, then decrypt
  the session.

**Expect:** a poisoned cache entry, a broken keep-alive connection, and a
recovered PMS respectively.

`case-studies/tls/` also carries the survey data behind the paper's claim that
4,072 of Tranco's top 10K sites still complete static-RSA handshakes.

---

## C8 — RNGuard neutralises all of the above

*Paper: §7.1.*

**Component:** `kernel/patches/0001` plus `0002` (x86-64) or `0003` (AArch64)
**Cost:** one extra kernel build

Re-run C2 through C7 against an RNGuard-hardened kernel. RNGuard is evaluated
against a *stronger* attacker than DirtyRand assumes — arbitrary kernel
read-write, not a single restricted write — so both corruption and direct
disclosure should fail.

**Expect:** the write is refused by hardware. On x86-64 the access faults
against the MPK protection key; on AArch64 it raises a synchronous tag-check
fault that panics the kernel rather than proceeding. Attempts to *read* the
protected state fail the same way.

---

## C9 — RNGuard's overhead is ≤1% on macro benchmarks

*Paper: §7.2, Tables 2 and 3.*

**Component:** `evaluation/`
**Cost:** seconds to check our numbers; two kernel builds plus hours of
benchmarking to reproduce them

Start with the free version — it reads the results we shipped and needs neither
suite installed nor an RNGuard kernel booted:

```bash
evaluation/harness/parse-results.py --reference
```

To reproduce rather than inspect, build both kernels from
`kernel/configs/x86_64-{vanilla,rnguard}.config` and run one sweep per boot;
`evaluation/README.md` has the procedure.

**Expect:** Across Phoronix's 13 families, per-family mean overhead is ≤1% —
our sweep reads +0.05% mean over 120 comparable metrics, with no family above
+0.68%. On lmbench the cost concentrates exactly where the paper says it does:
`select()` (≤10.1%), context switching (+4.3% mean), RPC latency (1.7–7.1%) and
`pipe` (3.3%), against +0.07% for plain syscall latency. Those outliers perform
few or no random draws; they stem from RNGuard splitting large pages into 4 KB
pages, not from the gateways.

Two things to know before comparing counts. The paper's Table 2 reports 36
lmbench *tests*; `parse-results.py` reports 95 *metrics*, because it expands
the context-switch matrix, the `lat_fs` sizes and the socket-bandwidth curve
into one row each. And on our thermally-limited testbed 57 of those 95 metrics
(and 85 of the 120 PTS metrics) have an overhead smaller than their own sample
scatter, flagged `~`. Trust the ranking over the magnitudes;
`evaluation/README.md` says why at length.
