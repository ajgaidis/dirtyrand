#!/usr/bin/env bash
#
# Download and verify the upstream Linux v6.12.11 source tree.
#
# Usage: fetch-kernel.sh [destination-dir]
#   destination-dir defaults to kernel/linux-6.12.11 next to this script.
set -euo pipefail

KVER="6.12.11"
TARBALL="linux-${KVER}.tar.xz"
URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/${TARBALL}"

# sha256sum of linux-6.12.11.tar.xz, computed from the tarball this work was
# developed against. Cross-check against cdn.kernel.org's sha256sums.asc.
SHA256="475172fdbd87a153f123a57952672e773bdb6daf5b58a417d1a5e419fcfeec49"

here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
dest="${1:-${here}/../linux-${KVER}}"

if [[ -d "${dest}" ]]; then
    echo "[=] ${dest} already exists; nothing to do."
    exit 0
fi

mkdir -p "$(dirname -- "${dest}")"
cd "$(dirname -- "${dest}")"

if [[ ! -f "${TARBALL}" ]]; then
    echo "[+] Downloading ${URL}"
    curl -fL --progress-bar -o "${TARBALL}" "${URL}"
fi

echo "[+] Verifying checksum"
if ! echo "${SHA256}  ${TARBALL}" | sha256sum --check --status; then
    echo "[!] Checksum mismatch for ${TARBALL}." >&2
    echo "    Expected: ${SHA256}" >&2
    echo "    Got:      $(sha256sum "${TARBALL}" | cut -d' ' -f1)" >&2
    echo "    Delete the file and re-run, or verify against kernel.org." >&2
    exit 1
fi

echo "[+] Extracting to ${dest}"
tar -xf "${TARBALL}"
mv "linux-${KVER}" "$(basename -- "${dest}")" 2>/dev/null || true

echo "[=] Kernel source ready at ${dest}"
echo "    Next: scripts/apply-patches.sh ${dest}"
