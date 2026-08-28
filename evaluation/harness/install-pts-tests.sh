#!/bin/bash
set -euo pipefail

DATE_FMT="%F ~ %T"

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

sudo apt-get update
sudo apt-get install netcat-openbsd

for test in ${TESTS}; do
	echo "[+][$(date +"${DATE_FMT}")] Installing dependencies: ${test}."
	sudo phoronix-test-suite install-dependencies "${test}" 
	echo "[+][$(date +"${DATE_FMT}")] Installing: ${test}."
	sudo phoronix-test-suite install "${test}" 
done
