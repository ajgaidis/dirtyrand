# Phoronix Test Suite

PTS 10.8.4 settings and the four out-of-tree fixes it needs. Backs Table 3.

```
apply-patches.sh      applies everything below; --check reports without changing
patches/pts/          PTS wrapper and core-command patches
patches/shims/        gpg-error-config / ksba-config (Debian 12)
pts-user-config.xml   the PTS settings the sweep assumes
```

## Setup

```bash
cd ../harness
./install-deps.sh           # php-cli, php-xml, and the test deps apt misses
./install-pts.sh            # PTS 10.8.4 from the bundled, checksum-verified .deb
./install-pts-tests.sh      # profiles + dependencies (long; downloads a lot)
cd ../phoronix && ./apply-patches.sh
cd ../harness && ./run-pts.sh --check   # confirms every test resolves + installed
```

Then merge `pts-user-config.xml` into `~/.phoronix-test-suite/user-config.xml`.
The settings that matter:

| Setting | Value | Why |
|---|---|---|
| `RunAllTestCombinations` | `TRUE` | run every subtest of each suite |
| `PromptForTestIdentifier` / `PromptSaveName` / `PromptForTestDescription` | `FALSE` | batch mode must not block on prompts |
| `UploadResults` / `OpenBrowser` | `FALSE` | headless, offline |
| `DynamicRunCount` | `TRUE` | re-runs noisy tests |
