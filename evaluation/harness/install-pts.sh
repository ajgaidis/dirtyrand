#!/bin/bash
# Install Phoronix Test Suite 10.8.4 from the copy bundled in this artifact.
#
# The version is pinned deliberately: PTS resolves an unversioned test name to
# the newest profile on disk, so a different PTS (or a refreshed profile cache)
# can silently change WHICH test ran. run-pts.sh pins profile versions for the
# same reason. Installing whatever `apt` offers would defeat both.
#
# Offline by default -- the .deb ships next to this script. If it has been
# stripped from the tree, we fall back to fetching the same release and verify
# it against the same checksum, so both paths install identical bytes.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PTS_VERSION="10.8.4"
PTS_DEB="${PTS_DEB:-${HERE}/phoronix-test-suite_${PTS_VERSION}_all.deb}"
PTS_SHA256="be81f71fc0382a7725dc88f4a18f013d1c3f6939d440629231d392a11816feca"
PTS_URL="https://github.com/phoronix-test-suite/phoronix-test-suite/releases/download/v${PTS_VERSION}/phoronix-test-suite_${PTS_VERSION}_all.deb"

verify() {
	local f="$1" got
	got="$(sha256sum "$f" | cut -d' ' -f1)"
	if [[ "${got}" != "${PTS_SHA256}" ]]; then
		echo "[!] checksum mismatch for ${f}" >&2
		echo "[!]   expected ${PTS_SHA256}" >&2
		echo "[!]   got      ${got}" >&2
		exit 1
	fi
	echo "[+] checksum OK: $(basename "$f")"
}

if [[ -f "${PTS_DEB}" ]]; then
	echo "[+] Using bundled package: ${PTS_DEB}"
else
	echo "[!] ${PTS_DEB} not found -- falling back to download." >&2
	TMP="$(mktemp -d)"
	trap 'rm -rf "${TMP}"' EXIT
	PTS_DEB="${TMP}/phoronix-test-suite_${PTS_VERSION}_all.deb"
	wget -O "${PTS_DEB}" "${PTS_URL}"
fi

verify "${PTS_DEB}"

sudo dpkg -i "${PTS_DEB}" || true   # exits non-zero on missing deps; -f fixes
sudo apt-get install -f -y

echo "[+] $(phoronix-test-suite version 2>/dev/null | head -1)"
echo "[+] Next: ./install-pts-tests.sh, then ../phoronix/apply-patches.sh"
