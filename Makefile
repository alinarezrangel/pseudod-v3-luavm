LUA=lua5.4
CC=gcc
GCOV=gcov

# Opciones del runtime para GCC. El README contiene una lista.
RTOPTS=-DPDCRT_OPT_GNU=1

# Agrega estas banderas a DEBUGFLAGS si quieres activar el address-sanitizer y
# el UB-sanitizer
SANFLAGS=-fsanitize=address -fsanitize=undefined

# Opciones de depuración para GCC:
DEBUGFLAGS=-g -O0

# Opciones para activar el code-coverage en GCC. Recuerda no activar --coverage
# si tienes optimizaciones activas.
COVERAGEFLAGS=--coverage
# Opciones para GCOV
GCOVFLAGS=--branch-probabilities --display-progress --human-readable

# Opciones de "profiling". Agrega "-g" si quieres medir el rendimiento del
# compilador con gprof.
PROFFLAGS=

# Las banderas usadas para compilar.
CFLAGS=-std=c18 -Wall $(DEBUGFLAGS) $(SANFLAGS) $(PROFFLAGS) -march=native $(RTOPTS) -I./src/
CLIBS=-L. -lpdcrt -lm


.PHONY: all
all: sample libpdcrt.a

.PHONY: gcov
gcov: pdcrt.c.gcov

.PHONY: clean
clean:
	rm -f sample pdcrt.o libpdcrt.a *.gcov *.gcda *.gcno

sample.c: main.lua
	$(LUA) main.lua -W all -Vso sample.c

sample: sample.c libpdcrt.a
	$(CC) $(CFLAGS) sample.c $(CLIBS) -o sample

pdcrt.o: src/pdcrt.c src/pdcrt.h
	$(CC) $(CFLAGS) -c src/pdcrt.c -o pdcrt.o

libpdcrt.a: pdcrt.o
	ar rcs libpdcrt.a pdcrt.o

pdcrt.c.gcov: libpdcrt.a run-tests.sh
	./run-tests.sh
	$(GCOV) $(GCOVFLAGS) pdcrt
