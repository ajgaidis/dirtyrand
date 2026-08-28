#!/bin/bash

# Resolve lmbench relative to this script so the tree is relocatable.
# Override with RNGUARD_LMBENCH_DIR if you keep lmbench somewhere else.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BMK_DIR="${RNGUARD_LMBENCH_DIR:-${HERE}/../lmbench/lmbench-3.0-a9}"
if [ ! -d "${BMK_DIR}" ]; then
	echo "[!] lmbench tree not found at ${BMK_DIR}" >&2
	echo "[!] Build it first (see README), or set RNGUARD_LMBENCH_DIR." >&2
	exit 1
fi
BMK_DIR="$(cd "${BMK_DIR}" && pwd)"

# glibc >= 2.32 dropped Sun RPC; prepare-lmbench.sh records the libtirpc
# include/link flags here so a rebuild from `make rerun` gets them too.
RPC_ENV="${BMK_DIR}/../rpc-env.sh"
# shellcheck source=/dev/null
[[ -s "${RPC_ENV}" ]] && . "${RPC_ENV}"
RESULTS_DIR="${BMK_DIR}/results/x86_64-linux-gnu"
DATE_FMT="%F ~ %T"

ITERS=10

# --- rpcbind preflight ---------------------------------------------------
# lat_rpc's server (`lat_rpc -s`) registers XACT_PROG with the portmapper. With
# no rpcbind listening on :111 it dies with
#
#     unable to register (XACT_PROG, XACT_VERS, udp).
#
# and the two clients then fail ("RPC: Timed out" / "Connection refused"),
# costing both RPC latency numbers. `make rerun` still exits 0, so the only
# symptom is missing lines in the result file -- exactly the failure that ate
# every run up to 2026-08-21.
#
# rpcbind.service is WantedBy=multi-user.target, so it does NOT come up in
# single-user/rescue mode, which is how these sweeps are meant to be booted.
# Being installed and `systemctl enable`d is therefore NOT enough; check that
# it is actually answering, and start it if it isn't.
check_rpcbind()
{
	if rpcinfo -p localhost >/dev/null 2>&1; then
		echo "[+] rpcbind is answering on :111 (lat_rpc can register)."
		return 0
	fi

	echo "[!] rpcbind is not answering on :111 -- lat_rpc would fail to register."
	if ! command -v rpcbind >/dev/null 2>&1; then
		echo "[!] rpcbind is not installed:  sudo apt-get install rpcbind" >&2
		exit 1
	fi

	echo " |----> starting rpcbind..."
	sudo systemctl start rpcbind.socket rpcbind.service >/dev/null 2>&1 \
		|| sudo systemctl start rpcbind >/dev/null 2>&1 \
		|| sudo rpcbind >/dev/null 2>&1

	sleep 1
	if rpcinfo -p localhost >/dev/null 2>&1; then
		echo "[+] rpcbind is now answering on :111."
		return 0
	fi

	echo "[!] Could not bring rpcbind up. lat_rpc would silently lose both RPC" >&2
	echo "[!] latency numbers, so refusing to run. Start it by hand and retry." >&2
	exit 1
}

# scripts/results names its output  results/$OS/`uname -n`.$EXT  (EXT counts up
# while the file exists), so the freshly produced file is ${HOST}.something.
HOST="$(uname -n)"

do_bmk() {
	variant="${1:?"variant name required"}"
	iters=${2:-10}

	pushd "${BMK_DIR}"

	for i in $(seq ${iters}); do
		echo "[+][$(date +"${DATE_FMT}")] Iteration #${i}"
		make rerun

		# Key the rename on ${HOST}.* rather than `ls -t | head -1`.
		# RESULTS_DIR also holds archive SUBDIRECTORIES (results-before-*),
		# and `ls -t` lists those too -- so if `make rerun` ever produced no
		# result (it exits 0 even when a benchmark dies), the old code would
		# rename an archive directory, or the previous iteration's file, to
		# ${variant}.${i} and silently destroy it.
		mapfile -t produced < <(
			find "${RESULTS_DIR}" -maxdepth 1 -type f -name "${HOST}.*" \
				-printf '%T@ %p\n' 2>/dev/null | sort -rn | cut -d' ' -f2-)

		if [[ ${#produced[@]} -eq 0 ]]; then
			echo "[!] iteration ${i}: 'make rerun' produced no ${HOST}.* result file" >&2
			echo "[!] in ${RESULTS_DIR} -- refusing to guess. Nothing renamed." >&2
			popd; exit 1
		fi
		if [[ ${#produced[@]} -gt 1 ]]; then
			echo "[!] iteration ${i}: more than one ${HOST}.* file present;" >&2
			echo "[!] using the newest ($(basename "${produced[0]}")). Stale leftovers:" >&2
			printf '[!]   %s\n' "${produced[@]:1}" >&2
		fi

		mv "${produced[0]}" "${RESULTS_DIR}/${variant}.${i}"
	done

	popd
}

check_rpcbind
do_bmk "${1}" ${ITERS}
