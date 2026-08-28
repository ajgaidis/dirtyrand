# Makefile for Kernel Stack Canary Trigger Module
# For Linux 6.12.11

# Module name
MODULE_NAME := stack_overflow_module

# Object files
obj-m := $(MODULE_NAME).o
$(MODULE_NAME)-objs := module.o

# Strip frame-pointer usage (omit rbp/ebp) so ROP chains don't depend on it
ccflags-y += -fomit-frame-pointer

# Paths are derived from this Makefile's own location so the build works from
# any checkout and any working directory. Everything is overridable:
#   make KERNEL_CONFIG=x86-defconfig-dbg
#   make KERNEL_SRC=/path/to/build/dir
THIS_DIR       := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT_DIR       ?= $(abspath $(THIS_DIR)/../..)
KERNEL_VERSION ?= linux-6.12.11
KERNEL_CONFIG  ?= defconfig-dbg
KERNEL_SRC     ?= $(ROOT_DIR)/build/$(KERNEL_VERSION)-$(KERNEL_CONFIG)

# VM rootfs directory
VM_ROOTFS      ?= $(ROOT_DIR)/build/rootfs

# Build directory
PWD := $(shell pwd)

# User-space compilation
CC          = gcc
OPT         = -O2
DBG         = -g -Wall -Werror
GEN_CFLAGS  = $(OPT) $(DBG) -fPIC -fstack-protector-all
GEN_LDFLAGS = -pie

KMOD_INC     = ../primitives/kmod
KMOD_LIB     = ../primitives/kmod
KMOD_CFLAGS  = $(GEN_CFLAGS) -I$(KMOD_INC)
KMOD_LDFLAGS = $(GEN_LDFLAGS) -Wl,-rpath,$(KMOD_LIB) -L$(KMOD_LIB) -lprimitive

# Source layout
EXPLOIT_DIR := exploit
TOOLS_DIR   := tools
# Vendored copy of the userspace ChaCha20 model, as kernel_slab also carries.
CHACHA_DIR     := lib/chacha
CHACHA_LIB_OBJ := $(CHACHA_DIR)/chacha_lib.o
INCLUDES    := -I. -I$(EXPLOIT_DIR) -I$(CHACHA_DIR)

# VM deploy (the running QEMU started by ../../launch-vm.sh)
VM_SSH_KEY  ?= $(VM_ROOTFS)/bookworm.id_rsa
VM_PORT     := 6868
VM_HOST     := root@127.0.0.1
VM_DEST     := /home/testuser/exploits
SSH_FLAGS   := -i $(VM_SSH_KEY) -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o BatchMode=yes -o ConnectTimeout=5
SSH_VM      := ssh -p $(VM_PORT) $(SSH_FLAGS) $(VM_HOST)
SCP_VM      := scp -P $(VM_PORT) $(SSH_FLAGS) -q
MODKRNG_KO  := ../primitives/kmod/modkrng.ko

# Default target
all: module exploit_escalation extract_canary_trigger extract_child canary_restore_payload exploit_rop

$(CHACHA_LIB_OBJ): $(CHACHA_DIR)/chacha_lib.c $(CHACHA_DIR)/chacha_lib.h
	$(CC) $(GEN_CFLAGS) -I$(CHACHA_DIR) -c -o $@ $<

# Build the kernel module
module:
	@echo "Building kernel module $(MODULE_NAME)..."
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

exploit_escalation: $(EXPLOIT_DIR)/exploit_escalation.c $(EXPLOIT_DIR)/predict_canary.c $(CHACHA_LIB_OBJ)
	$(CC) $(KMOD_CFLAGS) $(INCLUDES) -o $@ $(EXPLOIT_DIR)/exploit_escalation.c $(EXPLOIT_DIR)/predict_canary.c $(KMOD_LDFLAGS) $(CHACHA_LIB_OBJ)

# Build the user-space trigger program
extract_canary_trigger: $(TOOLS_DIR)/extract_canary_trigger.c
	$(CC) $(KMOD_CFLAGS) $(INCLUDES) -o $@ $< $(KMOD_LDFLAGS)

extract_child: $(TOOLS_DIR)/extract_child.c $(EXPLOIT_DIR)/predict_canary.c
	$(CC) $(GEN_CFLAGS) $(INCLUDES) -o $@ $(TOOLS_DIR)/extract_child.c $(EXPLOIT_DIR)/predict_canary.c $(GEN_LDFLAGS) $(CHACHA_LIB_OBJ)

canary_restore_payload: $(TOOLS_DIR)/canary_restore_payload.c
	$(CC) $(KMOD_CFLAGS) $(INCLUDES) -o $@ $< $(KMOD_LDFLAGS)

gadgets.h: $(KERNEL_SRC)/vmlinux $(KERNEL_SRC)/System.map $(TOOLS_DIR)/find_gadgets.sh
	./$(TOOLS_DIR)/find_gadgets.sh $(KERNEL_SRC) > $@

exploit_rop: $(EXPLOIT_DIR)/exploit_rop.c $(EXPLOIT_DIR)/predict_canary.c gadgets.h
	$(CC) $(GEN_CFLAGS) $(INCLUDES) -o $@ $(EXPLOIT_DIR)/exploit_rop.c $(EXPLOIT_DIR)/predict_canary.c $(GEN_LDFLAGS) $(CHACHA_LIB_OBJ)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	rm -f *.o *.ko *.mod *.mod.c *.mod.o
	rm -f .*.cmd .*.d .tmp_versions
	rm -rf .tmp_versions/
	rm -f exploit_escalation extract_canary_trigger extract_child canary_restore_payload exploit_rop canary_reverse_engineer
	rm -f gadgets.h $(CHACHA_LIB_OBJ)

# Copy artifacts (binaries + kernel modules + install script) into the
# running VM. Requires the VM to be up (../../launch-vm.sh from project root)
# and modkrng.ko to be already built in ../primitives/kmod/.
deploy: all
	@if [ ! -f "$(MODKRNG_KO)" ]; then \
		echo "[deploy] $(MODKRNG_KO) not found — build it once with: (cd ../primitives/kmod && make)"; \
		exit 1; \
	fi
	@echo "[deploy] copying to VM at $(VM_HOST):$(VM_PORT)..."
	@$(SSH_VM) 'mkdir -p $(VM_DEST)/kernel_canary $(VM_DEST)/primitives/kmod' \
		|| { echo "[deploy] cannot reach VM — is it running?"; exit 1; }
	@$(SCP_VM) exploit_escalation exploit_rop stack_overflow_module.ko install_modules.sh \
		$(VM_HOST):$(VM_DEST)/kernel_canary/
	@$(SCP_VM) $(MODKRNG_KO) $(VM_HOST):$(VM_DEST)/primitives/kmod/
	@$(SSH_VM) 'chown -R testuser:testuser $(VM_DEST)'
	@echo "[deploy] done. ssh -p $(VM_PORT) testuser@127.0.0.1 (password: test) -> ./install_modules.sh && ./exploit_escalation"

# Install module (requires root)
install: module
	@echo "Installing module $(MODULE_NAME)..."
	sudo insmod $(MODULE_NAME).ko

# Copy module to VM rootfs
install-vm: module
	@echo "Copying module to VM rootfs image..."
	@if [ -f "$(VM_ROOTFS)/bookworm.img" ]; then \
		echo "Mounting rootfs image..."; \
		sudo mkdir -p /mnt/vm_rootfs; \
		sudo mount -o loop $(VM_ROOTFS)/bookworm.img /mnt/vm_rootfs; \
		sudo mkdir -p /mnt/vm_rootfs/root/modules; \
		sudo cp $(MODULE_NAME).ko /mnt/vm_rootfs/root/modules/; \
		sudo umount /mnt/vm_rootfs; \
		sudo rmdir /mnt/vm_rootfs; \
		echo "Module copied to VM rootfs image"; \
		echo "Boot your VM and run: insmod /root/modules/$(MODULE_NAME).ko"; \
	else \
		echo "VM rootfs image not found at $(VM_ROOTFS)/bookworm.img"; \
		echo "Please check the VM_ROOTFS path in Makefile"; \
	fi

# Remove module (requires root)
remove:
	@echo "Removing module $(MODULE_NAME)..."
	sudo rmmod $(MODULE_NAME) || true

# Show module info
info:
	@echo "Module information:"
	modinfo $(MODULE_NAME).ko 2>/dev/null || echo "Module not built yet"

.PHONY: all module clean install install-vm deploy remove test info dmesg help