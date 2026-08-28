#!/usr/bin/env bash
#
# Apply the RNGuard and CVE-re-introduction patches to a v6.12.11 source tree.
#
# Usage: apply-patches.sh <kernel-src-dir> [patch ...]
#   With no patch arguments, applies every patch in kernel/patches/ in order.
#
# Examples:
#   # defense only, safe to boot anywhere you would boot a stock kernel
#   apply-patches.sh linux-6.12.11 0001-rnguard-mpk-x86_64.patch
#
#   # everything, including the deliberately re-introduced CVEs (VM only!)
#   apply-patches.sh linux-6.12.11
set -euo pipefail

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
patchdir="${here}/../patches"

if [[ $# -lt 1 ]]; then
    echo "usage: $(basename -- "$0") <kernel-src-dir> [patch ...]" >&2
    exit 2
fi

src="$1"; shift

if [[ ! -f "${src}/Makefile" ]] || ! grep -q "^VERSION = 6" "${src}/Makefile"; then
    echo "[!] ${src} does not look like a Linux source tree." >&2
    echo "    Run scripts/fetch-kernel.sh first." >&2
    exit 1
fi

if [[ $# -gt 0 ]]; then
    patches=("$@")
else
    mapfile -t patches < <(cd "${patchdir}" && ls -1 [0-9][0-9][0-9][0-9]-*.patch 2>/dev/null || true)
fi

if [[ ${#patches[@]} -eq 0 ]]; then
    echo "[!] No patches found in ${patchdir}." >&2
    echo "    See docs/artifact-status.md — patch generation is pending." >&2
    exit 1
fi

for p in "${patches[@]}"; do
    path="${patchdir}/${p}"
    [[ -f "${path}" ]] || { echo "[!] Missing patch: ${path}" >&2; exit 1; }

    # Every patch that weakens the kernel says so in its subject line, so this
    # cannot fall out of sync with the file names the way a glob would.
    if head -20 "${path}" | grep -q 'DO NOT DEPLOY'; then
        echo "[!] ${p} REMOVES a security property. Boot the result only in a VM."
    fi

    echo "[+] Applying ${p}"
    if ! patch -d "${src}" -p1 --forward --silent < "${path}"; then
        echo "[!] ${p} did not apply cleanly." >&2
        exit 1
    fi
done

echo "[=] All patches applied to ${src}"
echo "    Next: scripts/build.sh ${src} <config>"
