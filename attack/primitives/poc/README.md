# How to build 
Build: ``gcc -O2 -I./inc -o exploit exploit.c libprimitive.so``

To run within the VM, it may be necessary to run
``export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH`` from the same directory as ``limprimitive.so``. 

# How to run 
Usage: ``./exploit`` 

The exploit can be started at any time after the system has reached steady-state (requires waiting a minute or two after VM launch). The effects of running the exploit can be observed with a breakpoint on ``b crng_reseed``. The breakpoint will never be hit after exploit success. 

The exploit will continue to zap the _predicted_ reseed events, regardless of whether reseeding has halted. The user must manually stop the exploit with ``CTRL-C``. 

# Directory structure 
* ``exploit.c``: end-to-end exploit to time and zap the reseed cycle
* ``random_data.bin``: pre-computed random data for timing the reseed cycle, generated from ``krng/src/chacha``
