all:
	cc -Wall -O2 -std=c99 -DNDEBUG cli.c fpc.c -o fpc
