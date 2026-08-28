CC          = gcc
OPT         = -O2
DBG					= -g -Wall -Werror
GEN_CFLAGS	= $(OPT) $(DBG) -fPIC
GEN_LDFLAGS	= -pie
CURL_LIBS		= -lcurl
CRYPTO_LIBS	= -lssl -lcrypto

KMOD_INC			= ../../primitives/kmod
KMOD_LIB			= ../../primitives/kmod
KMOD_CFLAGS		= $(GEN_CFLAGS) -I$(KMOD_INC)
KMOD_LDFLAGS	= -Wl,-rpath,$(KMOD_LIB) -L$(KMOD_LIB) -lprimitive $(GEN_LDFLAGS)

POC_INC			= ../../primitives/poc/inc
POC_LIB			= ../../primitives/poc
POC_CFLAGS	= $(GEN_CFLAGS) -I$(POC_INC)
POC_LDFLAGS	= $(GEN_LDFLAGS) -Wl,-rpath,$(POC_LIB) -L$(POC_LIB) -lprimitive

.PHONY: all
all: rsa-kmod gen-pms-kmod guess-pms-kmod tls-client-kmod tls-server

tls-client-kmod: tls_client.c
	$(CC) $(KMOD_CFLAGS) -o $@ $^ $(KMOD_LDFLAGS) -lgnutls

tls-server: tls_server.c
	$(CC) $(GEN_CFLAGS) -o $@ $^ $(GEN_LDFLAGS) -lgnutls

rsa-kmod: rsa.c
	$(CC) $(KMOD_CFLAGS) -o $@ $^ $(KMOD_LDFLAGS) $(CRYPTO_LIBS)

gen-pms-kmod: gen_pms.c
	$(CC) $(KMOD_CFLAGS) -o $@ $^ $(KMOD_LDFLAGS) -lgnutls

guess-pms-kmod: guess_pms.c
	$(CC) $(KMOD_CFLAGS) -o $@ $^ $(KMOD_LDFLAGS) -lgnutls

rsa-inspect:
	openssl rsa -in private.pem -text -noout
	openssl rsa -in public.pem -pubin -text -noout

.PHONY: clean
clean:
	rm -f tls-kmod rsa-kmod private.pem public.pem tls-client-kmod tls-server gen-pms-kmod guess-pms-kmod
