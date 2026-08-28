# Compiler & flags
CC          = gcc
OPT         = -O2
COMPILE_DBG = -g -Wall -Werror
DBG         = $(COMPILE_DBG)
INC         = -Iinc
LIB_CFLAGS  = $(OPT) $(DBG) $(INC) -fPIC
LIB_LDFLAGS = -shared -pthread

# Paths
SRC         = src
INC_DIR     = inc

# Derived from this Makefile's own location, so the build works from any
# checkout path and any working directory. Every variable is overridable:
#   make VMLINUX=/path/to/vmlinux
#   make KERNEL_CONFIG=x86-defconfig-dbg
THIS_DIR       := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT_DIR       ?= $(abspath $(THIS_DIR)/../../../..)
KERNEL_VERSION ?= linux-6.12.11
KERNEL_CONFIG  ?= defconfig-dbg
VMLINUX        ?= $(ROOT_DIR)/build/$(KERNEL_VERSION)-$(KERNEL_CONFIG)/vmlinux

# Files
LIB         = libprimitive.so
EXPLOIT     = exploit
GEN         = offsets

SRC_FILES   = $(SRC)/keyring.c $(SRC)/libprimitive.c $(SRC)/netlink.c \
							$(SRC)/nf_tables.c $(SRC)/pipe_buffer.c $(SRC)/util.c

.PHONY: all clean offsets generate_offsets

# ---------------- All ----------------
all: generate_offsets $(LIB) $(EXPLOIT)

# ---------------- Offsets generator ----------------
$(GEN): $(SRC)/offsets.c
	$(CC) -O2 -Wall -o $(GEN) $(SRC)/offsets.c -lelf

generate_offsets: $(GEN)
	@test -r "$(VMLINUX)" || { \
		echo "error: vmlinux not found at $(VMLINUX)"; \
		echo "  build it with: ./tools/krng build kernel $(KERNEL_CONFIG)"; \
		echo "  or override:   make VMLINUX=/path/to/vmlinux"; \
		exit 1; }
	@mkdir -p $(INC_DIR)
	./$(GEN) $(VMLINUX) $(INC_DIR)/offsets.h

# ---------------- Shared library ----------------
$(LIB): generate_offsets $(SRC_FILES)
	$(CC) $(LIB_CFLAGS) -o $(LIB) $(SRC_FILES) $(LIB_LDFLAGS)

# ---------------- Exploit binary ----------------
$(EXPLOIT): exploit.c $(LIB)
	$(CC) -O2 $(INC) -o $(EXPLOIT) exploit.c $(LIB)

# ---------------- Clean ----------------
clean:
	rm -f $(LIB) $(EXPLOIT) $(GEN)
	rm -f $(INC_DIR)/offsets.h

