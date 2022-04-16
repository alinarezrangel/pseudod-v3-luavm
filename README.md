# Máquina virtual de PseudoD v3 #

El compilador de PseudoD v3 (["pseudod bootstrap"][pdc3]) tiene este backend
experimental llamado "tuplas". Este backend emite código para una máquina
virtual inexistente llamada "pdvm". Este repositorio contiene la implementación
(sin terminar) de dicha máquina virtual.

## Dependencias ##

- [Lua 5.4][lua].
- [GCC][gcc] 9 o superior (en teoría cualquier compilador con soporte de C18
  debería funcionar).
- [Fennel][fennel] (por el módulo `fennel.view`).
- [LPeg][lpeg].

[fennel]: https://fennel-lang.org/
[lua]: https://www.lua.org/
[gcc]: https://gcc.gnu.org/
[lpeg]: http://www.inf.puc-rio.br/~roberto/lpeg/

## Compilar ##

`make` debería crear un programa `sample` en el directorio actual. Este
programa es el compilado del programa embebido en `main.lua`.

La variable del makefile `RTOPTS` contiene algunas opciones del
runtime. Actualmente las únicas existentes son:

- `PDCRT_OPT_GNU` (valor predeterminado: `1`). Si el sistema para el que se
  está compilando es un sistema GNU. Nota que el predeterminado es 1 y puede
  que tengas que cambiarlo para compilar pdcrt en tu sistema operativo.

El makefile también tiene algunas variables opcionales que puedes cambiar para
configurar distintos aspectos de la instalación:

- `LUA`: Ejecutable de Lua 5.4 a usar. De forma predeterminada es `lua5.4`.
- `CC`: Compilador de C a usar. De forma predeterminada es `gcc`.
- `GCOV`: Ejecutable `gcov` a usar. Solo es usado si activas `COVERAGEFLAGS`.
- `AR`: Ejecutable `ar` a usar. Es usado para crear la biblioteca estática
  `libpdcrt.a`.
- `PREFIX`: Contiene la ruta al directorio donde se instalará el runtime. Su
  valor predeterminado es `/usr/local`, pero puedes cambiarlo a cualquier
  directorio con subdirectorios `bin/`, `share/`, `lib/`, etc.
- `INCLUDEDIR`: Directorio en el que se instalarán las cabeceras de C. De forma
  predeterminada es `$PREFIX/include`.
- `LIBDIR`: Directorio en el que se instalarán las bibliotecas compiladas. De
  forma predeterminada es `$PREFIX/lib`.

Todas estas variables pueden ser configuradas sin modificar el makefile, por
ejemplo, para instalar el runtime en `/opt/pdcrt` en vez de `/usr/local` puedes
usar `PREFIX=/opt/pdcrt make install`. Si quieres cambiar el valor de `PREFIX`,
`INCLUDEDIR` o `LIBDIR` tienes que limpiar los archivos compilados con `make
clean`. Por ejemplo, si compilastes el proyecto con `make`
(`PREFIX=/usr/local`) pero ahora quieres usar `PREFIX=/usr` entonces tienes que
ejecutar `make clean && PREFIX=/usr make`.

Hay muchas más variables que puedes configurar en el makefile, te recomiendo
que leas el principio del archivo para más información.

## Ejecutar pruebas ##

El programa `./run-tests.sh` ejecutará todas las pruebas. `./run.sh` es un
script de ayuda para el anterior.

## Extensión de GDB ##

Para facilitar el desarrollo del runtime, hay una pequeña extensión en Python
para [GDB](https://sourceware.org/gdb/onlinedocs/gdb/index.html) en el
directorio `pdcrt_gdb`. Para poder usar esta extensión tu gdb debe haber sido
compilado con soporte para Python 3 (Python 2 no está soportado). Después,
agrega las siguientes líneas a tu archivo `~/.gdbinit`:

```gdb
python
import sys
sys.path.insert(1, 'RUTA_A_ESTE_PROYECTO')
import pdcrt_gdb.pretty_printer as pdcrtpp
pdcrtpp.register_pretty_printers(gdb)
end
```

Pero reemplaza `RUTA_A_ESTE_PROYECTO` por la ruta absoluta al directorio de
este proyecto (**no** al directorio `pdcrt_gdb`).

## Documentación ##

Nada está documentado por ahora.

[pdc3]: https://github.com/alinarezrangel/pseudod-v3
