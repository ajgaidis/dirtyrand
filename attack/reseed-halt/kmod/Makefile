obj-m += modkrng.o

KMOD		= modkrng.ko
DEVICE_NAME 	= /dev/modkrng

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

.PHONY: clean
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

.PHONY: install
install: $(KMOD)
	insmod ./$(KMOD)
	mknod $(DEVICE_NAME) c 144 0

.PHONY: uninstall
uninstall: 
	rm -f $(DEVICE_NAME)
	rmmod $(basename $(KMOD)) || true


