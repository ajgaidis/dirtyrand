# lmbench

lmbench 3.0-a9, the one fix it needs, and the configuration our results were
taken with. Backs Table 2.

```
lmbench-3.0-a9.tgz    pristine upstream, checksum-verified on unpack
prepare-lmbench.sh    unpack + patch, and print what to answer in `make config`
patches/              the lat_pagefault fix (see below)
CONFIG.reference      the config the reference results were taken with
```

## Dependencies

```bash
../harness/install-deps.sh          # or --check to just report
```

`libtirpc-dev` and `rpcbind` must be installed. `net-tools` is
worth having too.

## Setup

```bash
./prepare-lmbench.sh
cd lmbench-3.0-a9 && make config     # interactive; answers in the script's output
cd ../../harness && ./run-lmbench.sh vanilla
```

`make config` is deliberately not run for you: it probes *this* machine (RAM,
clock, hostname) and its answers must describe the actual testbed. The answers
that change the measurement are `BENCHMARK_HARDWARE=NO` and
`BENCHMARK_OS=YES`; compare the rest against `CONFIG.reference`, where
`ENOUGH`, `SYNC_MAX`, `LINE_SIZE` and the `BENCHMARK_*` flags should match and
`TOTAL_MEM`/`MB`/`MHZ`/`PROCESSORS` will correctly differ.
