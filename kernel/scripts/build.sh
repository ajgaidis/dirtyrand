#!/usr/bin/env bash
#
# Configure and build a patched kernel.
#
# Usage: build.sh <kernel-src-dir> <config-name> [make args...]
#   config-name is a file in kernel/configs/ without the .config suffix,
#   e.g. x86_64-rnguard, x86_64-vanilla, aarch64-rnguard.
#
# Output lands in <kernel-src-dir>/../build-<config-name>/ so several
# configurations can coexist.
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cfgdir="${here}/../configs"

if [[ $# -lt 2 ]]; then
    echo "usage: $(basename -- "$0") <kernel-src-dir> <config-name> [make args...]" >&2
    echo "available configs:" >&2
    (cd "${cfgdir}" 2>/dev/null && ls -1 *.config 2>/dev/null | sed 's/\.config$/  /;s/^/  /') >&2 || \
        echo "  (none yet — see docs/artifact-status.md)" >&2
    exit 2
fi

src="$(cd -- "$1" && pwd)"; shift
cfgname="$1"; shift
cfg="${cfgdir}/${cfgname}.config"

[[ -f "${cfg}" ]] || { echo "[!] No such config: ${cfg}" >&2; exit 1; }

out="$(dirname -- "${src}")/build-${cfgname}"
mkdir -p "${out}"
cp "${cfg}" "${out}/.config"

case "${cfgname}" in
    aarch64-*) export ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" ;;
    x86_64-*)  export ARCH=x86_64 ;;
    *)         echo "[!] Cannot infer ARCH from '${cfgname}'." >&2; exit 1 ;;
esac

echo "[+] ARCH=${ARCH} building ${cfgname} into ${out}"
make -C "${src}" O="${out}" olddefconfig
make -C "${src}" O="${out}" -j"$(nproc)" "$@"

echo "[=] Build complete: ${out}"
case "${ARCH}" in
    x86_64) echo "    image:   ${out}/arch/x86/boot/bzImage" ;;
    arm64)  echo "    image:   ${out}/arch/arm64/boot/Image" ;;
esac
echo "    vmlinux: ${out}/vmlinux   (several case studies read offsets from this)"
