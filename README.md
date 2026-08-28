<div align="center">

# DirtyRand

#### Data-only Attacks against the Linux Kernel RNG

</div>

The effectiveness of many system-security mechanisms hinges on the unpredictability of a single component: the random number generator (RNG) of the OS. While decades of work hardened the cryptographic design on the Linux RNG, and ensured the quality of entropy sources, the integrity of its internal generator state in the presence of memory safety vulnerabilities is largely unexamined. In this work, we demonstrate that Linux’s RNG state is an unprotected, high-value target for data-only attacks. We present DirtyRand, the first data-only attack targeting the Linux RNG’s internal state. Assuming a weak, unprivileged local adversary with no kernel-memory disclosure capabilities and only a single, restricted write of attacker-known (not attacker-chosen) data, DirtyRand exploits the RNG’s deterministic, hierarchical structure; a single well-placed corruption collapses the entropy of all downstream randomness consumers across user and kernel space, rendering random draws predictable. We also introduce a follow-on primitive that halts the RNG’s periodic reseeding cycle, making the compromise persist indefinitely. We demonstrate the severity of DirtyRand through end-to-end exploits that bypass kernel and user-space stack canaries, ASLR, and SLUB hardening, and that mount DNS cache poisoning, TCP connection hijacking, and TLS premaster-secret recovery.

To mitigate DirtyRand, we design and implement RNGuard: a protection mechanism that isolates and mediates access to the RNG’s sensitive state using hardware memory-protection primitives (Intel MPK on x86-64 and ARM MTE on AArch64). Unlike the deliberately weak adversary DirtyRand assumes, RNGuard is engineered to withstand a strictly stronger attacker wielding an arbitrary (kernel-memory) read–write primitive. By applying RNGuard against our end-to-end exploits and arbitrary read scenarios, we show that RNGuard neutralizes both corruption-based attacks and direct disclosure. Furthermore, RNGuard imposes minimal overhead on macro benchmarks (&le;1% across the Phoronix Test Suite), demonstrating that the RNG’s internal state can be fully protected without compromising system performance.

Further information about the design and implementation of DirtyRand and RNGuard can be found in our [paper](./docs/dirtyrand.pdf).

```
@inproceedings{dirtyrand,
    title       = {{DirtyRand: Data-only Attacks against the Linux Kernel RNG}},
    author      = {XXX},
    booktitle   = {XXX},
    pages       = {XXX},
    year        = {XXX}
}
```

## Layout

| Path | Paper | Contents |
|---|---|---|
| `kernel/` | §6 | RNGuard patches, kernel configs, build scripts |
| `attack/lib/` | §4.1 | User-space ChaCha20 model that replays the kernel's RNG |
| `attack/primitives/` | §3, §5 | The restricted-write primitive: synthetic (`kmod`) and real (`poc`, CVE-2022-34918) |
| `attack/reseed-halt/` | §4.2 | Workqueue pending-bit corruption that stops reseeding |
| `case-studies/` | §5 | The seven end-to-end exploits of Table 1 |
| `evaluation/` | §7.2 | LMBench (Table 2) and Phoronix (Table 3) harnesses |
| `docs/` | — | Paper-to-code map and per-claim reproduction steps |

## Where to start

1. `REQUIREMENTS.md` — what hardware and software you need. Most of the
   artifact runs in QEMU; only the MPK performance numbers need real hardware.
2. `INSTALL.md` — build the kernels and bring up the VMs.
3. `docs/claims.md` — each claim in the paper, the command that exercises it,
   and what you should see.
4. `docs/paper-to-artifact.md` — reverse index from every section, table and
   figure to the code that produced it.

## Status

Some components are still being assembled. `docs/artifact-status.md` tracks
exactly what is present and what is pending, so nothing here is a surprise.

## Ethics and safe use

Every exploit in this artifact runs against a kernel that has been
**deliberately made vulnerable**: two patches in `kernel/patches/` re-introduce
CVEs that upstream fixed years ago, and one adds a synthetic write primitive
that exists in no real kernel. None of this is a working exploit against a
current Linux system.

Run this artifact only inside the disposable VMs described in `INSTALL.md`. Do
not boot a patched kernel on a machine you care about, and do not point the
network case studies at anything but the local topology the setup script
creates.
