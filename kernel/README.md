# Kernel

RNGuard and the deliberately vulnerable kernel the attacks target. Everything
is expressed as patches against Linux v6.12.11 rather than as a vendored tree,
so the changes stay small and reviewable.

## Patches

Applied in order by `scripts/apply-patches.sh`. Each is independent except
where noted.

| Patch | Paper | Purpose |
|---|---|---|
| `0001-rnguard-core.patch` | §6.1, §6.2 | Architecture-independent half: the Kconfig options, the `.rnguard_iso` placement macros, and the gate and stack-switch call sites in `drivers/char/random.c`. Inert without a backend. |
| `0002-rnguard-mpk-x86_64.patch` | §6.1.1, §6.2 | x86-64 backend: isolated pages are user-accessible with protection key 1, so a stray kernel access is refused by SMAP and PKRU both. Adds the guarded per-CPU pseudo-stack and teaches the unwinder about it. |
| `0003-rnguard-mte-aarch64.patch` | §6.1.2, §6.2 | AArch64 backend: MTE allocation tags plus a tagged pseudo-stack, and the four config corrections without which nothing is actually checked. |
| `0004-cve-2022-34918-reintroduce.patch` | §5 | Restores the netfilter `nf_tables` heap overflow used as the paper's *real* restricted-write primitive. |
| `0005-cve-2017-8824-reintroduce.patch` | §5.1.3 | Restores the DCCP use-after-free behind the SLUB freelist case study, adds two synthetic `setsockopt` write options for driving it, and the `mm/slub.c` line that discloses each cache's `s->random`. |
| `0006-research-kallsyms-export.patch` | §3 | Re-exports `kallsyms_lookup_name()` so the synthetic primitive module can resolve RNG symbols by name. |

Patches 0001–0003 are the defense and are safe to boot. 0004–0006
deliberately **remove** security properties and carry `DO NOT DEPLOY` in
their subject lines; apply them only to a disposable VM kernel.

## Configs

`configs/` holds the kernel configurations used in the paper:

- `x86_64-vanilla.config` — unmodified v6.12.11, the performance baseline (§7.2)
- `x86_64-rnguard.config` — RNGuard enabled via MPK
- `x86_64-attack.config` — vanilla plus the re-introduced CVEs, for §5 *(pending)*
- `aarch64-rnguard.config` — RNGuard enabled via MTE, for QEMU *(pending)*

## Prebuilt images

Building a kernel takes considerably longer than running any single case study.
Prebuilt `bzImage`/`Image` and a matching rootfs are published separately, so
reviewers can reproduce the attacks without a full build. See
`docs/artifact-status.md` for the current location.

## Scripts

```
scripts/fetch-kernel.sh     # download and checksum linux-6.12.11.tar.xz
scripts/apply-patches.sh    # apply the patches above, in order
scripts/build.sh            # configure and build for x86-64 or aarch64
```
