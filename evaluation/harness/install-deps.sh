#!/bin/bash
# System packages both benchmark suites need, beyond what
# `phoronix-test-suite install-dependencies` installs for itself.
#
# Debian/Ubuntu. Run once, before install-pts.sh and prepare-lmbench.sh.
#   ./install-deps.sh            install everything
#   ./install-deps.sh --check    report what is missing, change nothing
set -euo pipefail

# --- lmbench ---------------------------------------------------------------
# libtirpc-dev is not optional and not obvious: lmbench 3.0-a9's src/bench.h
# includes <rpc/rpc.h> unconditionally and all 60 source files include
# bench.h, so WITHOUT it the whole suite fails to compile, not just lat_rpc.
# glibc dropped its Sun RPC implementation in 2.32, so any distro newer than
# Debian 11 / Ubuntu 20.04 needs libtirpc instead -- see prepare-lmbench.sh,
# which wires up the include path.
#
# rpcbind supplies both the daemon lat_rpc registers with and the rpcinfo
# used to check it; without it running, both RPC latency numbers vanish
# while `make rerun` still exits 0.
#
# net-tools is a softer requirement, listed because its absence is invisible:
# scripts/lmbench writes the [net:]/[if:] provenance lines in each result file
# by shelling out to `netstat -i` and `ifconfig`, neither of which Debian
# installs by default any more. Without it those header lines come out empty.
# No measurement changes -- but the shipped reference results have them, so a
# reviewer comparing files would see a difference that is not a difference.
LMBENCH_PKGS=(build-essential libtirpc-dev rpcbind net-tools)

# --- Phoronix Test Suite ---------------------------------------------------
# PTS itself is PHP. php-xml is what parses and writes composite.xml.
# The remaining packages are test dependencies that
# `phoronix-test-suite install-dependencies` does not reliably pull on
# Debian 12: pkg-config and the two -dev packages are what the gnupg
# compat shims in ../phoronix/patches/shims/ call out to, and the kernel
# build test needs the usual kernel build chain.
PTS_PKGS=(php-cli php-xml pkg-config unzip wget netcat-openbsd
          libgpg-error-dev libksba-dev
          bc bison flex libssl-dev libelf-dev)

# --- harness ---------------------------------------------------------------
# cpupower pins the clock in bmk-env.sh; python3 runs parse-results.py.
HARNESS_PKGS=(python3 linux-cpupower)

# --- optional --------------------------------------------------------------
# Only rng-probe.sh and rng-sweep.sh need these, and neither contributes to
# the reported numbers -- they answer *why* an overhead appears.
OPTIONAL_PKGS=(linux-perf bpftrace)

ALL=("${LMBENCH_PKGS[@]}" "${PTS_PKGS[@]}" "${HARNESS_PKGS[@]}")

have() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "ok installed"; }

report() {
	local missing=() p
	for p in "$@"; do have "$p" || missing+=("$p"); done
	if [[ ${#missing[@]} -eq 0 ]]; then
		echo "  all present"
	else
		printf '  MISSING: %s\n' "${missing[*]}"
	fi
	MISSING_COUNT=$(( ${MISSING_COUNT:-0} + ${#missing[@]} ))
}

if [[ "${1:-}" == "--check" ]]; then
	MISSING_COUNT=0
	echo "[lmbench]"; report "${LMBENCH_PKGS[@]}"
	echo "[phoronix]"; report "${PTS_PKGS[@]}"
	echo "[harness]";  report "${HARNESS_PKGS[@]}"
	echo "[optional: attribution scripts only]"; report "${OPTIONAL_PKGS[@]}"
	echo
	# Header presence, not just the package: this is the failure that stops
	# lmbench building, and it is worth checking the actual file.
	# Search the sbin dirs too: scripts/lmbench:10 appends /sbin:/usr/sbin to
	# PATH before calling these, so looking only at the invoking user's PATH
	# reports a problem that will not exist at run time.
	for c in netstat ifconfig; do
		PATH="$PATH:/sbin:/usr/sbin" command -v "$c" >/dev/null 2>&1 || \
			echo "$c: not found -- lmbench result headers will lack [net:]/[if:] lines"
	done
	if [[ -e /usr/include/rpc/rpc.h ]]; then
		echo "rpc/rpc.h: present in the default include path"
	elif [[ -e /usr/include/tirpc/rpc/rpc.h ]]; then
		echo "rpc/rpc.h: via libtirpc (prepare-lmbench.sh will add -I/usr/include/tirpc)"
	else
		echo "rpc/rpc.h: NOT FOUND -- lmbench will not compile; install libtirpc-dev"
	fi
	exit $(( MISSING_COUNT > 0 ))
fi

echo "[+] Installing $(( ${#ALL[@]} )) packages"
sudo apt-get update
sudo apt-get install -y "${ALL[@]}"

echo
echo "[+] Optional (attribution scripts only, not needed for the numbers):"
echo "      sudo apt-get install -y ${OPTIONAL_PKGS[*]}"
echo
echo "[+] Next: ./install-pts.sh, then ../lmbench/prepare-lmbench.sh"
