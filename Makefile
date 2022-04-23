PREFIX?=/usr/local
CFLAGS = -Wall -O2 -std=c99 -DNDEBUG
DEBS = fpc.h
SOURCES = fpc.c cli.c

.PHONY: clean

fpc: $(SOURCES) $(DEBS)
	$(CC) $(CFLAGS) $(SOURCES) -o $@

install:
	install -d $(PREFIX)/bin
	install fpc $(PREFIX)/bin

uninstall:
	rm $(PREFIX)/bin/fpc

clean:
	rm -f fpc
