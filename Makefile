LUA=lua5.4
CC=gcc
RTOPTS=-DPDCRT_OPT_GNU=1
CFLAGS=-std=c18 -Wall -g -O0 -fsanitize=address -fsanitize=undefined -march=native $(RTOPTS)
CLIBS=-L. -lpdcrt


.PHONY: all
all: sample libpdcrt.a

.PHONY: clean
clean:
	rm -f sample pdcrt.o libpdcrt.a

sample.c: main.lua
	$(LUA) main.lua -W all -Vso sample.c

sample: sample.c libpdcrt.a
	$(CC) $(CFLAGS) sample.c $(CLIBS) -o sample

pdcrt.o: pdcrt.c pdcrt.h
	$(CC) $(CFLAGS) -c pdcrt.c -o pdcrt.o

libpdcrt.a: pdcrt.o
	ar rcs libpdcrt.a pdcrt.o
