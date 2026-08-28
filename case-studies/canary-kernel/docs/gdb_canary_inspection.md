# GDB Kernel Stack Canary Inspection Guide



```bash
# Connect to QEMU VM (adjust port as needed)
gdb vmlinux
target remote localhost:1235
# b *0xffffffff813dfaa4 (pop rdi; ret) should hit
# b *0xffffffff810c5eb0 (prepare_kernel_cred) should hit
# b *0xffffffff810c5950 (commit_creds) should hit
# Load kernel symbols
#(gdb) add-symbol-file ../../exploits/kernel_canary/canary_trigger.ko
# 0xffffffffa0006000
cat /sys/module/canary_trigger/sections/.text
add-symbol-file ../../exploits/kernel_canary/canary_trigger.ko -s .text 0xffffffffa0006000
### 2. Set Breakpoints for Canary Inspection

# Break on module functions
break vuln_func
```

0xffffffffa0000000

target remote localhost:1235
add-symbol-file $KRNG/build/linux-6.12.11-defconfig-dbg/net/dccp/dccp.ko 0xffffffffa0000000
break dccp_create_openreq_child


add-auto-load-safe-path $KRNG/build/linux-6.12.11-defconfig-dbg
source $KRNG/build/linux-6.12.11-defconfig-dbg/vmlinux-gdb.py
lx-symbols
break dccp_create_openreq_child