# Artifact status

What is present, what is pending, and why. Kept honest so reviewers never hit
an unexplained gap.

## Present

| Component | Paper | Path |
|---|---|---|
| User-space RNG model (L1/L2/L3 prediction) | §4.1 | `attack/lib/`, plus a linkable copy in two case studies (see `attack/README.md`) |
| Synthetic restricted-write primitive | §3 | `attack/primitives/kmod/` |
| Real write primitive (CVE-2022-34918) | §5 | `attack/primitives/poc/` |
| Reseed-halt via workqueue pending bit | §4.2 | `attack/reseed-halt/` |
| User-space stack canary bypass | §5.1.1 | `case-studies/canary-user/` |
| Kernel stack canary bypass | §5.1.1 | `case-studies/canary-kernel/` |
| ASLR bypass (CVE-2023-4911) | §5.1.2 | `case-studies/aslr/` |
| SLUB freelist bypass (CVE-2017-8824) | §5.1.3 | `case-studies/slub-freelist/` |
| DNS cache poisoning | §5.2.1 | `case-studies/dns/` |
| TCP RST hijack | §5.2.2 | `case-studies/tcp/` |
| TLS premaster-secret recovery | §5.2.3 | `case-studies/tls/` |
| Static-RSA prevalence survey data | §5.2.3 | `case-studies/tls/*.csv`, `*.txt` |
| lmbench harness and fixes | §7.2, Table 2 | `evaluation/lmbench/`, `evaluation/harness/` |
| Phoronix harness and fixes | §7.2, Table 3 | `evaluation/phoronix/`, `evaluation/harness/` |
| Benchmark kernel configs | §7.2 | `kernel/configs/x86_64-{vanilla,rnguard}.config` |
| RNGuard, architecture-independent core | §6.1, §6.2 | `kernel/patches/0001-rnguard-core.patch` |
| RNGuard x86-64 backend (Intel MPK) | §6.1.1 | `kernel/patches/0002-rnguard-mpk-x86_64.patch` |
| RNGuard AArch64 backend (ARM MTE) | §6.1.2 | `kernel/patches/0003-rnguard-mte-aarch64.patch` |
| CVE re-introductions and research hooks | §5 | `kernel/patches/0004`–`0006` |

## Pending

| Item | Path | Note |
|---|---|---|
| Attack and AArch64 configs | `kernel/configs/` | `x86_64-attack.config` and `aarch64-rnguard.config`. The two x86-64 benchmark configs are present. |
| Style pass over the patches | `kernel/patches/` | The series applies and reproduces the tree exactly, but `checkpatch.pl` reports whitespace and indentation errors in 0002 and 0005, and `asm/mte-rnguard.h` has no SPDX tag. See "Disclosures". |
| Prebuilt kernel images | off-repo | Hosted separately; see `kernel/README.md` |
