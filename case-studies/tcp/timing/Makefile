CC          = gcc
OPT         = -O2
DBG					= -g -Wall -Werror -Wno-error=format-zero-length
GEN_CFLAGS	= $(OPT) $(DBG) -fPIC
GEN_LDFLAGS	= -pie

.PHONY: all
all: timing

timing: timing.c
	$(CC) $(GEN_CFLAGS) -o $@ $^ $(GEN_LDFLAGS)

.PHONY: clean
clean:
	rm -f timing
