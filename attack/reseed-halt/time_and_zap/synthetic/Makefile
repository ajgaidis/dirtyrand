obj-m	           += modkrng.o
KERNEL_BUILD_DIR  = ../linux-6.12.11

LIB         = libprimitive.so
KMOD        = modkrng.ko
DEVICE_NAME = /dev/modkrng

CC          = gcc
OPT         = -O2
COMPILE_DBG = -g -Wall -Werror
RUNTIME_DBG = -DDEBUG
DBG         = $(COMPILE_DBG) #$(RUNTIME_DBG)
LIB_CFLAGS  = $(OPT) $(DBG) -fPIC
LIB_LDFLAGS = -shared

.PHONY: all
all: $(KMOD) $(LIB)

# Build the KRNG kernel module
$(KMOD): $(basename $(KMOD)).c
	make -C $(KERNEL_BUILD_DIR) M=$(PWD) modules

# Build the user-space library that communicates with the KRNG kernel module
$(LIB): $(basename $(LIB)).c
	$(CC) $(LIB_CFLAGS) -o $@ $^ $(LIB_LDFLAGS)

# Install the KRNG kernel module
.PHONY: install
install: $(KMOD)
	sudo insmod ./$(KMOD)
	sudo mknod $(DEVICE_NAME) c 144 0

# Uninstall the KRNG kernel module
.PHONY: uninstall
uninstall:
	sudo rm -f $(DEVICE_NAME)
	sudo rmmod $(basename $(KMOD)) || true

# Clean up this directory
.PHONY: clean
clean: uninstall
	make -C $(KERNEL_BUILD_DIR) M=$(PWD) clean
	rm -rf $(LIB) .cache
