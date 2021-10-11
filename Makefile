LUA=lua5.4
CC=gcc
CFLAGS=-std=c18 -Wall -g -O0 -fsanitize=address -fsanitize=undefined
CLIBS=-L. -lpdcrt


.PHONY: all
all: sample libpdcrt.a

.PHONY: clean
clean:
	rm -f sample pdcrt.o libpdcrt.a

sample.c: main.lua
	$(LUA) main.lua -Vso sample.c

sample: sample.c libpdcrt.a
	$(CC) $(CFLAGS) sample.c $(CLIBS) -o sample

pdcrt.o: pdcrt.c pdcrt.h
	$(CC) $(CFLAGS) -c pdcrt.c -o pdcrt.o

libpdcrt.a: pdcrt.o
	ar rcs libpdcrt.a pdcrt.o
