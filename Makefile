CC ?= cc
CFLAGS = -Wall -O2 -std=c99 -DNDEBUG
DEBS = fpc.h
SOURCES = fpc.c cli.c

.PHONY: clean

fpc: $(SOURCES) $(DEBS)
	$(CC) $(CFLAGS) $(SOURCES) -o $@

clean:
	rm -f fpc
