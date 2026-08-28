#!/bin/bash
# One iteration of just lmbench's RPC latency test, for verifying that
# lat_rpc works without paying for a whole `make rerun` (~15-20 min).
#
# Reproduces what scripts/lmbench does for RPC:
#     for server in ... lat_rpc ...; do $server -s; done
#     lat_rpc -P $SYNC_MAX -p udp localhost
#     lat_rpc -P $SYNC_MAX -p tcp localhost
#     lat_rpc -S localhost
# including the env it exports from CONFIG.`uname -n` (line 22 of scripts/lmbench),
# since ENOUGH/TIMING_O/LOOP_O change how long each measurement runs.

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LMB="${RNGUARD_LMBENCH_DIR:-${HERE}/../lmbench/lmbench-3.0-a9}"
BIN="${LMB}/bin/$(uname -m)-linux-gnu"
CFG="${BIN}/CONFIG.$(uname -n)"

[ -r "$CFG" ] || { echo "[!] no $CFG" >&2; exit 1; }
. "$CFG"
export ENOUGH TIMING_O LOOP_O SYNC_MAX LINE_SIZE LMBENCH_SCHED

cd "$BIN" || exit 1

if ! rpcinfo -p localhost >/dev/null 2>&1; then
	echo "[!] rpcbind is not answering on :111 -- lat_rpc cannot register."
	echo "[!]   sudo systemctl start rpcbind    (or: sudo apt-get install rpcbind)"
	exit 1
fi
echo "[+] rpcbind is up."

# A server left over from an earlier run would still hold XACT_PROG.
pkill -x lat_rpc 2>/dev/null; sleep 1

# NOTE: -s forks a child that runs svc_run() forever and INHERITS stdout.
# If you start it with its output on a pipe, the pipe never closes and the
# caller looks hung -- so send it to /dev/null.
./lat_rpc -s >/dev/null 2>&1 </dev/null
sleep 1

if ! rpcinfo -p localhost 2>/dev/null | grep -q 404040; then
	echo "[!] lat_rpc server did not register XACT_PROG (404040)." >&2
	exit 1
fi
echo "[+] lat_rpc server registered:"
rpcinfo -p localhost | awk '$1==404040 {printf "      %s %s port %s\n", $1, $3, $4}'

echo "[+] ENOUGH=$ENOUGH SYNC_MAX=$SYNC_MAX -- each measurement takes ~35s"
rc=0
for proto in tcp udp; do
	printf "[+] lat_rpc -p %s ... " "$proto"
	out=$(./lat_rpc -P "${SYNC_MAX:-1}" -p "$proto" localhost 2>&1 </dev/null)
	case "$out" in
		*"latency using"*) echo "OK   ${out}" ;;
		*)                 echo "FAIL ${out}"; rc=1 ;;
	esac
done

./lat_rpc -S localhost >/dev/null 2>&1 </dev/null
pkill -x lat_rpc 2>/dev/null
echo "[+] server shut down."
exit $rc
