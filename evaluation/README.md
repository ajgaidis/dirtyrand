# Evaluation

The performance evaluation of RNGuard from §7.2 of the paper: both benchmark
suites, the harness that drives them, and the out-of-tree fixes both suites
need.


| Suite | Paper | Directory | What it shows |
|---|---|---|---|
| lmbench 3.0-a9 | Table 2 | `lmbench/` | Per-operation latency across the OS microbenchmarks; isolates the cost of RNGuard's gateways on the paths that draw randomness. |
| Phoronix Test Suite 10.8.4 | Table 3 | `phoronix/` | 13 families of realistic workloads; shows macro-level overhead stays at or below 1%. |

```
harness/     the drivers -- one entry point, both suites
lmbench/     lmbench source, the lat_pagefault fix, and its prep script
phoronix/    PTS settings and the four out-of-tree patches PTS needs
results/     benchmark results and summaries
```

## The measurement is a paired, two-boot comparison

The same suite is run on an `x86_64-vanilla` kernel and an `x86_64-rnguard`
kernel, and the overhead is the per-metric ratio between them. Build both from
`../kernel/configs/`; they differ by `CONFIG_RNGUARD_MPK` and their
`CONFIG_LOCALVERSION` and nothing else.

## Running a sweep

```bash
# --- boot 1: vanilla kernel, single-user mode ---
sudo harness/bmk-env.sh                 # quiet the box; verified under load
cd harness && ./run-bmks.sh --new-sweep

# --- reboot into the RNGuard kernel, single-user mode ---
sudo harness/bmk-env.sh
cd harness && ./run-bmks.sh             # reuses .sweep-tag; merges into one file

# --- report ---
harness/parse-results.py
```

Before either suite: `harness/install-deps.sh` installs the system packages
they need (`--check` reports without installing). One of them is a hard
blocker rather than a convenience — lmbench does not compile at all without
`libtirpc-dev` on any distro newer than Debian 11.

Setup for each suite is in `lmbench/README.md` and `phoronix/README.md`. Both
have failure modes that are silent — a test that records nothing while the
runner still exits 0 — so read them before the first sweep rather than after.

Cost: several hours per half.

## Methodology

The noise controls from §7.2, and where each one actually lives:

| Control | Where |
|---|---|
| Single-user mode | how the sweep is booted (`rpcbind` needs starting by hand — see `lmbench/README.md`) |
| Turbo disabled, frequency pinned, verified under load | `harness/bmk-env.sh` (`SUSTAIN_FREQ`, 2.0 GHz on our box) |
| C-states restricted to C0 | `processor.max_cstate=0 intel_idle.max_cstate=0`, built into `CONFIG_CMDLINE` in both kernel configs |
| PTI forced on for both kernels | `pti=on`, likewise in `CONFIG_CMDLINE` |
| KASLR off | `CONFIG_RANDOMIZE_BASE` unset in both configs |
| Per-test thermal cooldown below 55 °C | `harness/run-pts.sh` |
