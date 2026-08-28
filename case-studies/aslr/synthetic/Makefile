CC          = gcc
OPT         = -O2
DBG					= -g -Wall -Werror -DDEBUG
GEN_CFLAGS	= $(OPT) $(DBG) -fPIC
GEN_LDFLAGS	= -pie

KMOD_INC			= ../../primitives/kmod
KMOD_LIB			= ../../primitives/kmod
KMOD_CFLAGS		= $(GEN_CFLAGS) -I$(KMOD_INC)
KMOD_LDFLAGS	= $(GEN_LDFLAGS) -Wl,-rpath,$(KMOD_LIB) -L$(KMOD_LIB) -lprimitive

POC_INC			= ../../primitives/poc/inc
POC_LIB			= ../../primitives/poc
POC_CFLAGS	= $(GEN_CFLAGS) -I$(POC_INC)
POC_LDFLAGS	= $(GEN_LDFLAGS) -Wl,-rpath,$(POC_LIB) -L$(POC_LIB) -lprimitive

BIN = guess-base aslr-poc aslr-kmod read_aslr
HDR = logging.h

.PHONY: all
all: $(BIN)

guess-base: guess_base.c $(HDR)
	$(CC) $(GEN_CFLAGS) -o $@ $< $(GEN_LDFLAGS)

read_aslr: read_aslr.c $(HDR)
	$(CC) $(GEN_CFLAGS) -o $@ $< $(GEN_LDFLAGS)

aslr-kmod: aslr.c read_aslr $(HDR)
	$(CC) $(KMOD_CFLAGS) -o $@ $< $(KMOD_LDFLAGS)

aslr-poc: aslr.c read_aslr $(HDR)
	$(CC) $(POC_CFLAGS) -o $@ $< $(POC_LDFLAGS)


# Clean up this directory
.PHONY: clean
clean:
	rm -rf $(BIN) .cache
