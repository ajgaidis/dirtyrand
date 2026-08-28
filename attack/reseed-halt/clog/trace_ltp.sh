#!/bin/bash
set -e

echo "[+] Starting BPFtrace..."
sudo bpftrace probes/trace_queue_delayed_work.bt &> logs/ltp_delayed_raw.log &
bpftrace_delayed_pid=$!
sudo bpftrace probes/trace_queue_work.bt &> logs/ltp_raw.log &
bpftrace_pid=$!

sleep 2

echo "[+] Running LTP..."
cd /opt/ltp
sudo ./runltp

echo "[+] Waiting for tests to complete..."
wait

echo "[+] Stopping BPFtrace..."
sudo kill "${bpftrace_delayed_pid}"
sudo kill "${bpftrace_pid}"
sleep 1

