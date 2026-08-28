CC			= gcc
LD			= ld
OPT			= -O2
DBG			= -g
WARN		= -Wall -Werror
CFLAGS	= $(OPT) $(DBG) $(WARN) -fPIC -DVERBOSE
LDFLAGS	= -pie

OBJ	= $(patsubst %.c,%.o,$(wildcard *.c))
BIN	= chacha

.PHONY: all
all: $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $< 

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(OBJ) $(BIN)
