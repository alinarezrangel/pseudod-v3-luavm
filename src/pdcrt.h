#ifndef PDCRT_H
#define PDCRT_H

// El runtime en C de PseudoD.
//
// Consiste de:
//
// 1. Un sistema de alojadores ("allocators") que te permite modificar que
// versiones de `malloc`/`realloc`/`free` se usarán.
//
// 2. El sistema de objetos mediante el cual funciona todo PseudoD.
//
// 3. (Como parte del #2) Una pequeña biblioteca de manipulación de
// strings-no-terminados-en-0.
//
// 4. La implementaciones de *closures*.
//
// 5. Las estructuras de datos principales de la máquina
// virtual. Específicamente: la pila de valores, el "constant pool" ("piscina
// de constantes", aunque prefiero el término "lista de constantes"), el
// "contexto" y los marcos de activación de las funciones.
//
// 6. Macros y funciones necesarias para compilar el código producido por el
// ensamblador.
//
// Algunos detalles adicionales:
//
// 1. A menos que se indique lo contrario, todos los textos, tanto de C como de
// PseudoD, están en UTF-8.
//
// 2. A menos que se indique lo contrario (con `PDCRT_NULL`), ningún puntero
// puede ser `NULL`.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>


// Las siguientes macros son usadas como "marcadores" de algunas variables:

// Marca un puntero que puede ser nulo
#define PDCRT_NULL

// Marca un parámetro como "de salida".
#define PDCRT_OUT

// Marca un parámetro como "de entrada".
#define PDCRT_IN

// Indica que un arreglo o puntero es realmente un arreglo de tamaño
// dinámico. `campo_tam` es una variable o campo que contiene el tamaño del
// arreglo.
#define PDCRT_ARR(campo_tam)


// Macros de depuración.
//
// Las siguientes macros activan distintos sistemas de depuración del
// runtime. Específicamente:
//
// `PDCRT_DBG_RASTREAR_MARCOS`: Escribe a `stdout` cada vez que se cree o
// destruya un marco. De esta forma podrás rastrear que funciones se están
// ejecutando. Recuerda que los "marcos" que corresponden a C no tienen
// `pdcrt_marco`s por ahora.
//
// `PDCRT_DBG_RASTREAR_CONTEXTO`: Escribe a `stdout` el estado del contexto del
// runtime de vez en cuando.
//
// `PDCRT_DBG_ESTADISTICAS_DE_LOS_ALOJADORES`: Al destruir un alojador, escribe
// estadísticas útiles del mismo.
//
// `PDCRT_DBG_ESCRIBIR_ERRORES`: Cuando una función "importante" devuelva un
// `pdcrt_error` que no sea `PDCRT_OK`, escribe la función, el error y la
// operación a `stdout` (pero no termina el programa).
//
// `PDCRT_DBG`: Activa todas las opciones anteriores.

#ifdef PDCRT_DBG
#define PDCRT_DBG_RASTREAR_MARCOS
#define PDCRT_DBG_ESTADISTICAS_DE_LOS_ALOJADORES
#define PDCRT_DBG_ESCRIBIR_ERRORES
#define PDCRT_DBG_RASTREAR_CONTEXTO
#endif

// Las macros PRB (de "prueba").
//
// Similares a las macros de depuración, estas macros habilitan capacidades del
// runtime que solo son útiles a la hora de realizar pruebas en
// este. Actualmente las macros de prueba son:
//
// `PDCRT_PRB_ALOJADOR_INESTABLE` (debe ser un `int`): Si está definido
// entonces el alojador de arena del runtime fallará con una probabilidad de 1
// sobre `PDCRT_PRB_ALOJADOR_INESTABLE`. Por ejemplo, si
// `PDCRT_PRB_ALOJADOR_INESTABLE` es 2 (el valor predeterminado) entonces el
// alojador de área fallará con una probabilidad de 1/2 = 50%. Utiliza `rand`
// para obtener los números aleatorios.
//
// `PDCRT_PRB_SRAND`: Un número (`unsigned int`) que será la semilla pasada a
// `srand` al crear un alojador de arena. Esta macro existe para que puedas
// reproducir bugs o fallas encontradas con
// `PDCRT_PRB_ALOJADOR_INESTABLE`. Tiene efecto incluso si
// `PDCRT_PRB_ALOJADOR_INESTABLE` no está definida.
//
// `PDCRT_PRB`: Activa todas las macros anteriores excepto `PDCRT_PRB_SRAND`.

#ifdef PDCRT_PRB
#define PDCRT_PRB_ALOJADOR_INESTABLE 2
#endif


// Los dos códigos de salida usados por el runtime.
//
// `PDCRT_SALIDA_ERROR` es el código que se usará para indicar que sucedió un
// error, mientras que `PDCRT_SALIDA_EXITO` será el código de salida exitoso.
#define PDCRT_SALIDA_ERROR 2
#define PDCRT_SALIDA_EXITO 0


// Códigos de error.
//
// Todos los códigos de error están aquí. El valor especial `PDCRT_OK` indica
// que no hubo error alguno. A menos que se indique lo contrario, ninguna de
// las funciones de pdcrt utiliza `errno`.
typedef enum pdcrt_error
{
    PDCRT_OK = 0,
    PDCRT_ENOMEM = 1,
    PDCRT_WPARTIALMEM = 2,
} pdcrt_error;

// Devuelve una representación textual de un código de error. El puntero
// devuelto apunta a un bloque de memoria estático, así que no es necesario
// desalojarlo con `free` y es válido por toda la duración del programa.
const char* pdcrt_perror(pdcrt_error err);


// Alojador.
//
// El diseño del alojador esta inspirado fuertemente en el de PUC-Lua 5.4. Te
// recomiendo que leas el manual de dicho en
// <https://www.lua.org/manual/5.4/manual.html> (especialmente el tipo
// `lua_Alloc`).
//
// Básicamente, un `pdcrt_func_alojar` se podrá llamar de las siguientes
// formas:
//
// 1. `alojador(DT, NULL, 0, tam)`: Aloja y devuelve `tam` bytes (tal como
// `malloc(tam)`). Devuelve `NULL` si no se pudo alojar la memoria.
//
// 2. `alojador(DT, ptr, tam, tam + n)`: Realoja el área indicada por `ptr`,
// que tiene actualmente `tam` bytes y deberá tener `tam + n` (para un `n`
// tanto positivo como negativo) bytes. Esto funciona como un `realloc` que
// incrementa el tamaño de `ptr`. Tal como `realloc`, el contenido actual de
// `ptr` debe ser copiado a su nueva dirección. Devuelve `NULL` si no se pudo
// alojar la memoria, en cuyo caso se asume que `ptr` aún es válido.
//
// 3. `alojador(DT, ptr, tam, 0)`: Desaloja `ptr`. Equivalente a
// `free(ptr)`. Su valor de retorno es ignorado.
//
// Este sistema permite representar toda la API `malloc`/`realloc`/`free` como
// una única función. Nota como a diferencia de estas funciones los usuarios de
// `pdcrt_func_alojar` mantienen en todo momento registro del tamaño actual del
// área, lo que te permite ahorrar espacio en metadatos.
//
// También notarás como todas las llamadas a `alojador` tienen este argumento
// `DT`. `DT` es un `void*` provisto por ti mismo para pasar datos adicionales
// a tus alojadores.
//
// Un detalle que debes tener en cuenta con esta API es que no es posible
// alojar/dealojar un bloque de tamaño 0, específicamente tratar de alojar un
// bloque de tamaño 0 cuenta como *comportamiento indefinido*. En todas las
// llamadas a un alojador tendrás que recordar esto. (Esto es diferente de como
// `malloc` funciona: `malloc` explícitamente indica que puede devolver `NULL`
// o un puntero válido para `free`.)
//
// Nota: En vez de llamar manualmente a `pdcrt_func_alojar`, deberías usar las
// funciones `pdcrt_alojar_simple`, `pdcrt_dealojar_simple` y
// `pdcrt_realojar_simple`.
//
// P.S. Además de mantener el tamaño actual de todas las áreas alojadas, pdcrt
// debería también mantener un sistema de run-time-type-information (RTTI) para
// permitir al proveedor de memoria optimizar ciertos patrones. Actualmente
// estoy trabajando en diseñar dicha extensión.
typedef PDCRT_NULL void* (*pdcrt_func_alojar)(void* datos_del_usuario, PDCRT_IN PDCRT_NULL void* ptr, size_t tam_viejo, size_t tam_nuevo);

// El verdadero núcleo del sistema de memoria de pdcrt.
//
// `alojar` es el alojador a usar, mientras que `datos` son los datos que se le
// pasarán como primer argumento. Nota que si así lo deseas, puedes simplemente
// pasar `NULL` como `datos`.
typedef struct pdcrt_alojador
{
    pdcrt_func_alojar alojar;
    void* datos;
} pdcrt_alojador;

// Un alojador simple que utiliza `malloc`, `realloc` y `free`.
pdcrt_alojador pdcrt_alojador_de_malloc(void);

// Un alojador de arena. A medida que se aloja memoria en el, este solo la
// acumula hasta que al final, cuando se llame a
// `pdcrt_dealoj_alojador_de_arena` toda la memoria que fue alojada será
// desalojada.
//
// Nota: Técnicamente hablando esto no es un alojador de arena sino un alojador
// grupal.
pdcrt_error pdcrt_aloj_alojador_de_arena(pdcrt_alojador* aloj);
void pdcrt_dealoj_alojador_de_arena(pdcrt_alojador aloj);

// Como ya se mencionó, estas funciones son "ayudantes" para alojar, realojar y
// desalojar memoria con un alojador dado.
PDCRT_NULL void* pdcrt_alojar_simple(pdcrt_alojador alojador, size_t tam);
void pdcrt_dealojar_simple(pdcrt_alojador alojador, void* ptr, size_t tam);
PDCRT_NULL void* pdcrt_realojar_simple(pdcrt_alojador alojador, PDCRT_NULL void* ptr, size_t tam_actual, size_t tam_nuevo);


struct pdcrt_contexto;
struct pdcrt_objeto;
struct pdcrt_marco;
struct pdcrt_env;


// Un puntero a función.
//
// El estándar de C indica que los punteros a void y los punteros a función no
// son inter-convertibles. Esto es debido a que C explícitamente soporta
// arquitecturas en las que el *data-space* y el *code-space* son de distintos
// tamaños (por ejemplo, algunos sistemas embebidos como los Arduino o los
// PICs). POSIX prácticamente garantiza que `void*` y los punteros a función
// son inter-convertibles pero ya que el estándar no lo indica, prefiero evitar
// problemas con el optimizador.
//
// Para esto, utilizaré el tipo `pdcrt_funcion_generica` para representar un
// puntero a función genérico, mientras que `void*` significará un puntero a
// datos genérico.
typedef void (*pdcrt_funcion_generica)(void);

// Una "closure" (clausura).
//
// Las closures están implementadas de la forma más simple posible:
// literalmente un "código" (el `pdcrt_proc_t`) y cero o más datos (contenidos
// dentro del `pdcrt_env*`).
//
// El comentario de `pdcrt_objeto` tiene más información, pero es muy
// importante que `env` sea un puntero ya que los valores del entorno son
// mutables.
//
// Las closures no "poseen" su `env`: un mismo `env` puede ser compartido por
// varias `pdcrt_closure`s.
typedef struct pdcrt_closure
{
    pdcrt_funcion_generica proc; // tipo real: pdcrt_proc_t
    struct pdcrt_env* env;
} pdcrt_closure;

typedef struct pdcrt_impl_obj
{
    void* recv;
    struct pdcrt_env* attrs;
} pdcrt_impl_obj;

// Un texto.
//
// Contiene cero o más carácteres codificados con UTF-8 en `contenido`. Estos
// **no están terminados por el byte nulo**, así que no es posible pasar
// directamente `contenido` a las funciones de `string.h`.
//
// También nota que `contenido` no tiene `const` en ninguna parte: esto es para
// simplificar la inicialización de los textos. Sin embargo, tu siempre
// deberías tratarlo como si estuviese declarado con `PDCRT_NULL
// PDCRT_ARR(longitud) const char* const contenido;`.
//
// Como caso especial, un texto vacío puede tener `NULL` como `contenido`.
//
// Los textos "poseen" su contenido. Este es alojado y desalojado junto al
// texto.
typedef struct pdcrt_texto
{
    PDCRT_NULL PDCRT_ARR(longitud) char* contenido;
    size_t longitud;
} pdcrt_texto;

// Aloja un texto con un contenido indeterminado pero de tamaño `lon`.
pdcrt_error pdcrt_aloj_texto(PDCRT_OUT pdcrt_texto** texto, pdcrt_alojador alojador, size_t lon);
// Aloja un texto con el mismo contenido y tamaño que el C-string `cstr` (que
// debe terminar con el byte nulo). El texto alojado no tendrá el byte nulo.
pdcrt_error pdcrt_aloj_texto_desde_c(PDCRT_OUT pdcrt_texto** texto, pdcrt_alojador alojador, const char* cstr);
// Desaloja un texto.
void pdcrt_dealoj_texto(pdcrt_alojador alojador, pdcrt_texto* texto);

// El tipo `pdcrt_objeto`.
//
// `pdcrt_objeto` es el tipo que el runtime utiliza para manipular y
// representar los objetos de PseudoD. Este tipo tiene algunas peculiaridades
// así que es importante explicar en detalle como funciona antes de introducir
// su definición.
//
// `pdcrt_objeto` implementa un patrón conocido como "handler". Es decir,
// aunque `pdcrt_objeto` siempre se manejará "by-value" en la pila de C,
// `pdcrt_objeto` realmente representa un puntero. Imagínalo como si
// `pdcrt_objeto` fuese un typedef para `void*`: tu igual dices `pdcrt_objeto
// a, b, c;` pero `a`, `b` y `c` son "referencias", no valores.
//
// Por motivos de eficiencia, `pdcrt_objeto` tiene varios campos que sí están
// "by-value" dentro de este. Tu puedes agregar cualquier cantidad de campos
// "by-value" a `pdcrt_objeto` con la única condición de que estos deben ser
// inmutables: ya que `pdcrt_objeto` es una referencia al objeto real,
// agregarle un campo a este que no sea un puntero es equivalente a si en vez
// de pasar un puntero a `T` pasases el `T` mismo. El código será equivalente
// si y solo si nunca modificas `T` ya que las modificaciones a punteros son
// visibles para todos los demás que posean también un puntero al mismo objeto,
// mientras que modificar un valor en la pila solo te permitirá ver los cambios
// a ti.
//
// Por esto es que `pdcrt_objeto` contiene campos para un entero y un double:
// en PseudoD los números son inmutables así que no importa si están a través
// de un puntero o no. Ahora, podrás preguntarte porqué `pdcrt_closure`, que es
// mutable, no está a través de un puntero. La respuesta es que la única parte
// mutable de `pdcrt_closure` es su `pdcrt_env`. Y de hecho, `pdcrt_env` está a
// través de un puntero en `pdcrt_closure`.
//
// Otro motivo por el cual los valores en `pdcrt_objeto` están a través de
// punteros es para "desduplicar" los datos: `pdcrt_texto` es inmutable pero la
// lista de constantes textuales es compartida, si el programa crea 10
// referencias al texto `"hola"` entonces estas son 10 referencias al mismo
// `pdcrt_texto`.
//
// Como consecuencia de todo esto, ten mucho cuidado si en algún momento creas
// un puntero a `pdcrt_objeto`: esto es casi siempre erróneo.
//
// # Recibir mensajes #
//
// Los objetos pueden recibir mensajes. Notarás que a diferencia de otras
// implementaciones de lenguajes orientados a objetos, no hay una función
// "enviar_mensaje(obj, msj, ...)". Gracias a que PseudoD es un compilador y no
// un intérprete, podemos compilar cada "receptor de mensajes" a su propia
// función y al llamarlos como "obj.receptor(obj, msj, ...)" la cache de saltos
// del procesador actuará como la "monomorfización de instrucciones" de una VM
// tradicional.
//
// Para mantener la cache de saltos "limpia", hay que evitar:
//
// 1. Envío centralizado de mensajes: siempre escribe `obj.recv(...)`, no crees
// una función que lo haga por ti. La macro `PDCRT_ENVIAR_MENSAJE` automatiza
// parte de esto pero el hecho de que es una macro y no una función es muy
// importante.
//
// 2. Funciones centralizadas para múltiples tipos: si tienes una función
// `hacer_x_en_textos(pdcrt_objeto x)` y una `hacer_y_en_numeros(pdcrt_objeto
// y)` la cache podrá especializar los saltos a `obj.recv` en estos en base al
// tipo en tiempo de ejecución. Si en cambio tienes una única
// `hacer_x_o_y(pdcrt_objeto x)` entonces la cache tendrá que ser compartida
// para ambos tipos.
//
// Finalmente, todo este sistema de abusar la cache de saltos como un sistema
// de monomorfización de opcodes es **teórico**. No he medido que tanto afecta
// al código (¡Quizás afecta a los programas de forma negativa!). **Creo** que
// va a funcionar en base a lo (poco) que sé de los CPUs modernos y al hecho de
// que es básicamente una versión ligeramente más dinámica de un vtable (y sé
// que los vtables si tienen las ventajas que he mencionado). Sin embargo, no
// puedo asegurar que esto funciona hasta que no mida el rendimiento con y sin
// este sistema de forma exhaustiva.
//
// Finalmente, debido a que el tipo de la función a la que `recv` apunta es
// recursivo y C no permite esto, `recv` está declarado como un puntero a
// `pdcrt_funcion_generica`, sin embargo, siempre debes convertirlo a un
// puntero a `pdcrt_recvmsj` antes de usarlo. La macro `PDCRT_CONV_RECV` sirve
// para esto.
typedef struct pdcrt_objeto
{
    enum pdcrt_tipo_de_objeto
    {
        PDCRT_TOBJ_ENTERO = 0,
        PDCRT_TOBJ_FLOAT = 1,
        PDCRT_TOBJ_MARCA_DE_PILA = 2,
        PDCRT_TOBJ_CLOSURE = 3,
        PDCRT_TOBJ_TEXTO = 4,
        PDCRT_TOBJ_OBJETO = 5,
        PDCRT_TOBJ_BOOLEANO = 6,
        PDCRT_TOBJ_NULO = 7
    } tag;
    union
    {
        int i;
        float f;
        pdcrt_closure c;
        pdcrt_impl_obj o;
        pdcrt_texto* t;
        bool b;
    } value;
    pdcrt_funcion_generica recv;
} pdcrt_objeto;

typedef enum pdcrt_tipo_de_objeto pdcrt_tipo_de_objeto;


// Una continuación.
//
// El runtime de PseudoD está implementado con continuaciones. Actualmente
// utiliza un sistema "stack-less", pero en un futuro quiero hacer que use un
// recolector de basura "Chenney on the MTA".
//
// A diferencia de un sistema en CPS ("continuation-passing style") real, el
// runtime actualmente solo implementa uno "stack-less". La diferencia es que
// aquí las funciones no toman una continuación a la cual pasar el valor de
// retorno, en cambio, un trampolín mantiene manualmente un stack en
// memoria. Las continuaciones solo exísten "localmente" dentro de las
// funciones. En un futuro cambiaré todo para que utilice CPS.
//
// Existen 4 "tipos" de continuaciones:
//
// 1. `PDCRT_CONT_INICIAR`: Llama a una función.
//
// 2. `PDCRT_CONT_CONTINUAR`: Continúa la función actual.
//
// 3. `PDCRT_CONT_DEVOLVER`: Devuelve de la función actual a la continuación de
// la función que la llamó.
//
// 4. `PDCRT_CONT_ENVIAR_MENSAJE`: Como `PDCRT_CONT_INICIAR`, pero en vez de
// llamar a una función, le envía un mensaje a un objeto.
//
// 5. `PDCRT_CONT_TAIL_INICIAR`: Como `PDCRT_CONT_INICIAR`, pero asume que la
// función actual destruyó su marco de activación (porque ya no lo necesita) y
// llama al procedimiento indicado reemplazando al actual. Básicamente,
// implementa un "tail-call".
//
// 6. `PDCRT_CONT_TAIL_ENVIAR_MENSAJE`: Como `PDCRT_CONT_ENVIAR_MENSAJE`, pero
// envía el mensaje como un tail-call (un "tail-enviar mensaje", por así
// decirlo). Tal como `PDCRT_CONT_TAIL_INICIAR` asume que la función que esta
// enviando el mensaje destruirá su marco.
//
// Nota como llamar a una función y enviarle un mensaje a un objeto son
// operaciones distíntas: en el runtime las funciones de PseudoD siempre son
// representadas como objetos, pero a veces el runtime necesita crear funciones
// internas y para esto utiliza INICIAR.
typedef struct pdcrt_continuacion
{
    enum pdcrt_tipo_de_continuacion
    {
        PDCRT_CONT_INICIAR = 0,
        PDCRT_CONT_CONTINUAR = 1,
        PDCRT_CONT_DEVOLVER = 2,
        PDCRT_CONT_ENVIAR_MENSAJE = 3,
        PDCRT_CONT_TAIL_INICIAR = 4,
        PDCRT_CONT_TAIL_ENVIAR_MENSAJE = 5
    } tipo;

    union
    {
        // Datos para iniciar una continuación.
        //
        // - `proc` es la función que se llamará. Será llamada con un nuevo
        //   marco y el marco superior. El marco pasado no estará inicializado:
        //   esta función tiene que inicializarlo lo antes posible con el marco
        //   superior pasado.
        //
        // - `cont` es la continuación de la función que esta llamando a
        //   `proc`.
        //
        // - `marco_superior` es el marco actual de la función que esta
        //   llamando a `proc`. Es decir, `marco_superior` es el marco superior
        //   de `proc` pero el marco actual de `cont`.
        //
        // - `args` y `rets` son los números de argumentos y valores devueltos
        //   por `proc`.
        struct
        {
            pdcrt_funcion_generica proc; // tipo real: pdcrt_proc_t
            pdcrt_funcion_generica cont; // tipo real: pdcrt_proc_continuacion
            struct pdcrt_marco* marco_superior;
            int args;
            int rets;
        } iniciar;

        // Datos para continuar la función actual.
        //
        // - `proc` es la función que será llamada cuando se continúe con esta
        //   función.
        //
        // - `marco_actual` es el marco actual que será conservado a travez de
        //   la continuación.
        struct
        {
            pdcrt_funcion_generica proc; // tipo real: pdcrt_proc_continuacion
            struct pdcrt_marco* marco_actual;
        } continuar;

        // - `recv` es la continuación que recibirá el resultado de enviar el
        //   mensaje.
        //
        // - `marco` es el marco actual de `recv`. Será usado como marco
        //   superior para el objeto.
        //
        // - `yo` es el objeto que recibirá el mensaje.
        //
        // - `mensaje` es el mensaje que será enviado.
        //
        // - `args` y `rets` tienen el mismo significado que en `iniciar`.
        struct
        {
            pdcrt_funcion_generica recv; // tipo real: pdcrt_proc_continuacion
            struct pdcrt_marco* marco;
            struct pdcrt_objeto yo;
            struct pdcrt_objeto mensaje;
            int args;
            int rets;
        } enviar_mensaje;

        // Como iniciar, pero no requiere una continuación al ser un tail-call.
        //
        // `marco_superior` debe ser el marco superior de la función
        // actual. Antes de usar una continuación tail-iniciar, debes
        // desinicializar tu marco actual y utilizar tu marco superior como
        // `marco_superior`. El trampolín no hace esto automáticamente y no
        // desinicializar el marco antes de usar una continuación "tail" dejará
        // memoria sin desalojar.
        struct
        {
            pdcrt_funcion_generica proc; // tipo real: pdcrt_proc_t
            struct pdcrt_marco* marco_superior;
            int args;
            int rets;
        } tail_iniciar;

        // Tail-call como tail-iniciar, pero envía un mensaje en vez de llamar
        // a una función.
        struct
        {
            struct pdcrt_marco* marco_superior;
            struct pdcrt_objeto yo;
            struct pdcrt_objeto mensaje;
            int args;
            int rets;
        } tail_enviar_mensaje;
    } valor;
} pdcrt_continuacion;

// Tipo de las funciones que pueden ser usadas como continuaciones.
//
// `marco` es el marco de la función que comenzó.
typedef pdcrt_continuacion (*pdcrt_proc_continuacion)(struct pdcrt_marco* marco);

// Crea y devuelve una continuación. Corresponde al tipo de continuación
// `PDCRT_CONT_CONTINUAR`.
pdcrt_continuacion pdcrt_continuacion_normal(pdcrt_proc_continuacion proc, struct pdcrt_marco* marco);
// Crea y devuelve una continuación para devolver.
pdcrt_continuacion pdcrt_continuacion_devolver(void);
// Crea y devuelve una continuación para enviar un mensaje. Corresponde al tipo
// `PDCRT_CONT_ENVIAR_MENSAJE`.
pdcrt_continuacion pdcrt_continuacion_enviar_mensaje(
    pdcrt_proc_continuacion proc,
    struct pdcrt_marco* marco,
    pdcrt_objeto yo,
    pdcrt_objeto mensaje,
    int args,
    int rets
);

// El trampolín: ejecuta las continuaciones en una pila en memoria.
//
// `k` es la continuación a ejecutar, mientras que `marco` es el marco que esta
// tendrá. Devuelve luego de ejecutar `k` y cualquier continuación que esta
// cree. Esta función no termina con la ejecución del programa (con `abort` o
// `exit`) y puede llamarse varias veces (si deseas ejecutar varias
// continuaciones).
//
// `marco` debe apuntar a un marco válido.
void pdcrt_trampolin(struct pdcrt_marco* marco, pdcrt_continuacion k);

// Un procedimiento que se puede llamar desde PseudoD.
//
// Estos procedimientos serán llamados como partes de una "closure". La
// explicación de estas está más abajo.
//
// `marco` es el marco de la función. Al principio no está inicializado (apunta
// a un marco inválido): la función tiene que inicializarlo lo antes
// posible. `marco_superior` es el marco de la función que está llamando a esta
// y siempre está inicializado.
//
// `args` y `rets` indica el número de valores que esta función puede
// sacar/debe empujar en la pila. Nota que el número es exacto: si `args` es 3
// y `rets` es 1 entonces la función **debe** sacar 3 valores al comienzo y
// empujar 1 al final.
typedef pdcrt_continuacion (*pdcrt_proc_t)(
    struct pdcrt_marco* marco,
    struct pdcrt_marco* marco_superior,
    int args,
    int rets
);

// Crea y devuelve una continuación que llama a una función.
pdcrt_continuacion pdcrt_continuacion_iniciar(
    pdcrt_proc_t proc,
    pdcrt_proc_continuacion cont,
    struct pdcrt_marco* marco_sup,
    int args,
    int rets
);

// Crea y devuelve una continuación tail-iniciar.
pdcrt_continuacion pdcrt_continuacion_tail_iniciar(
    pdcrt_proc_t proc,
    struct pdcrt_marco* marco_superior,
    int args,
    int rets
);
// Crea y devuelve una continuación tail-enviar mensaje.
pdcrt_continuacion pdcrt_continuacion_tail_enviar_mensaje(
    struct pdcrt_marco* marco_superior,
    pdcrt_objeto yo,
    pdcrt_objeto mensaje,
    int args,
    int rets
);


// Tipo de las funciones que sirven para recibir mensajes.
//
// Algunas notas:
//
// 1. Nota como `msj`, el mensaje recibido, es un objeto y no un texto.
//
// 2. Tal como las funciones de las closures, estos receptores toman dos
// parámetros `args` y `rets` indicando el número de argumentos a recibir (en
// la pila) y el número de valores a devolver (también en la pila). El código
// que llama a esta función asumirá que esta siempre consumirá `args` valores y
// devolverá `rets`.
//
// 3. El valor de retorno aún no se está usando, pero en un futuro significará
// lo mismo que en `pdcrt_proc_t`.
typedef pdcrt_continuacion (*pdcrt_recvmsj)(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets);

// Convierte un puntero a una función genérica `pdcrt_funcion_generica` a un
// puntero a una función que recibe mensajes `pdcrt_recvmsj`. La implementación
// de esta macro es pública y puedes usarla libremente en tus programas.
#define PDCRT_CONV_RECV(recv_gen) ((pdcrt_recvmsj) (recv_gen))

// Envía un mensaje a un objeto (macro de conveniencia).
//
// La implementación de esta macro es pública y puedes usarla libremente en tus
// programas.
//
// Advertencia: esta macro expande `yo` varias veces.
//
// `marco`, `msj`, `args` y `rets` deben tener los mismos tipos que sus
// respectivos parámetros en `pdcrt_recvmsj`. `yo` debe ser un `pdcrt_objeto`.
#define PDCRT_ENVIAR_MENSAJE(marco, yo, msj, args, rets)    \
    ((*PDCRT_CONV_RECV((yo).recv))((marco), (yo), (msj), (args), (rets)))

// Locales especiales.
//
// Algunas variables locales de PseudoD son especiales porque se definen en el
// bytecode como que deben ser distintas de todas las locales numéricas. Para
// esto, actualmente estoy usando números negativos. Ya que no es posible en C
// crear un arreglo con índices de -2 a N, el "offset" `PDCRT_NUM_LOCALES_ESP`
// es agregado a todos los accesos tanto de los entornos (`pdcrt_env`) como de
// las variables locales (`pdcrt_marco`).
//
// Como `PDCRT_ID_NIL` no es una variable local, no es tomada en cuenta en
// `PDCRT_NUM_LOCALES_ESP`.
//
// Las variantes `PDCRT_NAME_*` son los "nombres" de estas variables locales en
// C. Las macros `PDCRT_NAME_*` solo deben usarse por el compilador, no por los
// usuarios de pdcrt.
//
// Los valores de las macros `PDCRT_ID_*` son públicos: están sujetos a la
// política de estabilidad del proyecto y puedes usarlos directamente en tus
// programas (aunque te recomiendo usar las macros para mayor legibilidad). Lo
// mismo con la macro `PDCRT_NUM_LOCALES_ESP`.
#define PDCRT_ID_EACT -1
#define PDCRT_ID_ESUP -2
#define PDCRT_ID_NIL -3
#define PDCRT_NAME_EACT pdcrt_special_eact
#define PDCRT_NAME_ESUP pdcrt_special_esup
#define PDCRT_NAME_NIL pdcrt_special_nil
// NOTA: Si modificas este valor, también actualiza la variable del mismo
// nombre en `pdcrt_gdb/pretty_printer.py`
#define PDCRT_NUM_LOCALES_ESP 2

// El entorno de una "closure".
//
// `env` contiene los objetos capturados. Para guardarle espacio a las
// variables especiales (véase las macros `PDCRT_ID_EACT` y `PDCRT_ID_ESUP`),
// `env` y `env_size` realmente tienen un tamaño de `tam_del_entorno +
// PDCRT_NUM_LOCALES_ESP`. Es importante que si vas a acceder directamente a
// `env`, tengas este "offset" en cuenta.
//
// Nota: Podría parecer ineficiente que solo mantenemos tamaño+contenido, en
// vez de capacidad+tamaño+contenido. Sin embargo, los entornos nunca cambian
// de tamaño después de creados así que la garantía de agregar elementos en
// O(1) de forma asintótica no importa.
typedef struct pdcrt_env
{
    size_t env_size;
    PDCRT_ARR(env_size + PDCRT_NUM_LOCALES_ESP) pdcrt_objeto env[];
} pdcrt_env;

// Tipo de un índice a una variable local.
//
// Usado por `pdcrt_marco` y por `pdcrt_env`, representa el índice de una
// variable local. Es un entero con signo para permitir los valores negativos
// de `PDCRT_ID_*`.
typedef long pdcrt_local_index;

// Aloja y desaloja un entorno.
//
// `env_size` es el número de locales del entorno. `PDCRT_NUM_LOCALES_ESP` será
// agregado automáticamente.
pdcrt_error pdcrt_aloj_env(PDCRT_OUT pdcrt_env** env, pdcrt_alojador alojador, size_t env_size);
void pdcrt_dealoj_env(pdcrt_env* env, pdcrt_alojador alojador);

// Devuelve un C-string que es una versión legible del tipo del objeto
// especificado. Tal como con `pdcrt_perror`, el puntero devuelto tiene
// duración estática y es válido por toda la duración del programa.
const char* pdcrt_tipo_como_texto(pdcrt_tipo_de_objeto tipo);

// Aborta la ejecución del programa si `obj` no tiene el tipo `tipo`.
void pdcrt_objeto_debe_tener_tipo(pdcrt_objeto obj, pdcrt_tipo_de_objeto tipo);

// Crea un objeto entero (con el valor `v`).
pdcrt_objeto pdcrt_objeto_entero(int v);
// Crea un objeto real (con el valor `v`).
pdcrt_objeto pdcrt_objeto_float(float v);
// Crea un objeto "marca de pila".
pdcrt_objeto pdcrt_objeto_marca_de_pila(void);
// Crea un objeto desde un bool.
pdcrt_objeto pdcrt_objeto_booleano(bool v);
// Crea un objeto nulo.
pdcrt_objeto pdcrt_objeto_nulo(void);

// Aloja un objeto closure.
//
// `env_size` será pasado a `pdcrt_aloj_env`, mientras que `proc` será el
// "código" de la closure.
pdcrt_error pdcrt_objeto_aloj_closure(pdcrt_alojador alojador, pdcrt_proc_t proc, size_t env_size, PDCRT_OUT pdcrt_objeto* out);
// Aloja un objeto textual. Similar a `pdcrt_aloj_texto`.
pdcrt_error pdcrt_objeto_aloj_texto(PDCRT_OUT pdcrt_objeto* obj, pdcrt_alojador alojador, size_t lon);
// Aloja un objeto textual. Similar a `pdcrt_aloj_texto_desde_c`.
pdcrt_error pdcrt_objeto_aloj_texto_desde_cstr(PDCRT_OUT pdcrt_objeto* obj, pdcrt_alojador alojador, const char* cstr);
// Crea un objeto desde un texto.
pdcrt_objeto pdcrt_objeto_desde_texto(pdcrt_texto* texto);
// Aloja un objeto "real".
pdcrt_error pdcrt_objeto_aloj_objeto(PDCRT_OUT pdcrt_objeto* obj, pdcrt_alojador alojador, pdcrt_recvmsj recv, size_t num_attrs);

// Las siguientes funciones implementan los conceptos de igualdad/desigualdad
// de PseudoD. Te recomiendo que veas el "Reporte del lenguaje de programación
// PseudoD", sección "¿Qué es la Igualdad?" para los detalles.

// Determina si dos objetos tienen el mismo valor.
//
// No llama a sus métodos `igualA`/`operador_=`, incluso si tienen uno.
bool pdcrt_objeto_iguales(pdcrt_objeto a, pdcrt_objeto b);
// Determina si `a` y `b` son el mismo objeto.
bool pdcrt_objeto_identicos(pdcrt_objeto a, pdcrt_objeto b);


// Receptores de mensajes:

pdcrt_continuacion pdcrt_recv_numero(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets);
pdcrt_continuacion pdcrt_recv_texto(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets);
pdcrt_continuacion pdcrt_recv_closure(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets);
pdcrt_continuacion pdcrt_recv_marca_de_pila(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets);
pdcrt_continuacion pdcrt_recv_booleano(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets);
pdcrt_continuacion pdcrt_recv_nulo(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets);


// Formatear:
pdcrt_error pdcrt_formatear_texto(struct pdcrt_marco* marco,
                                  PDCRT_OUT pdcrt_texto** res,
                                  pdcrt_texto* fmt,
                                  PDCRT_ARR(num_objs) pdcrt_objeto* objs,
                                  size_t num_objs);


// La pila de valores.
//
// Implementada como un "arreglo dinámico" clásico (con
// capacidad+tamaño+elementos para garantizar que agregar elementos a la pila
// es O(1)). Actualmente la capacidad de la pila nunca disminuye lo que
// significa que los programas en PseudoD no son muy eficientes con la memoria
// por ahora.
typedef struct pdcrt_pila
{
    PDCRT_ARR(capacidad) pdcrt_objeto* elementos;
    size_t num_elementos;
    size_t capacidad;
} pdcrt_pila;

// Inicializa y desinicializa una pila.
pdcrt_error pdcrt_inic_pila(PDCRT_OUT pdcrt_pila* pila, pdcrt_alojador alojador);
void pdcrt_deinic_pila(pdcrt_pila* pila, pdcrt_alojador alojador);

// Empuja un objeto en la pila. Puede fallar si se queda sin memoria.
pdcrt_error pdcrt_empujar_en_pila(pdcrt_pila* pila, pdcrt_alojador alojador, pdcrt_objeto val);
// Saca un elemento de la pila. Aborta la ejecución del programa si la pila
// está vacía.
pdcrt_objeto pdcrt_sacar_de_pila(pdcrt_pila* pila);
// Obtiene el objeto en la cima de la pila. Aborta la ejecución del programa si
// la pila está vacía.
pdcrt_objeto pdcrt_cima_de_pila(pdcrt_pila* pila);
// Elimina el enésimo elemento de la pila. Nota que `n` indexa desde la cima de
// la pila, no desde el comienzo de `pila->elementos`. Por ejemplo: si `n` es 0
// entonces esto es lo mismo que `pdcrt_cima_de_pila`.
pdcrt_objeto pdcrt_eliminar_elemento_en_pila(pdcrt_pila* pila, size_t n);
// Inserta un elemento en la pila en una posición indicada. Tal como con
// `pdcrt_eliminar_elemento_en_pila`, `n` indexa desde la cima.
void pdcrt_insertar_elemento_en_pila(pdcrt_pila* pila, pdcrt_alojador alojador, size_t n, pdcrt_objeto obj);


// Lista de constantes ("constant pool").
//
// Contiene todas las constantes del programa. Tal como `pdcrt_env`, no tiene
// capacidad ya que no cambia de tamaño. Esta "tabla" es generada por el
// compilador y se inicializa al comienzo del programa.
typedef struct pdcrt_constantes
{
    size_t num_textos;
    PDCRT_ARR(num_textos) pdcrt_texto** textos;

    pdcrt_texto* operador_mas;
    pdcrt_texto* operador_menos;
    pdcrt_texto* operador_por;
    pdcrt_texto* operador_entre;
    pdcrt_texto* operador_menorQue;
    pdcrt_texto* operador_menorOIgualA;
    pdcrt_texto* operador_mayorQue;
    pdcrt_texto* operador_mayorOIgualA;
    pdcrt_texto* operador_igualA;
    pdcrt_texto* operador_noIgualA;
    pdcrt_texto* msj_igualA;
    pdcrt_texto* msj_clonar;
    pdcrt_texto* msj_llamar;
    pdcrt_texto* msj_comoTexto;
    pdcrt_texto* txt_verdadero;
    pdcrt_texto* txt_falso;
    pdcrt_texto* txt_nulo;
} pdcrt_constantes;

// Aloja una nueva lista de constantes.
pdcrt_error pdcrt_aloj_constantes(pdcrt_alojador alojador, PDCRT_OUT pdcrt_constantes* consts);
// Registra una constante textual en la lista. La lista es expandida en la
// medida necesaria para que la operación funcione.
pdcrt_error pdcrt_registrar_constante_textual(pdcrt_alojador alojador, pdcrt_constantes* consts, size_t idx, pdcrt_texto* texto);


// El contexto del intérprete.
//
// El núcleo del runtime. El contexto contiene todas las partes "globales" del
// programa, como la pila, el alojador, la lista de constantes e información de
// depuración.
//
// El contexto "posee" la pila y la lista de constantes: al desalojar el
// contexto también se desalojará la pila y la lista de constantes.
typedef struct pdcrt_contexto
{
    pdcrt_pila pila;
    pdcrt_alojador alojador;
    pdcrt_constantes constantes;
} pdcrt_contexto;

// Variantes de las funciones con el mismo nombre pero sin el `_simple` al
// final.
//
// En vez de pedirte un alojador, usan el contenido en el contexto. En todo lo
// demás son iguales a sus homónimos con `_simple`.
PDCRT_NULL void* pdcrt_alojar(pdcrt_contexto* ctx, size_t tam);
void pdcrt_dealojar(pdcrt_contexto* ctx, void* ptr, size_t tam);
PDCRT_NULL void* pdcrt_realojar(pdcrt_contexto* ctx, PDCRT_NULL void* ptr, size_t tam_actual, size_t tam_nuevo);

// Inicializa y desinicializa un contexto. También inicializa la pila y la
// lista de constantes de este.
pdcrt_error pdcrt_inic_contexto(pdcrt_contexto* ctx, pdcrt_alojador alojador);
void pdcrt_deinic_contexto(pdcrt_contexto* ctx, pdcrt_alojador alojador);

// Escribe a la salida estándar información útil cuando se quiere depurar un
// contexto. `extra` será escrito junto a la salida para que la puedas
// distinguir.
void pdcrt_depurar_contexto(pdcrt_contexto* ctx, const char* extra);

// Procesa los argumentos del CLI indicados en `argc` y `argv`, leyéndolos como
// argumentos del runtime. Esta función usa estado global y no es ni reentrante
// ni thread-safe. Tampoco puede llamarse más de una vez.
void pdcrt_procesar_cli(pdcrt_contexto* ctx, int argc, char* argv[]);


// Un marco de llamadas (también llamado "marco de activación").
//
// Cada marco contiene las variables locales de la función activada, un puntero
// al contexto global y otro puntero al marco anterior (es decir, al marco de
// la función que llamó a esta).
//
// Tal como con `pdcrt_env`, las locales del marco y `num_locales` tienen en
// cuenta `PDCRT_NUM_LOCALES_ESP`. De nuevo, tal como con `pdcrt_env`, la API
// toma en cuenta todos los "offsets" necesarios, pero si tu deseas acceder
// manualmente a las locales de un marco tienes que recordarlos.
typedef struct pdcrt_marco
{
    pdcrt_contexto* contexto;
    PDCRT_ARR(num_locales + PDCRT_NUM_LOCALES_ESP) pdcrt_objeto* locales;
    size_t num_locales;
    PDCRT_NULL struct pdcrt_marco* marco_anterior;
} pdcrt_marco;

// Inicializa y desinicializa un marco. `num_locales` es el número de locales,
// `PDCRT_NUM_LOCALES_ESP` será agregado automáticamente así que no tienes que
// tomarlo en cuenta.
//
// `marco_anterior` es el marco que activo a este o `NULL` si ningún marco
// activó a este.
pdcrt_error pdcrt_inic_marco(pdcrt_marco* marco, pdcrt_contexto* contexto, size_t num_locales, PDCRT_NULL pdcrt_marco* marco_anterior);
void pdcrt_deinic_marco(pdcrt_marco* marco);

// Fija el valor de una variable local.
void pdcrt_fijar_local(pdcrt_marco* marco, pdcrt_local_index n, pdcrt_objeto obj);
// Obtiene el valor de una variable local.
pdcrt_objeto pdcrt_obtener_local(pdcrt_marco* marco, pdcrt_local_index n);

// Tal como `pdcrt_depurar_contexto`, pero muestra información referente a un
// marco. `procname` es el nombre del procedimiento que contiene el marco a
// mostrar. `info` es un texto adicional que se mostrará en la salida.
void pdcrt_mostrar_marco(pdcrt_marco* marco, const char* procname, const char* info);

// La macro `PDCRT_RASTREAR_MARCO(marco, procname, info)` emitirá una llamada a
// `pdcrt_mostrar_marco` si `PDCRT_DBG_RASTREAR_MARCOS` está definido, o se
// expandirá a un statment vacío si no.
#ifdef PDCRT_DBG_RASTREAR_MARCOS
#define PDCRT_RASTREAR_MARCO(marco, procname, info) \
    pdcrt_mostrar_marco(marco, procname, info)
#else
#define PDCRT_RASTREAR_MARCO(marco, procname, info) \
    do { (void) (marco); (void) (procname); (void) (info); } while(0)
#endif


// Las siguientes macros solo existen para el compilador. Los usuarios de pdcrt
// nunca deberían usarlas.

#define PDCRT_MAIN()                            \
    int main(int argc, char* argv[])

#define PDCRT_MAIN_PRELUDE(nlocals)                                     \
    pdcrt_contexto ctx_real;                                            \
    pdcrt_error pderrno;                                                \
    pdcrt_contexto* ctx = &ctx_real;                                    \
    pdcrt_marco marco_real;                                             \
    pdcrt_marco* marco = &marco_real;                                   \
    pdcrt_alojador aloj;                                                \
    if((pderrno = pdcrt_aloj_alojador_de_arena(&aloj)) != PDCRT_OK)     \
    {                                                                   \
        puts(pdcrt_perror(pderrno));                                    \
        exit(PDCRT_SALIDA_ERROR);                                       \
    }                                                                   \
    if((pderrno = pdcrt_inic_contexto(&ctx_real, aloj)) != PDCRT_OK)    \
    {                                                                   \
        puts(pdcrt_perror(pderrno));                                    \
        exit(PDCRT_SALIDA_ERROR);                                       \
    }                                                                   \
    pdcrt_procesar_cli(ctx, argc, argv);                                \
    if((pderrno = pdcrt_inic_marco(&marco_real, ctx, nlocals, NULL)))   \
    {                                                                   \
        puts(pdcrt_perror(pderrno));                                    \
        exit(PDCRT_SALIDA_ERROR);                                       \
    }                                                                   \
    do {} while(0)

#define PDCRT_RUN(proc)                                                 \
        pdcrt_trampolin(marco, pdcrt_continuacion_iniciar((proc), &pdprocm_cont, marco, 0, 0))

#define PDCRT_MAIN_CONT_DECLR()                                     \
        pdcrt_continuacion pdprocm_cont(struct pdcrt_marco* marco);

#define PDCRT_MAIN_CONT()                                               \
        pdcrt_continuacion pdprocm_cont(struct pdcrt_marco* marco)      \
        {                                                               \
            pdcrt_deinic_marco(marco);                                 \
            pdcrt_deinic_contexto(marco->contexto, marco->contexto->alojador); \
            pdcrt_dealoj_alojador_de_arena(marco->contexto->alojador);  \
            exit(PDCRT_SALIDA_EXITO);                                   \
        }

// Registra una literal textual. Solo puede llamarse dentro de `PDCRT_MAIN()`.
// `lit` debe ser una literal de C (como `"hola mundo"`), mientras que `id`
// debe ser el índice en la lista de constantes de esta constante.
#define PDCRT_REGISTRAR_TXTLIT(id, lit)                                 \
    do                                                                  \
    {                                                                   \
        pdcrt_texto* txt;                                               \
        const char* str = (lit);                                        \
        if((pderrno = pdcrt_aloj_texto_desde_c(&txt, aloj, str)) != PDCRT_OK) \
        {                                                               \
            puts(pdcrt_perror(pderrno));                                \
            exit(PDCRT_SALIDA_ERROR);                                   \
        }                                                               \
        pdcrt_registrar_constante_textual(aloj, &ctx->constantes, id, txt); \
    }                                                                   \
    while(0)

// Declara, fija y obtiene una variable local.
#define PDCRT_LOCAL(idx, name)                              \
    pdcrt_fijar_local(marco, idx, pdcrt_objeto_nulo())
#define PDCRT_SET_LVAR(idx, val)                \
    pdcrt_fijar_local(marco, idx, val)
#define PDCRT_GET_LVAR(idx)                     \
    pdcrt_obtener_local(marco, idx)

#define PDCRT_PROC(name)                                                \
    pdcrt_continuacion pdproc_##name(pdcrt_marco* name##marco_actual, pdcrt_marco* name##marco_anterior, int name##nargs, int name##nrets) // {}
#define PDCRT_CONT(name, k)                                             \
    pdcrt_continuacion pdprock_##name##_k##k(pdcrt_marco* name##marco_actual) // {}
#define PDCRT_PROC_PRELUDE(name, nlocals)                               \
    pdcrt_error pderrno;                                                \
    pdcrt_contexto* ctx = name##marco_anterior->contexto;               \
    pdcrt_marco* marco = name##marco_actual;                            \
    do                                                                  \
    {                                                                   \
        if((pderrno = pdcrt_inic_marco(marco, ctx, nlocals, name##marco_anterior))) \
        {                                                               \
            puts(pdcrt_perror(pderrno));                                \
            exit(PDCRT_SALIDA_ERROR);                                   \
        }                                                               \
        pdcrt_empujar_en_pila(&ctx->pila, ctx->alojador, pdcrt_objeto_marca_de_pila()); \
        PDCRT_RASTREAR_MARCO(marco, #name, "preludio");                 \
    }                                                                   \
    while(0)
#define PDCRT_ASSERT_PARAMS(nparams)            \
    pdcrt_assert_params(marco, nparams)
#define PDCRT_PARAM(idx, param)                                     \
    pdcrt_fijar_local(marco, idx, pdcrt_sacar_de_pila(&ctx->pila))
#define PDCRT_CONT_PRELUDE(name, k)                     \
    pdcrt_marco* marco = name##marco_actual;            \
    PDCRT_RASTREAR_MARCO(marco, #name, "continuar");

// Obtiene un puntero a la función con nombre `name`. `name` es su nombre en el
// bytecode.
#define PDCRT_PROC_NAME(name)                   \
    &pdproc_##name

#define PDCRT_CONT_NAME(name, k)                \
    &pdprock_##name##_k##k

// Declara una función antes de usarla.
#define PDCRT_DECLARE_PROC(name)                \
    PDCRT_PROC(name);
#define PDCRT_DECLARE_CONT(name, k)             \
    PDCRT_CONT(name, k);

#define PDCRT_RETURN(nrets)                                       \
    do                                                            \
    {                                                             \
        pdcrt_op_retn(marco, nrets);                              \
        PDCRT_RASTREAR_MARCO(marco, "<unk>", "postludio");        \
        pdcrt_deinic_marco(marco);                                \
        return pdcrt_continuacion_devolver();                     \
    }                                                             \
    while(0)

#define PDCRT_REIFY_CONT(proc, k)                               \
    pdcrt_continuacion_normal(PDCRT_CONT_NAME(proc, k), marco)

#define PDCRT_CONTINUE(proc, k)                                         \
    do                                                                  \
    {                                                                   \
        PDCRT_RASTREAR_MARCO(marco, "<unk>", "continuar");              \
        return PDCRT_REIFY_CONT(proc, k);                               \
    }                                                                   \
    while(0)


// Los opcodes.
//
// Véase el ensamblador para ver que hace cada uno. Cada una de estas funciones
// corresponde casi 1-a-1 con un opcode.

void pdcrt_op_iconst(pdcrt_marco* marco, int c);
void pdcrt_op_bconst(pdcrt_marco* marco, bool c);
void pdcrt_op_lconst(pdcrt_marco* marco, int c);

pdcrt_continuacion pdcrt_op_sum(pdcrt_marco* marco, pdcrt_proc_continuacion proc);
pdcrt_continuacion pdcrt_op_sub(pdcrt_marco* marco, pdcrt_proc_continuacion proc);
pdcrt_continuacion pdcrt_op_mul(pdcrt_marco* marco, pdcrt_proc_continuacion proc);
pdcrt_continuacion pdcrt_op_div(pdcrt_marco* marco, pdcrt_proc_continuacion proc);
pdcrt_continuacion pdcrt_op_gt(pdcrt_marco* marco, pdcrt_proc_continuacion proc);
pdcrt_continuacion pdcrt_op_ge(pdcrt_marco* marco, pdcrt_proc_continuacion proc);
pdcrt_continuacion pdcrt_op_lt(pdcrt_marco* marco, pdcrt_proc_continuacion proc);
pdcrt_continuacion pdcrt_op_le(pdcrt_marco* marco, pdcrt_proc_continuacion proc);

void pdcrt_op_pop(pdcrt_marco* marco);

pdcrt_objeto pdcrt_op_lset(pdcrt_marco* marco);
void pdcrt_op_lget(pdcrt_marco* marco, pdcrt_objeto v);

void pdcrt_op_lsetc(pdcrt_marco* marco, pdcrt_objeto env, size_t alt, size_t ind);
void pdcrt_op_lgetc(pdcrt_marco* marco, pdcrt_objeto env, size_t alt, size_t ind);

// Recuerda que `padreidx` puede ser `PDCRT_ID_NIL`.
pdcrt_objeto pdcrt_op_open_frame(pdcrt_marco* marco, pdcrt_local_index padreidx, size_t tam);
void pdcrt_op_einit(pdcrt_marco* marco, pdcrt_objeto env, size_t i, pdcrt_objeto local);
void pdcrt_op_close_frame(pdcrt_marco* marco, pdcrt_objeto env);

void pdcrt_op_mkclz(pdcrt_marco* marco, pdcrt_local_index env, pdcrt_proc_t proc);
void pdcrt_op_mk0clz(pdcrt_marco* marco, pdcrt_proc_t proc);

void pdcrt_assert_params(pdcrt_marco* marco, int nparams);

pdcrt_continuacion pdcrt_op_dyncall(pdcrt_marco* marco, pdcrt_proc_continuacion proc, int acepta, int devuelve);

void pdcrt_op_retn(pdcrt_marco* marco, int n);
int pdcrt_real_return(pdcrt_marco* marco);
int pdcrt_passthru_return(pdcrt_marco* marco);

bool pdcrt_op_choose(pdcrt_marco* marco);

void pdcrt_op_rot(pdcrt_marco* marco, int n);

typedef enum pdcrt_cmp
{
    PDCRT_CMP_EQ,
    PDCRT_CMP_NEQ,
    PDCRT_CMP_REFEQ
} pdcrt_cmp;

pdcrt_continuacion pdcrt_op_cmp(pdcrt_marco* marco, pdcrt_cmp cmp, pdcrt_proc_continuacion proc);
void pdcrt_op_not(pdcrt_marco* marco);
void pdcrt_op_mtrue(pdcrt_marco* marco);

void pdcrt_op_prn(pdcrt_marco* marco);
void pdcrt_op_nl(pdcrt_marco* marco);

pdcrt_continuacion pdcrt_op_msg(pdcrt_marco* marco, pdcrt_proc_continuacion proc, int cid, int args, int rets);
pdcrt_continuacion pdcrt_op_tail_msg(pdcrt_marco* marco, int cid, int args, int rets);

void pdcrt_op_spush(pdcrt_marco* marco, pdcrt_local_index eact, pdcrt_local_index esup);
void pdcrt_op_spop(pdcrt_marco* marco, pdcrt_local_index eact, pdcrt_local_index esup);

#endif /* PDCRT_H */
