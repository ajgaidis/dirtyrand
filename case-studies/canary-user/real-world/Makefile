CC          = gcc
OPT         = -O2
DBG					= -g -Wall -Werror
GEN_CFLAGS	= $(OPT) $(DBG) -fPIC -fstack-protector-all
GEN_LDFLAGS	= -pie

KMOD_INC			= ../../primitives/kmod
KMOD_LIB			= ../../primitives/kmod
KMOD_CFLAGS		= $(GEN_CFLAGS) -I$(KMOD_INC)
KMOD_LDFLAGS	= -Wl,-rpath,$(KMOD_LIB) -L$(KMOD_LIB) -lprimitive $(GEN_LDFLAGS)

POC_INC			= ../../primitives/poc/inc
POC_LIB			= ../../primitives/poc
POC_CFLAGS	= $(GEN_CFLAGS) -I$(POC_INC)
POC_LDFLAGS	= $(GEN_LDFLAGS) -Wl,-rpath,$(POC_LIB) -L$(POC_LIB) -lprimitive

BIN = exploit-poc exploit-kmod

.PHONY: all
all: $(BIN)

exploit-poc: exploit.c
	$(CC) $(POC_CFLAGS) -o $@ $^ $(POC_LDFLAGS)

exploit-kmod: exploit.c
	$(CC) $(KMOD_CFLAGS) -o $@ $^ $(KMOD_LDFLAGS)

# Clean up this directory
.PHONY: clean
clean:
	rm -rf $(BIN) .cache
