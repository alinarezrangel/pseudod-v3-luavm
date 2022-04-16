# Ejecutables a usar.
LUA ?= lua5.4
CC ?= gcc
GCOV ?= gcov
AR ?= ar

# Rutas para la instalación. Estas variables son usadas para generar archivos
# como `pdcrt.pc`, así que si las cambias asegúrate de ejecutar `make clean`
# para regenerar todo.
PREFIX ?= /usr/local
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib

# Opciones del runtime para GCC. El README contiene una lista de las opciones
# disponibles.
RTOPTS=-DPDCRT_OPT_GNU=1

# Quita estas banderas de CFLAGS si quieres desactivar el address-sanitizer y
# el UB-sanitizer
SANFLAGS=-fsanitize=address -fsanitize=undefined

# Opciones de depuración para GCC:
DEBUGFLAGS=-g

# Opciones para activar el code-coverage en GCC. Recuerda no activar --coverage
# si tienes optimizaciones activas.
COVERAGEFLAGS=-g0 --coverage
# Opciones para GCOV
GCOVFLAGS=--branch-probabilities --display-progress --human-readable

# Opciones de "profiling". Si agregas PROFFLAGS a CFLAGS entonces activarás la
# medición de rendimiento con gprof.
PROFFLAGS=-pg

# Las banderas usadas para compilar:
#
# CFLAGS y CLIBS son exportadas a pdcrt.pc y serán usadas por todos los
# programas que utilicen pdcrt. LOCALCFLAGS y LOCALCLIBS solo serán usadas
# dentro de este makefile. Tienes que tener cuidado porque algunas opciones
# (como -fsanitize=undefined) deben ser activadas en todas las unidades de
# compilación (y por eso deben estar en CFLAGS y no LOCALCFLAGS) mientras que
# otras si pueden ser activadas solo para pdcrt (como -g).
#
# Algunas modificaciones que podrías desear son:
#
# 1. Quitar SANFLAGS para desactivar los sanitizers. Este cambio afecta la ABI
# y tendrás que recompilar todos los programas que usen `libpdcrt.a`.
#
# 2. Agregar PROFFLAGS para activar la medición de rendimiento con gprof.
#
# 3. Agregar COVERAGEFLAGS para medir el code-coverage con gcov.
#
# 4. Quitar `-march=native` para obtener binarios más portables pero menos
# eficientes.
CFLAGS=-std=c18 -Wall $(SANFLAGS) -march=native $(RTOPTS) -I$(INCLUDEDIR)
CLIBS=-L$(LIBDIR) -lpdcrt -lm
# Las opciones de LOCALCFLAGS y LOCALCLIBS tienen prioridad por sobre CFLAGS y
# CLIBS.
LOCALCFLAGS=-I./src/ $(DEBUGFLAGS)
LOCALCLIBS=-L.


.PHONY: all
all: sample libpdcrt.a

.PHONY: gcov
gcov: pdcrt.c.gcov

.PHONY: clean
clean:
	rm -f sample pdcrt.o libpdcrt.a *.gcov *.gcda *.gcno pdcrt.pc

sample.c: main.lua
	$(LUA) main.lua -W all -Vso sample.c

sample: sample.c libpdcrt.a
	$(CC) $(CFLAGS) $(LOCALCFLAGS) $< $(LOCALCLIBS) $(CLIBS) -o $@

pdcrt.o: src/pdcrt.c src/pdcrt.h
	$(CC) $(CFLAGS) $(LOCALCFLAGS) -c $< -o $@

libpdcrt.a: pdcrt.o
	$(AR) rcs $@ $^

pdcrt.pc: pdcrt.pc.in main.lua lua-escape-table.lua pkg-config-configure.lua
	$(LUA) pkg-config-configure.lua \
		-o $@ \
		-i $< \
		--prefix $(PREFIX) \
		--includedir $(INCLUDEDIR) \
		--libdir $(LIBDIR) \
		--cflags "$$($(LUA) lua-escape-table.lua $(CFLAGS))" \
		--clibs "$$($(LUA) lua-escape-table.lua $(CLIBS))" \
		--version "$$($(LUA) main.lua --version)"

pdcrt.c.gcov: libpdcrt.a run-tests.sh main.lua
	./run-tests.sh
	$(GCOV) $(GCOVFLAGS) pdcrt
