#!/bin/bash
# Unpack lmbench 3.0-a9 and apply the lat_pagefault fix.
#
# Leaves you with ./lmbench-3.0-a9 ready for `make config` (interactive) and
# then `../harness/run-lmbench.sh`. Deliberately does NOT run `make config`
# for you: that step probes THIS machine (RAM, MHz, hostname) and its answers
# must describe the actual testbed. See CONFIG.reference for the answers that
# matter.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

SRC=lmbench-3.0-a9

if [[ -d "$SRC" ]]; then
	echo "[!] $SRC already exists -- remove it first to start clean." >&2
	exit 1
fi

TGZ=lmbench-3.0-a9.tgz
TGZ_SHA256=cbd5777d15f44eab7666dcac418054c3c09df99826961a397d9acf43d8a2a551
got="$(sha256sum "$TGZ" | cut -d' ' -f1)"
if [[ "$got" != "$TGZ_SHA256" ]]; then
	echo "[!] checksum mismatch for $TGZ" >&2
	echo "[!]   expected $TGZ_SHA256" >&2
	echo "[!]   got      $got" >&2
	exit 1
fi
echo "[+] checksum OK: $TGZ"

# --- Sun RPC headers ------------------------------------------------------
# src/bench.h includes <rpc/rpc.h> unconditionally, and all 60 source files
# include bench.h, so this decides whether lmbench compiles AT ALL -- it is
# not just about lat_rpc. glibc removed its Sun RPC implementation in 2.32,
# so on anything newer than Debian 11 / Ubuntu 20.04 the headers come from
# libtirpc instead, under a different prefix.
#
# CPPFLAGS and LDLIBS are used verbatim by src/Makefile's COMPILE and link
# lines, and survive the `env CFLAGS=-O` that scripts/build interposes, so
# exporting them is enough -- lmbench needs no patch for this.
RPC_ENV=rpc-env.sh
if [[ -e /usr/include/rpc/rpc.h ]]; then
	echo "[+] rpc/rpc.h found in the default include path"
	: > "$RPC_ENV"
elif [[ -e /usr/include/tirpc/rpc/rpc.h ]]; then
	echo "[+] rpc/rpc.h found via libtirpc; writing $RPC_ENV"
	cat > "$RPC_ENV" <<'ENVEOF'
# Written by prepare-lmbench.sh. glibc >= 2.32 has no Sun RPC; use libtirpc.
export CPPFLAGS="${CPPFLAGS:-} -I/usr/include/tirpc"
export LDLIBS="${LDLIBS:-} -ltirpc"
ENVEOF
else
	cat >&2 <<'ERREOF'
[!] Neither /usr/include/rpc/rpc.h nor /usr/include/tirpc/rpc/rpc.h exists.
[!] lmbench will fail to compile -- every source file includes bench.h,
[!] which includes <rpc/rpc.h>.
[!]
[!]     sudo apt-get install libtirpc-dev     (or: ../harness/install-deps.sh)
[!]
[!] Then re-run this script.
ERREOF
	exit 1
fi

tar xzf "$TGZ"
echo "[+] unpacked $SRC"

patch -p1 -d "$SRC" < patches/0001-lat_pagefault-size_t-and-1GiB-cap.patch
echo "[+] applied lat_pagefault patch"

cat <<'MSG'

Next:
  1. cd lmbench-3.0-a9
     [ -s ../rpc-env.sh ] && . ../rpc-env.sh   # only if libtirpc is in use
     make config                               (interactive -- see NOTES below)
  2. cd .. && ../harness/run-lmbench.sh   (driven by run-bmks.sh normally)

  run-lmbench.sh sources rpc-env.sh itself; step 1 is manual because
  `make config` compiles, so it needs the same flags.

NOTES on `make config` -- the answers that change the measurement:
  * "Do you want to run the hardware benchmarks?"     -> NO
    (BENCHMARK_HARDWARE=NO; skips lat_mem_rd/tlb/par_mem/stream, which
     measure the machine, not the kernel.)
  * "Do you want to run the OS benchmarks?"           -> YES
  * Mail results to lmbench creators?                 -> NO
  * Everything else: accept the defaults.
  * It writes bin/<OS>/CONFIG.<hostname>. Compare it against CONFIG.reference
    in this directory -- ENOUGH, SYNC_MAX, LINE_SIZE and the BENCHMARK_*
    flags should match; TOTAL_MEM/MB/MHZ/PROCESSORS will (correctly) differ.

WARNING: lmbench's licence (src/lat_pagefault.c:8) permits publishing results
only from an UNMODIFIED benchmark. The lat_pagefault patch above is a
modification and must be disclosed in any writeup that uses its numbers.
MSG
