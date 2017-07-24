CFLAGS = -Wall -O2 -std=c99 -DNDEBUG
SOURCES = $(wildcard *.c)

fpc:
	cc $(CFLAGS) $(SOURCES) -o $@

clean:
	rm -f fpc
