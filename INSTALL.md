# Install

Three stages: build a kernel, build a rootfs, then build the exploits. Skip to
stage 3 if you use the prebuilt images (`kernel/README.md`).

Throughout, `$ART` is the root of this repository.

## 1. Kernel

```bash
cd "$ART/kernel"
./scripts/fetch-kernel.sh                 # downloads + verifies v6.12.11
./scripts/apply-patches.sh linux-6.12.11  # see below for choosing patches
./scripts/build.sh linux-6.12.11 x86_64-attack
```

Which patches you want depends on what you are reproducing:

| Goal | Patches | Config |
|---|---|---|
| Reproduce the attacks (§4, §5) | `0003`, `0004` | `x86_64-attack` |
| Reproduce RNGuard's protection (§7.1) | `0001` plus the above | `x86_64-rnguard` |
| Reproduce RNGuard's overhead (§7.2) | `0001` only, and a second unpatched build | `x86_64-rnguard`, `x86_64-vanilla` |
| Reproduce the MTE variant (§6.1.2) | `0002` | `aarch64-rnguard` |

Pass patch names explicitly to apply a subset:

```bash
./scripts/apply-patches.sh linux-6.12.11 0001-rnguard-mpk-x86_64.patch
```

> Patches `0003` and `0004` deliberately remove upstream security fixes. Boot
> the resulting kernel only in a throwaway VM.

## 2. Rootfs and VMs

Most case studies need a single VM. The DNS, TCP and TLS studies need three on
a routed topology (client, server, off-path attacker), which
`case-studies/setup.sh` provisions:

```bash
cd "$ART/case-studies"
./setup.sh network     # bridges, TAPs, IP forwarding
./setup.sh rootfs      # per-VM Debian images
./setup.sh vm-client   # and vm-server, vm-attacker
```

Single-VM studies boot the image directly under QEMU with `nokaslr`; each
case-study README gives the exact invocation.

## 3. Exploits

Every exploit is parameterised over a *write primitive*, so each builds twice:

- **`-kmod`** links `attack/primitives/kmod/` and talks to `/dev/modkrng`, a
  synthetic module that performs exactly the restricted write of §3. Use this
  to study a case study without depending on the 1-day.
- **`-poc`** links `attack/primitives/poc/`, which wraps the real
  CVE-2022-34918 exploit. Use this for end-to-end demonstrations.

```bash
cd "$ART/attack/primitives/kmod" && make
cd "$ART/attack/primitives/poc"  && make        # needs vmlinux; see below
cd "$ART/case-studies/canary-user/real-world" && make
```

The `poc` primitive derives kernel offsets from `vmlinux` at build time.
It defaults to the build tree produced in stage 1; override if yours is
elsewhere:

```bash
make VMLINUX=/path/to/vmlinux
```

## 4. Check it works

```bash
cd "$ART/attack/lib" && make && ./chacha --extractions 4 --size 32
```

This replays the kernel's ChaCha20 construction in user space from a known
key. It needs no kernel, no VM and no privileges, and is the quickest
confirmation that the toolchain is set up. `docs/claims.md` walks from here to
the full end-to-end claims.
