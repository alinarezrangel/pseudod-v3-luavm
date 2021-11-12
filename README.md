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

## Documentación ##

Nada está documentado por ahora.

[pdc3]: https://github.com/alinarezrangel/pseudod-v3
