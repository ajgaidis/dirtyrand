# Harness

One entry point drives both suites, because the two halves of a sweep are
separated by a reboot.

```
run-bmks.sh         entry point -- one invocation per boot; runs lmbench then PTS
run-pts.sh          PTS half   (also: --check, a 0.1 s preflight)
run-lmbench.sh      lmbench half
parse-results.py    reads BOTH suites, reports overhead and dispersion
install-pts.sh      PTS 10.8.4 from the bundled .deb, checksum-verified
install-pts-tests.sh  test profiles + their dependencies
check-rpc.sh        standalone check that lmbench lat_rpc can register
bmk-env.sh          quiets the box: no turbo, pinned clock, verified under load
```

## run-bmks.sh

```bash
# ... boot into the vanilla kernel ...
./run-bmks.sh --new-sweep
# ... reboot into the rnguard kernel ...
./run-bmks.sh
```

## parse-results.py

```bash
./parse-results.py                      # both suites, this machine's results
./parse-results.py --suite both         # both suites; default
./parse-results.py --suite lmbench      # one suite
./parse-results.py --csv                # machine-readable, suite in column 1
./parse-results.py --clamp-negative     # floor negative overhead in aggregates
```
