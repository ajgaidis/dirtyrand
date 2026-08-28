CC          = gcc
OPT         = -O2
COMPILE_DBG = -g -Wall -Werror
DBG         = $(COMPILE_DBG)
INC					= -I./inc
LIB_CFLAGS  = $(OPT) $(DBG) $(INC) -fPIC
LIB_LDFLAGS = -shared -pthread

SRC_DIR = src
INC_DIR = inc

# Derived from this Makefile's own location, so the build works from any
# checkout path and any working directory. Every variable is overridable:
#   make VMLINUX=/path/to/vmlinux
#   make KERNEL_CONFIG=x86-defconfig-dbg
THIS_DIR       := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT_DIR       ?= $(abspath $(THIS_DIR)/../../..)
KERNEL_VERSION ?= linux-6.12.11
KERNEL_CONFIG  ?= defconfig-dbg
VMLINUX        ?= $(ROOT_DIR)/build/$(KERNEL_VERSION)-$(KERNEL_CONFIG)/vmlinux

LIB = libprimitive.so
GEN = offsets

SRC_FILES   = $(SRC_DIR)/keyring.c $(SRC_DIR)/libprimitive.c $(SRC_DIR)/netlink.c \
							$(SRC_DIR)/nf_tables.c $(SRC_DIR)/pipe_buffer.c $(SRC_DIR)/util.c

.PHONY: all clean offsets generate_offsets
all: generate_offsets $(LIB)

$(GEN): $(SRC_DIR)/offsets.c
	$(CC) -O2 -Wall -o $(GEN) $(SRC_DIR)/offsets.c -lelf

generate_offsets: $(GEN)
	@test -r "$(VMLINUX)" || { \
		echo "error: vmlinux not found at $(VMLINUX)"; \
		echo "  build it with: ./tools/krng build kernel $(KERNEL_CONFIG)"; \
		echo "  or override:   make VMLINUX=/path/to/vmlinux"; \
		exit 1; }
	@mkdir -p $(INC_DIR)
	./$(GEN) $(VMLINUX) $(INC_DIR)/offsets.h

$(LIB): generate_offsets $(SRC_FILES)
	$(CC) $(LIB_CFLAGS) -o $(LIB) $(SRC_FILES) $(LIB_LDFLAGS)

clean:
	rm -f $(LIB) $(GEN)
	rm -f $(INC_DIR)/offsets.h
	rm -rf .cache
