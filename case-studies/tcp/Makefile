CC          = gcc
OPT         = -O2
DBG					= -g -Wall -Werror
GEN_CFLAGS	= $(OPT) $(DBG) -fPIC
GEN_LDFLAGS	= -pie
PCAP_LIBS		= -lpcap -lpthread

KMOD_INC			= ../../primitives/kmod
KMOD_LIB			= ../../primitives/kmod
KMOD_CFLAGS		= $(GEN_CFLAGS) -I$(KMOD_INC)
KMOD_LDFLAGS	= -Wl,-rpath,$(KMOD_LIB) -L$(KMOD_LIB) -lprimitive $(GEN_LDFLAGS)

POC_INC			= ../../primitives/poc/inc
POC_LIB			= ../../primitives/poc
POC_CFLAGS	= $(GEN_CFLAGS) -I$(POC_INC)
POC_LDFLAGS	= $(GEN_LDFLAGS) -Wl,-rpath,$(POC_LIB) -L$(POC_LIB) -lprimitive

.PHONY: all
all: tcp-client guess-kmod exploit

tcp-client: tcp_client.c
	$(CC) $(GEN_CFLAGS) -o $@ $^ $(GEN_LDFLAGS)

guess-kmod: guess.c jiffies.c siphash.c
	$(CC) $(KMOD_CFLAGS) -o $@ $^ $(KMOD_LDFLAGS)

exploit: exploit.c
	$(CC) $(GEN_CFLAGS) -o $@ $^ $(GEN_LDFLAGS)

.PHONY: clean
clean:
	rm -f tcp-client guess-kmod exploit
