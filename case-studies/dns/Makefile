CC          = gcc
OPT         = -O2
DBG					= -g -Wall -Werror
GEN_CFLAGS	= $(OPT) $(DBG) -fPIC
GEN_LDFLAGS	= -pie
PCAP_LIBS		= -lpcap -lpthread
SD_LIBS			= $(shell pkg-config --cflags --libs libsystemd)
RESOLV_LIBS	= -lresolv

KMOD_INC			= ../../primitives/kmod
KMOD_LIB			= ../../primitives/kmod
KMOD_CFLAGS		= $(GEN_CFLAGS) -I$(KMOD_INC)
KMOD_LDFLAGS	= -Wl,-rpath,$(KMOD_LIB) -L$(KMOD_LIB) -lprimitive $(GEN_LDFLAGS)

POC_INC			= ../../primitives/poc/inc
POC_LIB			= ../../primitives/poc
POC_CFLAGS	= $(GEN_CFLAGS) -I$(POC_INC)
POC_LDFLAGS	= $(GEN_LDFLAGS) -Wl,-rpath,$(POC_LIB) -L$(POC_LIB) -lprimitive

BIN = dns-query-kmod dns-poison

.PHONY: all
all: $(BIN) 

dns-query-kmod: dns_query.c
	#$(CC) $(KMOD_CFLAGS) -DUSE_PCAP -o $@ $^ $(PCAP_LIBS) $(SD_LIBS) $(KMOD_LDFLAGS)
	$(CC) $(KMOD_CFLAGS) -o $@ $^ $(SD_LIBS) $(KMOD_LDFLAGS)

dns-poison: dns_poison.c
	$(CC) $(GEN_CFLAGS) -o $@ $^ $(PCAP_LIBS) $(RESOLV_LIBS) $(GEN_LDFLAGS)

.PHONY: clean
clean:
	rm -f $(BIN)
