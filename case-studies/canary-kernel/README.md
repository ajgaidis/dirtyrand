# Kernel Canary Exploit Project

Tools and exploits demonstrating a stack-canary bypass and ROP-to-root in Linux 6.12.11. The canary is recovered by zapping the per-CPU ChaCha CRNG and replicating it in userspace; the ROP chain calls `commit_creds(&init_cred)` to return UID 0.

## End-to-end workflow

The exploit runs inside a QEMU VM that boots the custom kernel (see `tools/build.sh` at the project root for the kernel build). The flow is: build on host → boot VM → copy artifacts in → run as `testuser` → root shell.

### 1. Build on the host (in this directory)

```bash
make                       # builds module + all userspace binaries + gadgets.h
make deploy                # scp built artifacts into the running VM (step 3)
make clean                 # removes build artifacts and gadgets.h
```

`gadgets.h` is auto-generated from `$(KERNEL_SRC)/{vmlinux,System.map}` by `tools/find_gadgets.sh`. Do not edit it by hand.

Outputs (all at this directory's root):
`exploit_escalation`, `exploit_rop`, `stack_overflow_module.ko` (the three needed at runtime), plus the diagnostic binaries `extract_canary_trigger`, `extract_child`, `canary_restore_payload`.

### 2. Boot the VM

From the project root (the repo root, `$KRNG` below):

```bash
./launch-vm.sh --interactive   # serial console in your terminal; SSH also up on 127.0.0.1:6868 (root/root). Ctrl+C to stop.
# or
./launch-debug-vm.sh       # same VM with gdbstub on :1235 for kernel debugging
```

The VM exposes SSH on host port `6868`. Key auth uses `build/rootfs/bookworm.id_rsa`; password auth is `root` / `root`. There is also a `testuser` account (password: `test`, in the `sudo` group) — exploits run as this user.

### 3. Copy the exploit artifacts into the VM

```bash
make deploy
```

This `scp`s `exploit_escalation`, `exploit_rop`, `stack_overflow_module.ko`, and `install_modules.sh` to `/home/testuser/exploits/kernel_canary/`, plus `modkrng.ko` to `/home/testuser/exploits/primitives/kmod/`, and `chown`s everything to `testuser`. Uses key auth (`build/rootfs/bookworm.id_rsa` → `root`) so it does not prompt for a password.

`modkrng.ko` must already be built — one-time setup: `cd ../primitives/kmod && make`.

The project root also exposes shell helpers via `source $KRNG/tools/common.sh` — `vmcopy`, `vmcmd`, `vmstart`, `vmstop` — for ad-hoc operations.

### 4. Run the exploit (inside the VM, as `testuser`)

```bash
cd ~/exploits/kernel_canary
./install_modules.sh                            # asks for sudo password (test); insmods both modules
./exploit_escalation                            # pins to CPU0, zaps RNG, forks, execs ./exploit_rop
# -> "[*] We are root, spawning shell..."
# id
# uid=0(root) gid=0(root) groups=0(root)
```

`install_modules.sh` only needs to run once per VM boot. After that you can re-run `./exploit_escalation` directly — the L1-key + batch-entropy zap inside the binary makes consecutive runs deterministic. If a run does panic the VM (canary mispredict → `__stack_chk_fail`), the SSH session will drop with `Connection closed by remote host`; relaunch the VM and try again rather than poking around with `rmmod`/`insmod` on a wedged kernel.

## Layout

```
kernel_canary/
├── README.md
├── Makefile
├── install_modules.sh        # VM-side: insmod modkrng + stack_overflow_module
├── module.c                  # kernel module source (kbuild requires it at root)
├── gadgets.h                 # AUTO-GENERATED, gitignored — do not edit
│
├── exploit/                  # main privilege-escalation exploit
│   ├── exploit_escalation.c  #   zaps RNG, forks, execs exploit_rop
│   ├── exploit_rop.c         #   builds ROP chain from gadgets.h, triggers overflow
│   ├── predict_canary.{c,h}  #   userspace ChaCha20 replica → canary prediction
│   └── logging.h             #   shared log/warn/handle_error/hexdump macros
│
├── tools/                    # build helpers and diagnostic utilities
│   ├── find_gadgets.sh       #   emits gadgets.h from vmlinux + System.map
│   ├── extract_canary_trigger.c  # sanity-check canary prediction (no ROP)
│   ├── extract_child.c       #   helper for canary extraction via kernel module
│   ├── canary_restore_payload.c  # demos clean return through a restored canary
│   └── sleeper.sh            #   small timing helper used during testing
│
└── docs/
    └── gdb_canary_inspection.md  # notes on inspecting canary state under GDB
```

Build outputs (`exploit_escalation`, `exploit_rop`, `extract_canary_trigger`, `extract_child`, `canary_restore_payload`, and `stack_overflow_module.ko`) all land at the root — that's where `install_modules.sh` and `exploit_escalation`'s `execl("./exploit_rop", ...)` expect them.

### Address resolution

`gadgets.h` resolves `commit_creds`, `prepare_kernel_cred`, `init_cred` from `System.map`, and finds a `pop rdi; ret` gadget by scanning `vmlinux`'s `.text` raw bytes for `5f c3`. `RET_GADGET` is derived as `POP_RDI_RET_GADGET + 1` (the trailing `c3` byte). `exploit_rop.c` consumes those macros — there are no hardcoded addresses left in the source.

## Notes

- Boot flags `nokaslr slab_nomerge` are required (set by `tools/common.sh` at the project root, not the `tools/` here).
- The ChaCha CRNG is per-CPU, so the predicting task and the syscall-issuing child must stay on the same CPU. `exploit_escalation` and `exploit_rop` both `sched_setaffinity` to CPU 0 — no manual `taskset` needed.
- The zap loop overwrites both the L1 key and the per-CPU batch entropy. Without the batch zap, residual pre-zap bytes can still satisfy the next `get_random_long()` and the prediction misses.
- Rebuilding the kernel changes addresses; just re-run `make` — `gadgets.h` regenerates automatically because it depends on `vmlinux`/`System.map`.
