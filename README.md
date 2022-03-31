# Máquina virtual de PseudoD v3 #

El compilador de PseudoD v3 (["pseudod bootstrap"][pdc3]) tiene este backend
experimental llamado "tuplas". Este backend compila código para una máquina
virtual "no existente" llamada "pdvm". Este repositorio contiene la
implementación (sin terminar) de dicha máquina virtual.

## Dependencias ##

- [Lua 5.4][lua].
- [GCC][gcc] 9 o superior (en teoría cualquier compilador con soporte de C18
  debería funcionar).
- [Fennel][fennel] (por el módulo `fennelview`).
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
