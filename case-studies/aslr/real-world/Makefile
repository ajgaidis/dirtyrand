CC          = gcc
OPT         = -O2
DBG					= -g -Wall -Werror -Wno-attributes -DDEBUG
CFLAGS			= $(OPT) $(DBG) -fPIC
BIN_LDFLAGS	= -pie
LIB_LDFLAGS	= -shared -ldl

KMOD_INC			= ../../primitives/kmod
KMOD_LIB			= ../../primitives/kmod
KMOD_CFLAGS		= $(CFLAGS) -I$(KMOD_INC)
KMOD_LDFLAGS	= $(BIN_LDFLAGS) -Wl,-rpath,$(KMOD_LIB) -L$(KMOD_LIB) -lprimitive

POC_INC			= ../../primitives/poc/inc
POC_LIB			= ../../primitives/poc
POC_CFLAGS	= $(CFLAGS) -I$(POC_INC)
POC_LDFLAGS	= $(BIN_LDFLAGS) -Wl,-rpath,$(POC_LIB) -L$(POC_LIB) -lprimitive

LIB = suid_lib.so
BIN = exploit-poc exploit-kmod
HDR = logging.h

.PHONY: all
all: $(LIB) $(BIN)

$(LIB): $(LIB:.so=.c) $(HDR)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_LDFLAGS)

exploit-kmod: exploit.c $(HDR)
	$(CC) $(KMOD_CFLAGS) -o $@ $< $(KMOD_LDFLAGS)

exploit-poc: exploit.c $(HDR)
	$(CC) $(POC_CFLAGS) -o $@ $< $(POC_LDFLAGS)

# Clean up this directory
.PHONY: clean
clean:
	rm -rf $(BIN) $(LIB) .cache
