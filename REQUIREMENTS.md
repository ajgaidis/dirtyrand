# Requirements

## Hardware

| What | Needed for | Notes |
|---|---|---|
| x86-64 host with VT-x | Everything in §4, §5 | Case studies run in QEMU/KVM guests |
| Intel CPU with MPK (Skylake-SP or newer) | RNGuard MPK **performance** numbers (Table 2, Table 3) | Correctness can be checked in QEMU; the paper's timings were taken on bare metal (Core i7-11800H) |
| ≥16 GB RAM | Network case studies | Three concurrent VMs |
| ≈60 GB free disk | Kernel builds + rootfs images | Per kernel configuration |

AArch64 hardware is **not** required. The paper's MTE variant is evaluated
under QEMU with `-machine virt,mte=on -cpu max`, which emulates MTE in
software; no MTE-capable silicon is needed.

## Software

- A recent Linux distribution (Debian 12 / Ubuntu 22.04 or newer)
- `build-essential`, `bc`, `bison`, `flex`, `libssl-dev`, `libelf-dev`
- `qemu-system-x86_64` and `qemu-system-aarch64` (≥ 7.2 for MTE support)
- `gcc-aarch64-linux-gnu` for the MTE variant
- `debootstrap` for building rootfs images
- `gdb` / `gdb-multiarch` for the debugging walkthroughs

## Software for the performance evaluation

`evaluation/` has its own dependencies on top of the above. Install them with
`evaluation/harness/install-deps.sh`, or check without installing:

```bash
evaluation/harness/install-deps.sh --check
```

| Package | Needed for |
|---|---|
| `libtirpc-dev` | **lmbench will not compile without it.** |
| `rpcbind` | lmbench's `lat_rpc` registers with the portmapper. It must be *running*, not merely installed.  |
| `net-tools` | lmbench records the interface table in each result file with `netstat -i` and `ifconfig`. Not a measurement dependency, just QoL. |
| `php-cli`, `php-xml` | the Phoronix Test Suite itself; `php-xml` parses and writes `composite.xml` |
| `pkg-config`, `libgpg-error-dev`, `libksba-dev` | `pts/gnupg` on Debian 12 — see `evaluation/phoronix/patches/shims/` |
| `netcat-openbsd`, `unzip`, `wget` | PTS test fetch and setup |
| `bc`, `bison`, `flex`, `libssl-dev`, `libelf-dev` | the `build-linux-kernel` PTS test (same chain as the kernel build above) |
| `linux-cpupower` | `bmk-env.sh` pins the clock with `cpupower` |
| `python3` | `parse-results.py` |

Per-test dependencies beyond these are installed by
`phoronix-test-suite install-dependencies`, which `install-pts-tests.sh` runs
for every test in the sweep.

## Guest configuration

The paper's measurements used Debian 12 ("Bookworm") guests on Linux v6.12.11
with 16 GB RAM, 4 vCPUs and an `e1000` NIC.

**KASLR is disabled** (`nokaslr`) for all case studies. This is a deliberate
scoping choice stated in §3 of the paper, not a limitation of the attack.

## Time budget

| Task | Approximate cost |
|---|---|
| Kernel build, one configuration | 20–45 min |
| Rootfs build | 10 min |
| Network topology (three VMs) | 30 min |
| Any single case study, once built | seconds to a few minutes |
| Full LMBench run | ~1 hour |
| Full Phoronix run | several hours |

Prebuilt kernel images and a rootfs are published separately so the case
studies can be exercised without the build cost; see `kernel/README.md`.
