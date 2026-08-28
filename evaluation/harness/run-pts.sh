#!/bin/bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TAG_FILE="${HERE}/.sweep-tag"
LOG=/tmp/pts-result-log.txt
DATE_FMT="%F ~ %T"

ITERS=1

# --- result bucketing ----------------------------------------------------
# PTS normally auto-names each result directory after the date AND auto-picks
# an identifier, which on this box resolves to a hardware string ("Intel Wi-Fi
# 6 AX201") -- identical for both kernels, and scattered over ~11 dated dirs
# per sweep. Two env vars fix both problems (batch mode honours them without
# prompting; see pts_test_run_manager.php:405 and :535):
#
#   TEST_RESULTS_NAME        the result DIRECTORY  -- one per sweep ("tag")
#   TEST_RESULTS_IDENTIFIER  the COLUMN inside it  -- vanilla / rnguard
#
# Reusing a tag loads the existing composite.xml and appends, so the vanilla
# and rnguard halves land in ONE file as two identifiers, ready for
# `phoronix-test-suite show-result <tag>` and parse-results.py.
#
#     test-results/rnguard-20260822/composite.xml
#       <Result> nettle/sha256
#         <Entry> vanilla 1234
#         <Entry> rnguard   1180
#
# The two halves are separated by a REBOOT, so the tag must survive it: it is
# persisted in .sweep-tag rather than derived from date(1) at start-up, which
# would give the two boots different tags and silently split the sweep.
# The identifier is derived from `uname -r` so that the half run after the
# reboot cannot be mislabeled by a forgotten argument.
#
# PTS lowercases tags and strips everything but [a-z0-9-] (clean_save_name),
# so keep tags in that alphabet or they will not round-trip.

detect_variant() {
	# The legacy *krng-* spellings are the pre-rename internal names; kept so a
	# kernel built before the RNGuard rename still labels its half correctly.
	case "$(uname -r)" in
		*rnguard-vanilla*|*krng-vanilla*) echo vanilla ;;
		*rnguard-mpk*|*krng-pkeys*)       echo rnguard ;;
		*) return 1 ;;
	esac
}

usage() {
	cat <<EOF
usage: ${0##*/} [--variant vanilla|rnguard] [--tag TAG] [--new-tag] [--iters N]
       ${0##*/} --check

  --variant V   override kernel auto-detection (normally unnecessary)
  --tag TAG     use TAG as the sweep name and persist it   [a-z0-9-]
  --new-tag     start a fresh sweep tag (do NOT merge into the current one)
  --iters N     iterations per test (default ${ITERS})
  --check       run ONLY the installed-tests preflight and exit; runs in a
                second, touches nothing, and needs no RNGuard kernel booted

Typical two-kernel sweep:
  \$ ./${0##*/} --new-tag      # boot 1: vanilla kernel, mints + saves the tag
  ... reboot into the rnguard kernel ...
  \$ ./${0##*/}                # boot 2: reuses .sweep-tag, merges into it
  \$ ./parse-results.py
EOF
}

VARIANT_ARG="" TAG_ARG="" NEW_TAG=0 CHECK_ONLY=0
while [[ $# -gt 0 ]]; do
	case "$1" in
		--variant) VARIANT_ARG="${2:?--variant needs a value}"; shift 2 ;;
		--tag)     TAG_ARG="${2:?--tag needs a value}";         shift 2 ;;
		--new-tag) NEW_TAG=1;                                   shift   ;;
		--check)   CHECK_ONLY=1;                                shift   ;;
		--iters)   ITERS="${2:?--iters needs a value}";          shift 2 ;;
		-h|--help) usage; exit 0 ;;
		# Bare word = variant, for symmetry with run-lmbench.sh <variant>.
		-*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
		*)  VARIANT_ARG="$1"; shift ;;
	esac
done

# --- resolve the variant (identifier) -----------------------------------
DETECTED="$(detect_variant || true)"
VARIANT="${VARIANT_ARG:-${DETECTED}}"

# --check inspects the test list, not the kernel: give it a placeholder label
# so it works from the distro kernel, and keep it side-effect free.
if [[ ${CHECK_ONLY} -eq 1 ]]; then
	VARIANT="${VARIANT:-check}"
	LOG=/dev/null
fi

if [[ -z "${VARIANT}" ]]; then
	cat >&2 <<EOF
[!] Cannot tell which kernel this is: uname -r = $(uname -r)
    Expected a kernel matching *rnguard-vanilla* or *rnguard-mpk*.
    You are probably booted into the distro kernel. Reboot into an RNGuard
    kernel, or force the label with:  ${0##*/} --variant vanilla|rnguard
EOF
	exit 1
fi

if [[ -n "${VARIANT_ARG}" && -n "${DETECTED}" && "${VARIANT_ARG}" != "${DETECTED}" ]]; then
	echo "[!] --variant ${VARIANT_ARG} contradicts the booted kernel ($(uname -r) => ${DETECTED})." >&2
	echo "[!] Labeling results '${VARIANT_ARG}' anyway -- Ctrl-C now if that is wrong." >&2
	sleep 5
fi


PERSIST_TAG=0
# --- resolve the sweep tag ----------------------------------------------
if [[ -n "${TAG_ARG}" ]]; then
	TAG="${TAG_ARG}"
	PERSIST_TAG=1
elif [[ -n "${PTS_SWEEP_TAG:-}" ]]; then
	TAG="${PTS_SWEEP_TAG}"
elif [[ ${NEW_TAG} -eq 0 && -s "${TAG_FILE}" ]]; then
	TAG="$(< "${TAG_FILE}")"
else
	TAG="rnguard-$(date +%Y%m%d-%H%M)"
	PERSIST_TAG=1
fi

# Mirror clean_save_name() so what we print is what PTS actually writes.
TAG="$(tr '[:upper:]' '[:lower:]' <<< "${TAG}" | tr -c 'a-z0-9-' '-' | tr -s '-')"
TAG="${TAG#-}"; TAG="${TAG%-}"

# Only now that BOTH halves are known good is the tag committed --
# otherwise a refused run would leave a stale .sweep-tag behind that
# the next real sweep would silently adopt.
if [[ ${PERSIST_TAG} -eq 1 && ${CHECK_ONLY} -eq 0 ]]; then
	echo "${TAG}" > "${TAG_FILE}"
fi

export TEST_RESULTS_NAME="${TAG}"
export TEST_RESULTS_IDENTIFIER="${VARIANT}"
export TEST_RESULTS_DESCRIPTION="$(uname -r)"

RESULT_DIR="${HOME}/.phoronix-test-suite/test-results/${TAG}"

# Warn if this identifier already has data in the target file -- re-running
# the same kernel under one tag merges into the same column rather than
# replacing it.
if [[ -f "${RESULT_DIR}/composite.xml" ]]; then
	EXISTING="$(grep -oP '(?<=<Identifier>)[^<]+' "${RESULT_DIR}/composite.xml" | sort -u | tr '\n' ' ')"
	echo "[i] Merging into existing sweep '${TAG}' (identifiers present: ${EXISTING})"
	if [[ " ${EXISTING} " == *" ${VARIANT} "* ]]; then
		echo "[!] '${VARIANT}' already has data in this sweep; new values will be ADDED to it." >&2
	fi
fi

# The log is appended to with `tee -a` and shipped to ~/pts-result-log-<tag>-
# <variant>.txt at the end, so a leftover from an aborted/earlier run would be
# folded into this sweep's log. Start each half from an empty file.
if [[ ${CHECK_ONLY} -eq 0 ]]; then
	: > "${LOG}"
fi

if [[ ${CHECK_ONLY} -eq 0 ]]; then
	echo "[+][$(date +"${DATE_FMT}")] sweep tag : ${TAG}"        | tee -a "${LOG}"
	echo "[+][$(date +"${DATE_FMT}")] identifier: ${VARIANT}"     | tee -a "${LOG}"
	echo "[+][$(date +"${DATE_FMT}")] kernel    : $(uname -r)"    | tee -a "${LOG}"
	echo "[+][$(date +"${DATE_FMT}")] result dir: ${RESULT_DIR}"  | tee -a "${LOG}"
fi

# --- thermal / throttle guards -------------------------------------------
# A test must start from a known-cool package, and we flag any run during
# which the hardware logged a thermal-throttle event (its result is then
# untrustworthy and should be discarded). Counters reset each boot, so the
# per-test before/after delta is what matters.
COOL_C=55        # don't start a test until pkg temp drops below this (C)
COOL_POLL=10     # seconds between cooldown polls
COOL_MAX=300     # stop waiting to cool after this many seconds

# x86_pkg_temp is the package sensor; fall back to zone0 if absent.
THERM_TEMP=""
for _z in /sys/class/thermal/thermal_zone*; do
	if [[ "$(cat "${_z}/type" 2>/dev/null)" == x86_pkg_temp ]]; then
		THERM_TEMP="${_z}/temp"; break
	fi
done
THERM_TEMP="${THERM_TEMP:-/sys/class/thermal/thermal_zone0/temp}"
THROTTLE_DIR=/sys/devices/system/cpu/cpu0/thermal_throttle

pkg_temp_c() { echo $(( $(cat "$THERM_TEMP") / 1000 )); }

throttle_count() {
	echo $(( $(cat "$THROTTLE_DIR/core_throttle_count") \
		+ $(cat "$THROTTLE_DIR/package_throttle_count") ))
}

cooldown() {
	local waited=0 t
	while t=$(pkg_temp_c); [[ $t -ge $COOL_C && $waited -lt $COOL_MAX ]]; do
		echo "[~][$(date +"${DATE_FMT}")] cooling: pkg ${t}C >= ${COOL_C}C (waited ${waited}s)" \
			| tee -a "${LOG}"
		sleep "$COOL_POLL"; waited=$((waited + COOL_POLL))
	done
}

# NOTE: pts/nginx is expensive under batch-run. With RunAllTestCombinations=TRUE
# it runs all 7 connection counts x TimesToRun 3 x 90s ~= 32min PER ITERATION.
#
# pts/build-linux-kernel is PINNED to 1.17.1 (linux-6.15). Naming a test without
# a version makes PTS resolve it to the NEWEST profile on disk, and an
# openbenchmarking refresh pulled in 1.18.0 (linux-7.0), which is not installed.
# batch-run then prints "[PROBLEM] ... is not installed", records nothing, and
# STILL EXITS 0 -- which is why this test is absent from every sweep since.
# Pinning also keeps both halves of a sweep compiling the same source tree
# across the reboot, which is the whole point of the measurement.
TESTS="\
	pts/osbench             \
	pts/perf-bench          \
	pts/pmbench             \
	pts/stress-ng           \
	pts/build-linux-kernel-1.17.1 \
	pts/nettle              \
	pts/securemark          \
	pts/memcached           \
	pts/redis               \
	pts/network-loopback    \
	pts/openssl             \
	pts/nginx               \
	pts/gnupg               \
	system/wireguard"

# --- per-test option overrides -------------------------------------------
# batch-run with RunAllTestCombinations=TRUE runs EVERY menu option. For
# build-linux-kernel that means allmodconfig as well as defconfig: ~5461s vs
# ~336s per run, x TimesToRun 3, i.e. ~4.8h per half and ~9.6h for the pair.
# defconfig already exercises the compile/syscall/pagefault path we care
# about, so pin the option rather than pay for allmodconfig.
#
# PRESET_OPTIONS is read by PTS per test, so it is safe to export globally:
# tests that do not define a `build` option ignore it.
export PRESET_OPTIONS="build-linux-kernel.build=defconfig"

do_bmk() {
	bmk="${1:?"bmk name required"}"
	iters=${2:-10}

	for i in $(seq ${iters}); do
		cooldown
		before=$(throttle_count)
		echo "[+][$(date +"${DATE_FMT}")] Iteration #${i} (pkg $(pkg_temp_c)C)" | tee -a "${LOG}"
		phoronix-test-suite batch-run "${bmk}" | tee -a "${LOG}"
		after=$(throttle_count)
		if [[ $after -gt $before ]]; then
			echo "[!][$(date +"${DATE_FMT}")] THERMAL THROTTLE during ${bmk} iter #${i}:" \
				"+$((after - before)) events -- result is SUSPECT, discard/re-run" \
				| tee -a "${LOG}"
		fi
	done
}

# --- preflight: every test must be installed ------------------------------
# `batch-run` treats an uninstalled profile as a soft "[PROBLEM]" and moves on
# with exit status 0, so a missing test costs hours of sweep time and is only
# noticed when parse-results.py comes up short. Check up front instead.
MISSING=()
for t in ${TESTS}; do
	repo="${t%%/*}"; name="${t#*/}"
	if [[ "${name}" == *-[0-9]* ]]; then
		# Version pinned: that exact profile must be installed.
		[[ -d "${HOME}/.phoronix-test-suite/installed-tests/${repo}/${name}" ]] \
			|| MISSING+=("${t}")
	else
		# Unpinned: PTS resolves to the NEWEST profile on disk, so that is
		# the one that has to be installed -- not merely some older version.
		latest="$(ls -d "${HOME}/.phoronix-test-suite/test-profiles/${repo}/${name}-"* 2>/dev/null \
			| sed "s#.*/${name}-##" | sort -V | tail -1)"
		if [[ -z "${latest}" ]]; then
			MISSING+=("${t} (no profile on disk)")
		elif [[ ! -d "${HOME}/.phoronix-test-suite/installed-tests/${repo}/${name}-${latest}" ]]; then
			MISSING+=("${t} (resolves to ${name}-${latest}, not installed)")
		fi
	fi
done
if [[ ${#MISSING[@]} -gt 0 ]]; then
	echo "[!] Not installed, would be silently skipped by batch-run:" >&2
	printf '[!]   %s\n' "${MISSING[@]}" >&2
	echo "[!] Run ./install-pts-tests.sh (or pin the installed version in TESTS)." >&2
	exit 1
fi

if [[ ${CHECK_ONLY} -eq 1 ]]; then
	echo "[+] Every test in TESTS resolves to an installed profile:"
	for t in ${TESTS}; do echo "      ${t}"; done
	exit 0
fi

for t in ${TESTS}; do
	echo "[+][$(date +"${DATE_FMT}")] Running test: ${t}." | tee -a "${LOG}"
	do_bmk "${t}" ${ITERS}
	echo "[+][$(date +"${DATE_FMT}")] Done running test: ${t}." | tee -a "${LOG}"
done

echo "[+][$(date +"${DATE_FMT}")] Sweep '${TAG}' half '${VARIANT}' complete." | tee -a "${LOG}"
echo "[+] Results: ${RESULT_DIR}" | tee -a "${LOG}"
mv "${LOG}" "${HOME}/pts-result-log-${TAG}-${VARIANT}.txt"
