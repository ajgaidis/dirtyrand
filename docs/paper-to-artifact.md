# Paper → artifact map

Reverse index from the paper to the code.

## Attack

| Paper | Artifact |
|---|---|
| §4.1 Emulating the RNG — user-space library that replays the kernel's ChaCha20 hierarchy | `attack/lib/` (CLI), and a linkable copy under each of `case-studies/canary-kernel/lib/chacha/` and `case-studies/slub-freelist/lib/chacha/` — see `attack/README.md` |
| §4.1 Corrupting the L1 DRNG — the 33-byte write (32-byte `key` + `generation`) | `attack/primitives/kmod/` (synthetic), `attack/primitives/poc/` (CVE-2022-34918) |
| §4.1 Validating prediction — zap `L1.key`, then predict `getrandom()`, `get_random_bytes()`, `get_random_long()` | the RNG model above, driven from `case-studies/*/` |
| §4.2 Halting the reseed cycle — workqueue pending-bit overwrite | `attack/reseed-halt/` |
| §4.2 Timing side channel that locates the reseed window | `attack/reseed-halt/` |

## Case studies (Table 1)

| Table 1 row | Level | Paper | Artifact |
|---|---|---|---|
| Canaries (user) | L2 | §5.1.1 | `case-studies/canary-user/` |
| Canaries (kernel) | L3 | §5.1.1 | `case-studies/canary-kernel/` |
| ASLR | L3 | §5.1.2 | `case-studies/aslr/` |
| SLUB freelist | L3 | §5.1.3, App. E | `case-studies/slub-freelist/` |
| DNS | L2, L3 | §5.2.1 | `case-studies/dns/` |
| TCP | L2, L3 | §5.2.2 | `case-studies/tcp/` |
| TLS | L2 | §5.2.3 | `case-studies/tls/` |

Supporting CVEs: CVE-2015-1817 (musl `inet_pton`, user canary),
CVE-2023-4911 (glibc "Looney Tunables", ASLR), CVE-2017-8824 (DCCP UAF, SLUB),
CVE-2022-34918 (netfilter, the write primitive itself).

## Defense

| Paper | Artifact |
|---|---|
| §6.1 Design — isolate persistent (L1/L2/L3) and transient state | `kernel/patches/0001`, `0002` |
| §6.1.1 MPK variant — `pkey`-tagged pages, gateway functions, interrupts held off across the window | `kernel/patches/0001-rnguard-mpk-x86_64.patch` |
| §6.1.2 MTE variant — tagged granules, tagged `sp`, unmapped guard page | `kernel/patches/0002-rnguard-mte-aarch64.patch` |
| §6.1 Protecting transient state — per-CPU pseudo-stack, entry gateway switches the stack pointer | Both patches |

## Evaluation

| Paper | Artifact |
|---|---|
| §7.1 Security analysis — RNGuard against the case studies | `docs/claims.md` |
| §7.2 Table 2 — LMBench v3.0-a9, 36 tests | `evaluation/lmbench/`, `evaluation/harness/`, raw output in `evaluation/results/lmbench/` |
| §7.2 Table 3 — Phoronix Test Suite v10.8.4, 13 families | `evaluation/phoronix/`, `evaluation/harness/`, raw output in `evaluation/results/pts/` |
| §7.2 measurement conditions — PTI, C-states, KASLR, clock pin | `kernel/configs/README.md`, `evaluation/harness/bmk-env.sh` |
