# Install the module for mokdrn and the vulnerable stack overflow module
sudo insmod ../primitives/kmod/modkrng.ko
sudo mknod /dev/modkrng c 144 0
sudo insmod stack_overflow_module.ko