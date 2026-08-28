#!/bin/bash

THREADS_LIST=(1 16 32 64 128 256 512)
PAGES_LIST=(1 2 3 4 5 8 16 32 64)
TRACE_SCRIPT="probes/trace_reseed.bt"
PTY_WRITE="./pty_write"
LOG_DIR="logs/time_reseeds"
TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")
LOG_FILE="$LOG_DIR/log_$TIMESTAMP.txt"
TIMEOUT_DURATION=60s

echo "[+] Starting bpftrace..."
stdbuf -oL sudo bpftrace "$TRACE_SCRIPT" >> "$LOG_FILE" 2>&1 &
BPFTRACE_PID=$!
sleep 2  

trap "echo '[!] Stopping bpftrace...'; sudo kill $BPFTRACE_PID" EXIT

{
echo "==== Test Run: $(date) ====" | tee -a "$LOG_FILE"
for threads in "${THREADS_LIST[@]}"; do
    for pages in "${PAGES_LIST[@]}"; do
        echo "----------------------------------------"
        echo "Running: threads=$threads, pages=$pages" 
        echo "Start Time: $(date +"%Y-%m-%d %H:%M:%S")" 

       	timeout "$TIMEOUT_DURATION" $PTY_WRITE "$threads" "$pages" 2>&1

        echo "End Time: $(date +"%Y-%m-%d %H:%M:%S")" 
        echo ""
    done
done
} | tee -a "$LOG_FILE"

echo "==== Test Completed: $(date) ====" | tee -a "$LOG_FILE"

