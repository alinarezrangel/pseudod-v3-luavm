#include "pdcrt.h"

#include <assert.h>
#include <math.h>
#include <errno.h>

#ifdef PDCRT_OPT_GNU
#include <malloc.h>
#define PDCRT_MALLOC_SIZE(ptr) malloc_usable_size(ptr)
#endif

#ifdef PDCRT_PRB_ALOJADOR_INESTABLE
#include <time.h>
#endif

// Macro simple de ayuda: emite una llamada a pdcrt_depurar_contexto si
// PDCRT_DBG_RASTREAR_CONTEXTO está definido o un statment vacío si no.
#ifdef PDCRT_DBG_RASTREAR_CONTEXTO
#define PDCRT_DEPURAR_CONTEXTO(ctx, extra)      \
    pdcrt_depurar_contexto((ctx), (extra))
#else
#define PDCRT_DEPURAR_CONTEXTO(ctx, extra)      \
    do { (void) (ctx); (void) (extra); } while(0)
#endif

// La macro `PDCRT_ESCRIBIR_ERROR` se espandirá a una llamada apropiada a
// `printf` con `err` (un `pdcrt_error`) e `info` (un string) si
// `PDCRT_DBG_ESCRIBIR_ERRORES` está definido, de otra forma se expandirá a un
// statment vacío.
#ifdef PDCRT_DBG_ESCRIBIR_ERRORES
#define PDCRT_ESCRIBIR_ERROR(err, info)         \
    printf("|%s: %s\n", (info), pdcrt_perror((err)))
#else
#define PDCRT_ESCRIBIR_ERROR(err, info)         \
    do { (void) err; (void) info; } while(0)
#endif


const char* pdcrt_perror(pdcrt_error err)
{
    static const char* const errores[] =
        { u8"Ok",
          u8"No hay memoria",
          u8"Operación inválida"
        };
    return errores[err];
}

// Envoltura de `abort()`. En un futuro esto hará más cosas (como guardar una
// imagen del programa).
_Noreturn static void pdcrt_abort(void)
{
    abort();
}

static void pdcrt_notifica_error_interno(void)
{
    fprintf(stderr, u8"\nEste error es un error interno del runtime, no es un error con tu programa. \
Por favor, reporta este bug en el repositorio del runtime \
<https://github.com/alinarezrangel/pseudod-v3-luavm/issues>.\n");
}

static void no_falla(pdcrt_error err)
{
    if(err != PDCRT_OK)
    {
        fprintf(stderr, "Error (que no debia fallar): %s\n", pdcrt_perror(err));
        pdcrt_notifica_error_interno();
        pdcrt_abort();
    }
}

static void pdcrt_rtassert(bool cond, const char* expr_str, const char* filename, long int lineno)
{
    if(!cond)
    {
        fprintf(stderr, "error:%s:%ld:%s\n", filename, lineno, expr_str);
        pdcrt_notifica_error_interno();
        pdcrt_abort();
    }
}

_Noreturn static void pdcrt_no_implementado(const char* op)
{
    fprintf(stderr, "Error: Operación '%s' aún no está implementada.\n", op);
    pdcrt_notifica_error_interno();
    pdcrt_abort();
}

_Noreturn static void pdcrt_inalcanzable(void)
{
    fprintf(stderr, "Error: Código inalcanzable ejecutado.\n");
    pdcrt_notifica_error_interno();
    pdcrt_abort();
}

// `PDCRT_ASSERT` es una macro que condicionalmente llama a
// `pdcrt_rtassert`. Incluso si no se está en modo de depuración (macro
// `NDEBUG` de C) `PDCRT_ASSERT` siempre evalúa su argumento.
#ifdef NDEBUG
#define PDCRT_ASSERT(expr) do { (void) (expr); } while(0)
#else
#define PDCRT_ASSERT(expr) pdcrt_rtassert(expr, #expr, __FILE__, __LINE__)
#endif

// Obtiene la siguiente capacidad de un arreglo dinámico.
//
// Devuelve la que debería ser la siguiente capacidad del arreglo. Trata de
// garantizar inserción en tiempo constante multiplicando la capacidad por 2 en
// cada ciclo.
//
// Además, se asegura de que la capacidad devuelta siempre pueda almacenar al
// menos `req_adicional` elementos nuevos, incluso si esto implica crecer un
// poco más que `cap_actual * 2`.
//
// Finalmente, toma en cuenta la API de los alojadores del runtime y nunca
// devuelve una capacidad de 0 (`pdcrt_siguiente_capacidad(0, 0, 0)` devuelve
// un número mayor que 0).
static size_t pdcrt_siguiente_capacidad(size_t cap_actual, size_t tam_actual, size_t req_adicional)
{
    size_t base = cap_actual == 0? 1 : 0;
    size_t tam_deseado = tam_actual + req_adicional;
    size_t cap_base = 2 * (cap_actual + base);
    PDCRT_ASSERT(cap_base >= cap_actual);
    // Si `tam_deseado >= cap_base` entonces el nuevo tamaño `cap_base` aún no
    // podría hospedar a `req_adicional` elementos nuevos.
    return cap_base + (tam_deseado < cap_base? 0 : (tam_deseado - cap_actual));
}


// Los alojadores predeterminados:

static void* pdcrt_alojador_de_malloc_impl(void* datos_del_usuario, void* ptr, size_t tam_viejo, size_t tam_nuevo)
{
    (void) datos_del_usuario; // Siempre es NULL
    if(tam_nuevo == 0)
    {
        free(ptr);
        return NULL;
    }
    else if(tam_viejo == 0)
    {
        return malloc(tam_nuevo);
    }
    else
    {
        return realloc(ptr, tam_nuevo);
    }
}

pdcrt_alojador pdcrt_alojador_de_malloc(void)
{
    return (pdcrt_alojador){ .alojar = &pdcrt_alojador_de_malloc_impl, .datos = NULL };
}

typedef struct pdcrt_alojador_de_arena
{
    PDCRT_ARR(num_punteros) void** punteros;
    size_t num_punteros;
} pdcrt_alojador_de_arena;

static void pdcrt_arena_agregar(pdcrt_alojador_de_arena* arena, void* ptr)
{
    void** narena = realloc(arena->punteros, sizeof(void*) * (arena->num_punteros + 1));
    PDCRT_ASSERT(narena != NULL);
    narena[arena->num_punteros] = ptr;
    arena->num_punteros++;
    arena->punteros = narena;
}

static void pdcrt_arena_reemplazar(pdcrt_alojador_de_arena* arena, void* ptr, void* nptr)
{
    for(size_t i = 0; i < arena->num_punteros; i++)
    {
        if(arena->punteros[i] == ptr)
        {
            arena->punteros[i] = nptr;
        }
    }
}

static void* pdcrt_alojar_en_arena(void* vdt, void* ptr, size_t tam_viejo, size_t tam_nuevo)
{
    pdcrt_alojador_de_arena* dt = vdt;
    if(tam_nuevo == 0)
    {
        // Nada: toda la memoria se liberará cuando se destruya la arena
        return NULL;
    }
    else if(tam_viejo == 0)
    {
#ifdef PDCRT_PRB_ALOJADOR_INESTABLE
        if(rand() % PDCRT_PRB_ALOJADOR_INESTABLE == 0)
        {
            return NULL;
        }
#endif
        void* nptr = malloc(tam_nuevo);
        if(nptr != NULL)
        {
            pdcrt_arena_agregar(dt, nptr);
        }
        return nptr;
    }
    else
    {
#ifdef PDCRT_PRB_ALOJADOR_INESTABLE
        if(rand() % PDCRT_PRB_ALOJADOR_INESTABLE == 0)
        {
            return NULL;
        }
#endif
        void* nptr = realloc(ptr, tam_nuevo);
        if(nptr != NULL)
        {
            pdcrt_arena_reemplazar(dt, ptr, nptr);
        }
        return nptr;
    }
}

pdcrt_error pdcrt_aloj_alojador_de_arena(pdcrt_alojador* aloj)
{
    pdcrt_alojador_de_arena* dt = malloc(sizeof(pdcrt_alojador_de_arena));
    if(dt == NULL)
    {
        PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, __func__);
        return PDCRT_ENOMEM;
    }
    dt->punteros = NULL;
    dt->num_punteros = 0;
    aloj->alojar = &pdcrt_alojar_en_arena;
    aloj->datos = dt;
#if defined(PDCRT_PRB_ALOJADOR_INESTABLE) && !defined(PDCRT_PRB_SRAND)
    unsigned int s = time(NULL);
    srand(s);
    srand(s = rand());
    printf(u8"|Semilla del generador de números aleatorios: %u\n", s);
#elif defined(PDCRT_PRB_SRAND)
    srand(PDCRT_PRB_SRAND);
#endif
    return PDCRT_OK;
}

void pdcrt_dealoj_alojador_de_arena(pdcrt_alojador aloj)
{
    pdcrt_alojador_de_arena* arena = aloj.datos;
#ifdef PDCRT_DBG_ESTADISTICAS_DE_LOS_ALOJADORES
#ifdef PDCRT_MALLOC_SIZE
    size_t total = 0, maxaloj = 0;
    int cant_alojaciones_por_desvstd[3] = {0, 0, 0};
    double tamprom = 0, var = 0, desvstd = 0;
    for(size_t i = 0; i < arena->num_punteros; i++)
    {
        size_t tam = PDCRT_MALLOC_SIZE(arena->punteros[i]);
        total += tam;
        if(tam > maxaloj)
        {
            maxaloj = tam;
        }
    }
    tamprom = ((double) total) / ((double) arena->num_punteros);
    for(size_t i = 0; i < arena->num_punteros; i++)
    {
        size_t tam = PDCRT_MALLOC_SIZE(arena->punteros[i]);
        var += (tam - tamprom) * (tam - tamprom);
    }
    var /= arena->num_punteros;
    desvstd = sqrt(var);
    for(size_t i = 0; i < arena->num_punteros; i++)
    {
        size_t tam = PDCRT_MALLOC_SIZE(arena->punteros[i]);
        double delta = abs(((double) tam) - tamprom);
        double ind = delta / desvstd;
        if(ind <= 1)
            cant_alojaciones_por_desvstd[0]++;
        else if(ind <= 2)
            cant_alojaciones_por_desvstd[1]++;
        else
            cant_alojaciones_por_desvstd[2]++;
    }
    printf(u8"|Desalojando alojador de arena: %zd elementos, %zd bytes en total, máxima alojación de %zd bytes.\n",
           arena->num_punteros, total, maxaloj);
    printf(u8"|  Total de %zd bytes, %zd KiB, %zd MiB\n", total, total / 1024, (total / 1024) / 1024);
    printf(u8"|  Máxima alojación de %zd bytes, %zd KiB, %zd MiB\n|\n", maxaloj, maxaloj / 1024, (maxaloj / 1024) / 1024);

    printf(u8"|  Tamaño promedio: %.2F bytes / %zd bytes\n", tamprom, (size_t) tamprom);
    printf(u8"|  Varianza: %.2F bytes / %zd bytes\n", var, (size_t) var);
    printf(u8"|  Desviación estándar: %.2F bytes / %zd bytes\n|\n", desvstd, (size_t) desvstd);

    printf(u8"|  %d alojaciones (%.2F%%) tienen 1 desv. std. o menos\n",
           cant_alojaciones_por_desvstd[0],
           100 * (((double)cant_alojaciones_por_desvstd[0]) / ((double)arena->num_punteros)));
    printf(u8"|  %d alojaciones (%.2F%%) tienen más de 1 y menos de 2 desv. std.\n",
           cant_alojaciones_por_desvstd[1],
           100 * (((double)cant_alojaciones_por_desvstd[1]) / ((double)arena->num_punteros)));
    printf(u8"|  %d alojaciones (%.2F%%) tienen más de 2 desv. std.\n",
           cant_alojaciones_por_desvstd[2],
           100 * (((double)cant_alojaciones_por_desvstd[2]) / ((double)arena->num_punteros)));
#else
    printf(u8"|Desalojando alojador de arena: %zd elementos\n", arena->num_punteros);
    printf(u8"|  Advertencia: no se pudo solicitar el tamaño en bytes de los elementos\n");
#endif
#endif
    for(size_t i = 0; i < arena->num_punteros; i++)
    {
        free(arena->punteros[i]);
    }
    free(arena->punteros);
    free(arena);
}


// Operaciones del alojador:

PDCRT_NULL void* pdcrt_alojar_simple(pdcrt_alojador alojador, size_t tam)
{
    return alojador.alojar(alojador.datos, NULL, 0, tam);
}

void pdcrt_dealojar_simple(pdcrt_alojador alojador, void* ptr, size_t tam)
{
    (void) alojador.alojar(alojador.datos, ptr, tam, 0);
}

void* pdcrt_realojar_simple(pdcrt_alojador alojador, void* ptr, size_t tam_actual, size_t tam_nuevo)
{
    return alojador.alojar(alojador.datos, ptr, tam_actual, tam_nuevo);
}


// Textos:

pdcrt_error pdcrt_aloj_texto(PDCRT_OUT pdcrt_texto** texto, pdcrt_alojador alojador, size_t lon)
{
    *texto = pdcrt_alojar_simple(alojador, sizeof(pdcrt_texto));
    if(*texto == NULL)
    {
        PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, "pdcrt_aloj_texto: alojando el texto mismo");
        return PDCRT_ENOMEM;
    }
    if(lon == 0)
    {
        (*texto)->contenido = NULL;
    }
    else
    {
        (*texto)->contenido = pdcrt_alojar_simple(alojador, sizeof(char) * lon);
        if((*texto)->contenido == NULL)
        {
            PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, "pdcrt_aloj_texto: alojando el contenido del texto");
            pdcrt_dealojar_simple(alojador, *texto, sizeof(pdcrt_texto));
            return PDCRT_ENOMEM;
        }
    }
    (*texto)->longitud = lon;
    return PDCRT_OK;
}

pdcrt_error pdcrt_aloj_texto_desde_c(PDCRT_OUT pdcrt_texto** texto, pdcrt_alojador alojador, const char* cstr)
{
    size_t len = strlen(cstr);
    pdcrt_error errc = pdcrt_aloj_texto(texto, alojador, len);
    if(errc != PDCRT_OK)
    {
        PDCRT_ESCRIBIR_ERROR(errc, __func__);
        return errc;
    }
    if((*texto)->contenido)
    {
        memcpy((*texto)->contenido, cstr, len);
    }
    return PDCRT_OK;
}

void pdcrt_dealoj_texto(pdcrt_alojador alojador, pdcrt_texto* texto)
{
    pdcrt_dealojar_simple(alojador, texto->contenido, sizeof(char) * texto->longitud);
    pdcrt_dealojar_simple(alojador, texto, sizeof(pdcrt_texto));
}

bool pdcrt_textos_son_iguales(pdcrt_texto* a, pdcrt_texto* b)
{
    if(a->longitud != b->longitud)
    {
        return false;
    }
    for(size_t i = 0; i < a->longitud; i++)
    {
        if(a->contenido[i] != b->contenido[i])
        {
            return false;
        }
    }
    return true;
}

static void pdcrt_escribir_texto_al_archivo(FILE* f, pdcrt_texto* texto)
{
    for(size_t i = 0; i < texto->longitud; i++)
    {
        fprintf(f, "%c", texto->contenido[i]);
    }
}

static void pdcrt_escribir_texto(pdcrt_texto* texto)
{
    pdcrt_escribir_texto_al_archivo(stdout, texto);
}

static void pdcrt_escribir_texto_max(pdcrt_texto* texto, size_t max)
{
    size_t i = 0;
    for(; i < texto->longitud && (i + 3) < max; i++)
    {
        printf("%c", texto->contenido[i]);
    }
    if(i < texto->longitud)
    {
        printf("...");
    }
}

pdcrt_error pdcrt_aloj_espacio_de_nombres(pdcrt_alojador alojador, PDCRT_OUT pdcrt_espacio_de_nombres** espacio, size_t num)
{
    *espacio = pdcrt_alojar_simple(alojador, sizeof(pdcrt_espacio_de_nombres));
    if(!*espacio)
    {
        return PDCRT_ENOMEM;
    }
    pdcrt_espacio_de_nombres* p = *espacio;
    p->num_nombres = num;
    p->ultimo_nombre_creado = 0;
    if(num == 0)
    {
        p->nombres = NULL;
    }
    else
    {
        p->nombres = pdcrt_alojar_simple(alojador, sizeof(pdcrt_edn_triple) * num);
        if(!p->nombres)
        {
            free(*espacio);
            *espacio = NULL;
            return PDCRT_ENOMEM;
        }
    }
    return PDCRT_OK;
}

void pdcrt_dealoj_espacio_de_nombres(pdcrt_alojador alojador, pdcrt_espacio_de_nombres** espacio)
{
    if((*espacio)->num_nombres > 0)
        pdcrt_dealojar_simple(alojador, (*espacio)->nombres, sizeof(pdcrt_edn_triple) * (*espacio)->num_nombres);
    pdcrt_dealojar_simple(alojador, *espacio, sizeof(pdcrt_espacio_de_nombres));
    *espacio = NULL;
}

void pdcrt_agregar_nombre_al_espacio_de_nombres(pdcrt_espacio_de_nombres* espacio, pdcrt_texto* nombre, bool es_autoejecutable, pdcrt_objeto valor)
{
    PDCRT_ASSERT(espacio->ultimo_nombre_creado < espacio->num_nombres);
    pdcrt_edn_triple* triple = &espacio->nombres[espacio->ultimo_nombre_creado++];
    triple->nombre = nombre;
    triple->es_autoejecutable = es_autoejecutable;
    triple->valor = valor;
}

bool pdcrt_obtener_campo_del_espacio_de_nombres(pdcrt_espacio_de_nombres* espacio,
                                                pdcrt_texto* nombre,
                                                PDCRT_OUT pdcrt_objeto* valor)
{
    for(size_t i = 0; i < espacio->num_nombres; i++)
    {
        pdcrt_edn_triple triple = espacio->nombres[i];
        if(pdcrt_textos_son_iguales(triple.nombre, nombre))
        {
            *valor = triple.valor;
            return true;
        }
    }

    *valor = pdcrt_objeto_nulo();
    return false;
}

pdcrt_error pdcrt_aloj_arreglo(pdcrt_alojador alojador, PDCRT_OUT pdcrt_arreglo* arr, size_t capacidad)
{
    arr->capacidad = pdcrt_siguiente_capacidad(capacidad, 0, 0);
    arr->elementos = pdcrt_alojar_simple(alojador, arr->capacidad * sizeof(pdcrt_objeto));
    if(!arr->elementos)
    {
        return PDCRT_ENOMEM;
    }
    arr->longitud = 0;
    return PDCRT_OK;
}

void pdcrt_dealoj_arreglo(pdcrt_alojador alojador, pdcrt_arreglo* arr)
{
    pdcrt_dealojar_simple(alojador, arr->elementos, arr->capacidad * sizeof(pdcrt_objeto));
    arr->capacidad = 0;
    arr->longitud = 0;
}

pdcrt_error pdcrt_aloj_arreglo_vacio(pdcrt_alojador alojador, PDCRT_OUT pdcrt_arreglo* arr)
{
    return pdcrt_aloj_arreglo(alojador, arr, 0);
}

pdcrt_error pdcrt_aloj_arreglo_con_1(pdcrt_alojador alojador, PDCRT_OUT pdcrt_arreglo* arr, pdcrt_objeto el0)
{
    pdcrt_error pderrno = pdcrt_aloj_arreglo(alojador, arr, 1);
    if(pderrno != PDCRT_OK)
    {
        return pderrno;
    }
    arr->longitud = 1;
    arr->elementos[0] = el0;
    return PDCRT_OK;
}

pdcrt_error pdcrt_aloj_arreglo_con_2(pdcrt_alojador alojador, PDCRT_OUT pdcrt_arreglo* arr, pdcrt_objeto el0, pdcrt_objeto el1)
{
    pdcrt_error pderrno = pdcrt_aloj_arreglo(alojador, arr, 2);
    if(pderrno != PDCRT_OK)
    {
        return pderrno;
    }
    arr->longitud = 2;
    arr->elementos[0] = el0;
    arr->elementos[1] = el1;
    return PDCRT_OK;
}

pdcrt_error pdcrt_realoj_arreglo(pdcrt_alojador alojador, pdcrt_arreglo* arr, size_t nueva_capacidad)
{
    PDCRT_ASSERT(nueva_capacidad >= arr->longitud);
    pdcrt_objeto* nuevos_elementos = pdcrt_realojar_simple(alojador, arr->elementos, arr->capacidad * sizeof(pdcrt_objeto), nueva_capacidad * sizeof(pdcrt_objeto));
    if(!nuevos_elementos)
    {
        return PDCRT_ENOMEM;
    }
    arr->elementos = nuevos_elementos;
    arr->capacidad = nueva_capacidad;
    return PDCRT_OK;
}

void pdcrt_arreglo_fijar_elemento(pdcrt_arreglo* arr, size_t indice, pdcrt_objeto nuevo_elemento)
{
    PDCRT_ASSERT(indice < arr->longitud);
    arr->elementos[indice] = nuevo_elemento;
}

pdcrt_objeto pdcrt_arreglo_obtener_elemento(pdcrt_arreglo* arr, size_t indice)
{
    PDCRT_ASSERT(indice < arr->longitud);
    return arr->elementos[indice];
}

pdcrt_error pdcrt_arreglo_concatenar(pdcrt_alojador alojador,
                                     pdcrt_arreglo* arr_final,
                                     pdcrt_arreglo* arr_fuente)
{
    pdcrt_error pderrno = pdcrt_realoj_arreglo(alojador, arr_final, arr_final->capacidad + arr_fuente->capacidad);
    if(pderrno != PDCRT_OK)
    {
        return pderrno;
    }
    for(size_t i = 0; i < arr_fuente->longitud; i++)
    {
        arr_final->elementos[arr_final->longitud + i] = arr_fuente->elementos[i];
    }
    arr_final->longitud += arr_fuente->longitud;
    return PDCRT_OK;
}

pdcrt_error pdcrt_arreglo_agregar_al_final(pdcrt_alojador alojador,
                                           pdcrt_arreglo* arr,
                                           pdcrt_objeto el)
{
    if(arr->longitud >= arr->capacidad)
    {
        size_t nueva_capacidad = pdcrt_siguiente_capacidad(arr->capacidad, arr->longitud, 1);
        pdcrt_error pderrno = pdcrt_realoj_arreglo(alojador, arr, nueva_capacidad);
        if(pderrno != PDCRT_OK)
        {
            return pderrno;
        }
    }
    PDCRT_ASSERT(arr->longitud < arr->capacidad);
    arr->elementos[arr->longitud] = el;
    arr->longitud += 1;
    return PDCRT_OK;
}

pdcrt_error pdcrt_arreglo_redimensionar(pdcrt_alojador alojador,
                                        pdcrt_arreglo* arr,
                                        size_t nueva_longitud)
{
    if(nueva_longitud < arr->longitud)
    {
        arr->longitud = nueva_longitud;
    }
    else if(nueva_longitud > arr->longitud)
    {
        if(nueva_longitud > arr->capacidad)
        {
            size_t nueva_capacidad = pdcrt_siguiente_capacidad(arr->capacidad, arr->longitud, (nueva_longitud - arr->capacidad));
            pdcrt_error pderrno = pdcrt_realoj_arreglo(alojador, arr, nueva_capacidad);
            if(pderrno != PDCRT_OK)
            {
                return pderrno;
            }
            arr->capacidad = nueva_capacidad;
        }
        for(size_t i = arr->longitud; i < nueva_longitud; i++)
        {
            arr->elementos[i] = pdcrt_objeto_nulo();
        }
        arr->longitud = nueva_longitud;
    }
    return PDCRT_OK;
}

pdcrt_error pdcrt_arreglo_mover_elementos(
    pdcrt_arreglo* fuente,
    size_t inicio_fuente,
    size_t final_fuente,
    pdcrt_arreglo* destino,
    size_t inicio_destino
)
{
    PDCRT_ASSERT(final_fuente <= fuente->longitud);
    PDCRT_ASSERT(inicio_fuente <= fuente->longitud);
    PDCRT_ASSERT(inicio_destino <= destino->longitud);
    PDCRT_ASSERT(final_fuente >= inicio_fuente);
    PDCRT_ASSERT((final_fuente - inicio_fuente) <= fuente->longitud);
    for(size_t i = inicio_fuente; i < final_fuente; i++)
    {
        destino->elementos[inicio_destino + (i - inicio_fuente)] = fuente->elementos[i];
    }
    return PDCRT_OK;
}


// Continuaciones:


pdcrt_continuacion pdcrt_continuacion_iniciar(
    pdcrt_proc_t proc,
    pdcrt_proc_continuacion contp,
    struct pdcrt_marco* marco_sup,
    int args,
    int rets
)
{
    pdcrt_continuacion cont;
    cont.tipo = PDCRT_CONT_INICIAR;
    cont.valor.iniciar.proc = (pdcrt_funcion_generica) proc;
    cont.valor.iniciar.cont = (pdcrt_funcion_generica) contp;
    cont.valor.iniciar.marco_superior = marco_sup;
    cont.valor.iniciar.args = args;
    cont.valor.iniciar.rets = rets;
    return cont;
}

pdcrt_continuacion pdcrt_continuacion_normal(pdcrt_proc_continuacion proc, struct pdcrt_marco* marco)
{
    pdcrt_continuacion cont;
    cont.tipo = PDCRT_CONT_CONTINUAR;
    cont.valor.continuar.proc = (pdcrt_funcion_generica) proc;
    cont.valor.continuar.marco_actual = marco;
    return cont;
}

pdcrt_continuacion pdcrt_continuacion_devolver(void)
{
    pdcrt_continuacion cont;
    cont.tipo = PDCRT_CONT_DEVOLVER;
    return cont;
}

pdcrt_continuacion pdcrt_continuacion_enviar_mensaje(pdcrt_proc_continuacion proc,
                                                     struct pdcrt_marco* marco,
                                                     pdcrt_objeto yo,
                                                     pdcrt_objeto mensaje,
                                                     int args,
                                                     int rets)
{
    pdcrt_continuacion cont;
    cont.tipo = PDCRT_CONT_ENVIAR_MENSAJE;
    cont.valor.enviar_mensaje.recv = (pdcrt_funcion_generica) proc;
    cont.valor.enviar_mensaje.marco = marco;
    cont.valor.enviar_mensaje.yo = yo;
    cont.valor.enviar_mensaje.mensaje = mensaje;
    cont.valor.enviar_mensaje.args = args;
    cont.valor.enviar_mensaje.rets = rets;
    return cont;
}

pdcrt_continuacion pdcrt_continuacion_tail_iniciar(
    pdcrt_proc_t proc,
    struct pdcrt_marco* marco_superior,
    int args,
    int rets
)
{
    pdcrt_continuacion cont;
    cont.tipo = PDCRT_CONT_TAIL_INICIAR;
    cont.valor.tail_iniciar.proc = (pdcrt_funcion_generica) proc;
    cont.valor.tail_iniciar.marco_superior = marco_superior;
    cont.valor.tail_iniciar.args = args;
    cont.valor.tail_iniciar.rets = rets;
    return cont;
}

pdcrt_continuacion pdcrt_continuacion_tail_enviar_mensaje(
    struct pdcrt_marco* marco_superior,
    pdcrt_objeto yo,
    pdcrt_objeto mensaje,
    int args,
    int rets
)
{
    pdcrt_continuacion cont;
    cont.tipo = PDCRT_CONT_TAIL_ENVIAR_MENSAJE;
    cont.valor.tail_enviar_mensaje.marco_superior = marco_superior;
    cont.valor.tail_enviar_mensaje.yo = yo;
    cont.valor.tail_enviar_mensaje.mensaje = mensaje;
    cont.valor.tail_enviar_mensaje.args = args;
    cont.valor.tail_enviar_mensaje.rets = rets;
    return cont;
}

// PDCRT_TAM_PILA_DE_CONTINUACIONES es el nuḿero de marcos que estan en la
// pila. Si cambias su valor, asegúrate de cambiar la prueba
// `tests/tailcall.pdasm`.
#define PDCRT_TAM_PILA_DE_CONTINUACIONES 512
void pdcrt_trampolin(struct pdcrt_marco* marco, pdcrt_continuacion k)
{
    pdcrt_continuacion pila[PDCRT_TAM_PILA_DE_CONTINUACIONES];
    pdcrt_marco marcos[PDCRT_TAM_PILA_DE_CONTINUACIONES];
    pila[0] = k;
    marcos[0] = *marco;
    size_t tam_pila = 1;
    while(tam_pila > 0)
    {
        // El -2 es porque las acciones PDCRT_CONT_INICIAR y
        // PDCRT_CONT_ENVIAR_MENSAJE requieren dos espacios en la pila (uno
        // para la continuación actual, otro para la nueva función).
        if(tam_pila >= (PDCRT_TAM_PILA_DE_CONTINUACIONES - 2))
        {
            fprintf(stderr, u8"Límite de recursión alcanzado: %zd llamadas recursivas\n", tam_pila);
            pdcrt_abort();
        }

        pdcrt_continuacion sk = pila[tam_pila - 1];
        switch(sk.tipo)
        {
        case PDCRT_CONT_INICIAR:
        {
            pdcrt_proc_t fproc = (pdcrt_proc_t) sk.valor.iniciar.proc;
            pdcrt_proc_continuacion kproc = (pdcrt_proc_continuacion) sk.valor.iniciar.cont;
            pila[tam_pila - 1] = pdcrt_continuacion_normal(kproc, sk.valor.iniciar.marco_superior);
            pila[tam_pila] = (*fproc)(
                &marcos[tam_pila],
                sk.valor.iniciar.marco_superior,
                sk.valor.iniciar.args,
                sk.valor.iniciar.rets);
            tam_pila += 1;
            break;
        }
        case PDCRT_CONT_CONTINUAR:
        {
            pdcrt_proc_continuacion kproc = (pdcrt_proc_continuacion) sk.valor.continuar.proc;
            pila[tam_pila - 1] = (*kproc)(sk.valor.continuar.marco_actual);
            break;
        }
        case PDCRT_CONT_ENVIAR_MENSAJE:
        {
            pdcrt_proc_continuacion kproc = (pdcrt_proc_continuacion) sk.valor.enviar_mensaje.recv;
            pila[tam_pila - 1] = pdcrt_continuacion_normal(kproc, sk.valor.enviar_mensaje.marco);
            tam_pila += 1;
            pila[tam_pila - 1] = PDCRT_ENVIAR_MENSAJE(
                sk.valor.enviar_mensaje.marco,
                sk.valor.enviar_mensaje.yo,
                sk.valor.enviar_mensaje.mensaje,
                sk.valor.enviar_mensaje.args,
                sk.valor.enviar_mensaje.rets);
            break;
        }
        case PDCRT_CONT_DEVOLVER:
            tam_pila -= 1;
            break;
        case PDCRT_CONT_TAIL_INICIAR:
        {
            pdcrt_proc_t fproc = (pdcrt_proc_t) sk.valor.tail_iniciar.proc;
            pila[tam_pila - 1] = (*fproc)(
                &marcos[tam_pila],
                sk.valor.tail_iniciar.marco_superior,
                sk.valor.tail_iniciar.args,
                sk.valor.tail_iniciar.rets);
            break;
        }
        case PDCRT_CONT_TAIL_ENVIAR_MENSAJE:
        {
            pila[tam_pila - 1] = PDCRT_ENVIAR_MENSAJE(
                sk.valor.tail_enviar_mensaje.marco_superior,
                sk.valor.tail_enviar_mensaje.yo,
                sk.valor.tail_enviar_mensaje.mensaje,
                sk.valor.tail_enviar_mensaje.args,
                sk.valor.tail_enviar_mensaje.rets);
            break;
        }
        }
    }
}
#undef PDCRT_TAM_PILA_DE_CONTINUACIONES


// Entornos:

pdcrt_error pdcrt_aloj_env(pdcrt_env** env, pdcrt_alojador alojador, size_t env_size)
{
    *env = pdcrt_alojar_simple(alojador, sizeof(pdcrt_env) + sizeof(pdcrt_objeto) * env_size);
    if(!*env)
    {
        PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, __func__);
        return PDCRT_ENOMEM;
    }
    else
    {
        (*env)->env_size = env_size;
        return PDCRT_OK;
    }
}

void pdcrt_dealoj_env(pdcrt_env* env, pdcrt_alojador alojador)
{
    pdcrt_dealojar_simple(alojador, env, sizeof(pdcrt_env) + sizeof(pdcrt_objeto) * env->env_size);
}


// Objetos:

const char* pdcrt_tipo_como_texto(pdcrt_tipo_de_objeto tipo)
{
    static const char* const tipos[] =
        { u8"Entero",
          u8"Float",
          u8"Marca de pila",
          u8"Closure (función)",
          u8"Texto",
          u8"Objeto",
          u8"Booleano",
          u8"Nulo",
          u8"Arreglo",
          u8"Puntero de C",
          u8"Espacio de nombres",
        };
    return tipos[tipo];
}

static void pdcrt_escribir_traceback(pdcrt_marco* marco)
{
    for(pdcrt_marco* m = marco; m; m = m->marco_anterior)
    {
        if(m->nombre)
            fprintf(stderr, "  %s\n", m->nombre);
    }
}

void pdcrt_objeto_debe_tener_tipo(pdcrt_objeto obj, pdcrt_tipo_de_objeto tipo)
{
    if(obj.tag != tipo)
    {
        fprintf(stderr, u8"Objeto de tipo %s debía tener tipo %s\n", pdcrt_tipo_como_texto(obj.tag), pdcrt_tipo_como_texto(tipo));
        if(obj.tag == PDCRT_TOBJ_MARCA_DE_PILA || tipo == PDCRT_TOBJ_MARCA_DE_PILA)
        {
            pdcrt_notifica_error_interno();
        }
        pdcrt_abort();
    }
}

static void pdcrt_objeto_debe_tener_uno_de_los_tipos(pdcrt_marco* marco,
                                                     pdcrt_objeto obj,
                                                     pdcrt_tipo_de_objeto tipo1,
                                                     pdcrt_tipo_de_objeto tipo2)
{
    if(obj.tag != tipo1 && obj.tag != tipo2)
    {
        fprintf(stderr,
                u8"Objeto de tipo %s debía tener tipos %s o %s\n",
                pdcrt_tipo_como_texto(obj.tag),
                pdcrt_tipo_como_texto(tipo1),
                pdcrt_tipo_como_texto(tipo2));
        pdcrt_escribir_traceback(marco);
        if(obj.tag == PDCRT_TOBJ_MARCA_DE_PILA)
        {
            pdcrt_notifica_error_interno();
        }
        pdcrt_abort();
    }
}

static void pdcrt_objeto_debe_tener_tipo_tb(pdcrt_marco* marco, pdcrt_objeto obj, pdcrt_tipo_de_objeto tipo)
{
    if(obj.tag != tipo)
    {
        fprintf(stderr, u8"Objeto de tipo %s debía tener tipo %s\n", pdcrt_tipo_como_texto(obj.tag), pdcrt_tipo_como_texto(tipo));
        pdcrt_escribir_traceback(marco);
        if(obj.tag == PDCRT_TOBJ_MARCA_DE_PILA || tipo == PDCRT_TOBJ_MARCA_DE_PILA)
        {
            pdcrt_notifica_error_interno();
        }
        pdcrt_abort();
    }
}

static void pdcrt_objeto_debe_tener_closure(pdcrt_marco* marco, pdcrt_objeto obj)
{
    pdcrt_objeto_debe_tener_uno_de_los_tipos(marco, obj, PDCRT_TOBJ_CLOSURE, PDCRT_TOBJ_OBJETO);
}

pdcrt_objeto pdcrt_objeto_entero(pdcrt_entero v)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_ENTERO;
    obj.value.i = v;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_numero;
    return obj;
}

pdcrt_objeto pdcrt_objeto_float(pdcrt_float v)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_FLOAT;
    obj.value.f = v;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_numero;
    return obj;
}

pdcrt_objeto pdcrt_objeto_marca_de_pila(void)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_MARCA_DE_PILA;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_marca_de_pila;
    return obj;
}

pdcrt_objeto pdcrt_objeto_booleano(bool v)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_BOOLEANO;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_booleano;
    obj.value.b = v;
    return obj;
}

pdcrt_objeto pdcrt_objeto_nulo(void)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_NULO;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_nulo;
    return obj;
}

pdcrt_objeto pdcrt_objeto_voidptr(void* ptr)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_VOIDPTR;
    obj.value.p = ptr;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_voidptr;
    return obj;
}

pdcrt_error pdcrt_objeto_aloj_closure(pdcrt_alojador alojador, pdcrt_proc_t proc, size_t env_size, pdcrt_objeto* obj)
{
    obj->tag = PDCRT_TOBJ_CLOSURE;
    obj->value.c.proc = (pdcrt_funcion_generica) proc;
    obj->recv = (pdcrt_funcion_generica) &pdcrt_recv_closure;
    return pdcrt_aloj_env(&obj->value.c.env, alojador, env_size + PDCRT_NUM_LOCALES_ESP);
}

pdcrt_error pdcrt_objeto_aloj_texto(PDCRT_OUT pdcrt_objeto* obj, pdcrt_alojador alojador, size_t lon)
{
    obj->tag = PDCRT_TOBJ_TEXTO;
    obj->recv = (pdcrt_funcion_generica) &pdcrt_recv_texto;
    return pdcrt_aloj_texto(&obj->value.t, alojador, lon);
}

pdcrt_error pdcrt_objeto_aloj_texto_desde_cstr(PDCRT_OUT pdcrt_objeto* obj, pdcrt_alojador alojador, const char* cstr)
{
    obj->tag = PDCRT_TOBJ_TEXTO;
    obj->recv = (pdcrt_funcion_generica) &pdcrt_recv_texto;
    return pdcrt_aloj_texto_desde_c(&obj->value.t, alojador, cstr);
}

pdcrt_objeto pdcrt_objeto_desde_arreglo(pdcrt_arreglo* arreglo)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_ARREGLO;
    obj.value.a = arreglo;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_arreglo;
    return obj;
}

pdcrt_error pdcrt_objeto_aloj_arreglo(pdcrt_alojador alojador, size_t capacidad, PDCRT_OUT pdcrt_objeto* out)
{
    out->recv = (pdcrt_funcion_generica) &pdcrt_recv_arreglo;
    out->value.a = pdcrt_alojar_simple(alojador, sizeof(pdcrt_arreglo));
    if(!out->value.a)
    {
        return PDCRT_ENOMEM;
    }
    out->tag = PDCRT_TOBJ_ARREGLO;
    pdcrt_error pderrno = pdcrt_aloj_arreglo(alojador, out->value.a, capacidad);
    if(pderrno != PDCRT_OK)
    {
        pdcrt_dealojar_simple(alojador, out->value.a, sizeof(pdcrt_arreglo));
        return pderrno;
    }
    return PDCRT_OK;
}

pdcrt_objeto pdcrt_objeto_desde_texto(pdcrt_texto* texto)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_TEXTO;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_texto;
    obj.value.t = texto;
    return obj;
}

pdcrt_error pdcrt_objeto_aloj_objeto(PDCRT_OUT pdcrt_objeto* obj, pdcrt_alojador alojador, pdcrt_recvmsj recv, size_t num_attrs)
{
    PDCRT_ASSERT(false);
    /* obj->tag = PDCRT_TOBJ_OBJETO; */
    /* obj->value.c.proc = (pdcrt_funcion_generica) recv; */
    /* obj->recv = (pdcrt_funcion_generica) &pdcrt_recv_objeto; */
    /* return pdcrt_aloj_env(&obj->value.o.attrs, alojador, num_attrs); */
    // TODO
    return PDCRT_ENOMEM;
}

pdcrt_error pdcrt_objeto_aloj_espacio_de_nombres(PDCRT_OUT pdcrt_objeto* obj, pdcrt_alojador alojador, size_t num_nombres)
{
    obj->tag = PDCRT_TOBJ_ESPACIO_DE_NOMBRES;
    obj->recv = (pdcrt_funcion_generica) &pdcrt_recv_espacio_de_nombres;
    return pdcrt_aloj_espacio_de_nombres(alojador, &obj->value.e, num_nombres);
}


// Igualdad:

// La mayoría de los tipos son fáciles de comparar (por igualdad). Sin embargo,
// los números nos presentan un problema: no todos los enteros son floats y no
// todos los floats son enteros.
//
// Específicamente, los «floats enteros» (como 3.0 o -7.0) no necesariamente
// pueden convertirse a un pdcrt_entero. Esto es debido a que los floats
// (incluso los «pequeños» de 32 bits) pueden representar enteros con
// magnitudes mucho mayores a PDCRT_ENTERO_MAX o menores a PDCRT_ENTERO_MIN.
//
// Además, como los floats pierden precisión rápidamente a medida que se alejan
// de 0, los enteros grandes tampoco pueden ser representados como floats. Por
// ejemplo, el entero 9223372036854775806L no puede ser representado como un
// float de 32 bits: es redondeado a 9223372036854775808.0f.
//
// No podemos realizar `ent == (pdcrt_entero) flt` porque `(pdcrt_entero) flt`
// podría hacer overflow, así que primero hay que verificar que `flt` se
// encuentre en el rango de los enteros. En teoría es tan sencillo como `flt >=
// (pdcrt_float) PDCRT_ENTERO_MIN && flt <= (pdcrt_float) PDCRT_ENTERO_MAX`,
// pero como ya dije ¡Algunos enteros son irrepresentables! Si PDCRT_ENTERO_MIN
// o PDCRT_ENTERO_MAX son irrepresentables entonces este condicional fallaría
// al aceptar más o menos valores de los esperados.
//
// Una solución sencilla, utilizada por Lua <http://www.lua.org> 5.4
// <http://www.lua.org/versions.html#5.4>, es la siguiente:
//
// 1. Determinamos cual es el rango de enteros que pueden ser representados por
// pdcrt_float sin pérdida de precisión.
//
// 2. Al comparar `ent` y `flt`, primero convertimos `ent` a un pdcrt_float *si
// se encuentra en este rango seguro*. Si este es el caso, comparamos con `flt
// == (pdcrt_float) ent`.
//
// 3. Si este no es el caso, tratamos de convertir `flt` a un entero mediante
// la condición anteriormente descrita (`flt >= (pdcrt_float) PDCRT_ENTERO_MIN
// && flt <= (pdcrt_float) PDCRT_ENTERO_MAX`).
//
// Lua no maneja el caso en el que PDCRT_ENTERO_MAX no pueda ser un float. En
// cambio, ellos documentan que este caso está «indefinído».
//
// Python <https://www.python.org> 3.13.0a1 (commit
// 1c7ed7e9ebc53290c831d7b610219fa737153a1b) implementa la siguiente estrategia
// (función `float_richcompare`, `Objects/floatobject.c` línea 414):
//
// 1. Si tienen signos distintos, no son iguales.
//
// 2. Si el float es NaN o ±inf, no son iguales.
//
// 3. Si el entero tiene menos bits que pdcrt_float tiene de precisión, `flt ==
// (pdcrt_float) ent`. `(pdcrt_float) ent` siempre podrá representar `ent`.
//
// Recuerda que todos los números en coma flotante son de la forma `flt === sig
// * 2**exp`. La función `frexp(3)` te permite obtener `sig` y `exp`. Si
// ninguno de los casos anteriores funciona, Python procede a desestructurar
// `flt` en sus componentes `sig` y `exp` y a compararlos con `ent`.


// Sacado de
// <https://stackoverflow.com/questions/64842669/how-to-test-if-a-target-has-twos-complement-integers-with-the-c-preprocessor>
_Static_assert((-1 & 3) == 3,
               u8"tu compilador debe implementar los enteros como números en complemento a 2");

#if !__STDC_IEC_559__ || !__STDC_IEC_60559_BFP__
#error tu compilador debe implementar float/double/long double como números IEEE-754 / IEC 60559
#endif

_Static_assert(FLT_RADIX == 2, "float/double/long double deben ser binarios");
_Static_assert(sizeof(float) == 4, "float debe ser de 32 bits");
_Static_assert(sizeof(double) == 8, "double debe ser de 64 bits");

// Determina si `t` es un tipo numérico (entero o float).
static bool pdcrt_es_numero(pdcrt_tipo_de_objeto t)
{
    return (t == PDCRT_TOBJ_ENTERO) || (t == PDCRT_TOBJ_FLOAT);
}

// La cantidad de bits en un entero.
#define PDCRT_ENTERO_BITS (sizeof(pdcrt_entero) * 8)

enum pdcrt_comparacion
{
    //                              ABC
    PDCRT_MENOR_QUE       = 1,  // 0001
    PDCRT_MENOR_O_IGUAL_A = 3,  // 0011
    PDCRT_MAYOR_QUE       = 4,  // 0100
    PDCRT_MAYOR_O_IGUAL_A = 6,  // 0110
    PDCRT_IGUAL_A         = 10  // 1010
    // El bit A es si es "mayor que", el bit B es el bit "igual a", el bit C es
    // el bit "menor que".
};

static inline bool pdcrt_es_menor_que(enum pdcrt_comparacion op)
{
    return op & 1;
}

static inline bool pdcrt_es_igual_a(enum pdcrt_comparacion op)
{
    return op & 2;
}

static inline bool pdcrt_es_mayor_que(enum pdcrt_comparacion op)
{
    return op & 4;
}

static bool pdcrt_comparar_floats(pdcrt_float a, pdcrt_float b, enum pdcrt_comparacion op)
{
    switch(op)
    {
    case PDCRT_MENOR_QUE:
        return a < b;
    case PDCRT_MENOR_O_IGUAL_A:
        return a <= b;
    case PDCRT_MAYOR_QUE:
        return a > b;
    case PDCRT_MAYOR_O_IGUAL_A:
        return a >= b;
    case PDCRT_IGUAL_A:
        return a == b;
    }
    pdcrt_inalcanzable();
}

static bool pdcrt_comparar_enteros(pdcrt_entero a, pdcrt_entero b, enum pdcrt_comparacion op)
{
    switch(op)
    {
    case PDCRT_MENOR_QUE:
        return a < b;
    case PDCRT_MENOR_O_IGUAL_A:
        return a <= b;
    case PDCRT_MAYOR_QUE:
        return a > b;
    case PDCRT_MAYOR_O_IGUAL_A:
        return a >= b;
    case PDCRT_IGUAL_A:
        return a == b;
    }
    pdcrt_inalcanzable();
}

static bool pdcrt_comparar_entero_y_float(pdcrt_entero e, pdcrt_float f, enum pdcrt_comparacion op)
{
    if(PDCRT_FLOAT_DIG_SIG >= PDCRT_ENTERO_BITS) // (1)
    {
        return pdcrt_comparar_floats((pdcrt_float) e, f, op);
    }

    if(isnan(f))
    {
        return false; // No son comparables.
    }
    else if(isinf(f))
    {
        return f > 0 ? pdcrt_es_menor_que(op) : pdcrt_es_mayor_que(op);
    }

    // Debido a (1), sabemos que PDCRT_FLOAT_DIG_SIG < PDCRT_ENTERO_BITS
    const pdcrt_entero max_entero_repr_float = (1U << PDCRT_FLOAT_DIG_SIG) - 1U;
    const pdcrt_entero min_entero_repr_float = -(1U << PDCRT_FLOAT_DIG_SIG);

    if((e >= min_entero_repr_float) && (e <= max_entero_repr_float))
    {
        return pdcrt_comparar_floats((pdcrt_float) e, f, op);
    }

    if((e < 0 && f >= 0.0) || (e <= 0 && f > 0.0))
    {
        return pdcrt_es_menor_que(op);
    }

    if((e > 0 && f <= 0.0) || (e >= 0 && f < 0.0))
    {
        return pdcrt_es_mayor_que(op);
    }

    if(e == 0 && f == 0.0)
    {
        return pdcrt_es_igual_a(op);
    }

    // Ahora sabemos que `e` y `f` tienen el mismo signo (ambos positivos o
    // ambos negativos).

    pdcrt_float f_ent, f_floor;
    f_floor = PDCRT_FLOAT_FLOOR(f);
    if(f_floor == f)
    {
        // `f` es un "float entero" (por ejemplo: 3.0)
        f_ent = f;
    }
    else
    {
        // `f` tiene una parte fraccional.
        switch(op)
        {
        case PDCRT_IGUAL_A:
            // Un float "normal" como 2.4 jamás será igual a un entero.
            return false;
        case PDCRT_MAYOR_O_IGUAL_A:
            // `e >= f` => `e >= floor(f)`
        case PDCRT_MAYOR_QUE:
            // `e > f` => `e > floor(f)`
            f_ent = f_floor;
            break;
        case PDCRT_MENOR_O_IGUAL_A:
            // `e <= f` => `e <= ceil(f)`
        case PDCRT_MENOR_QUE:
            // `e < f` => `e < ceil(f)`
            f_ent = PDCRT_FLOAT_CEIL(f);
            break;
        }
    }

    int exp = 0;
    PDCRT_FLOAT_FREXP(f_ent, &exp);
    PDCRT_ASSERT(exp > 0); // `exp > 0` significa que `f_ent` contiene un
                           // entero.

    size_t f_bits = exp;

    // Además, como es positivo, el exponente es la cantidad de bits antes
    // del punto decimal (¿punto decimal? ¿o punto binario?).
    if(f_bits > PDCRT_ENTERO_BITS)
    {
        // `f` es más grande o más pequeño que cualquier entero.
        return (f_ent > 0) ? op == PDCRT_MENOR_QUE : op == PDCRT_MAYOR_QUE;
    }
    else
    {
        PDCRT_ASSERT(f_bits <= PDCRT_ENTERO_BITS);
        // Ahora sabemos que tienen cantidades comparables de bits, hay que
        // comparar sus magnitudes.

        // Este cast es seguro (no hará overflow) ya que sabemos que f tiene la
        // misma cantidad de bits *en su parte entera*.
        return pdcrt_comparar_enteros(e, (pdcrt_entero) f_ent, op);
    }
}

bool pdcrt_objeto_iguales(pdcrt_objeto a, pdcrt_objeto b)
{
    if(a.tag != b.tag)
    {
        if(pdcrt_es_numero(a.tag) && pdcrt_es_numero(b.tag))
        {
            // Entero = Float o Float = Entero
            if(a.tag == PDCRT_TOBJ_ENTERO)
            {
                PDCRT_ASSERT(b.tag == PDCRT_TOBJ_FLOAT);
                return pdcrt_comparar_entero_y_float(a.value.i, b.value.f, PDCRT_IGUAL_A);
            }
            else
            {
                PDCRT_ASSERT(a.tag == PDCRT_TOBJ_FLOAT);
                PDCRT_ASSERT(b.tag == PDCRT_TOBJ_ENTERO);
                return pdcrt_comparar_entero_y_float(b.value.i, a.value.f, PDCRT_IGUAL_A);
            }
        }
        else
        {
            return false;
        }
    }
    switch(a.tag)
    {
    case PDCRT_TOBJ_TEXTO:
        if(a.value.t->longitud != b.value.t->longitud)
            return false;
        for(size_t i = 0; i < a.value.t->longitud; i++)
        {
            if(a.value.t->contenido[i] != b.value.t->contenido[i])
                return false;
        }
        return true;
    default:
        return pdcrt_objeto_identicos(a, b);
    }
}

bool pdcrt_objeto_identicos(pdcrt_objeto a, pdcrt_objeto b)
{
    if(a.tag != b.tag)
    {
        return false;
    }
    switch(a.tag)
    {
    case PDCRT_TOBJ_TEXTO:
        return a.value.t == b.value.t;
    case PDCRT_TOBJ_NULO:
        return true;
    case PDCRT_TOBJ_ARREGLO:
        return a.value.a == b.value.a;
    case PDCRT_TOBJ_VOIDPTR:
        return a.value.p == b.value.p;
    case PDCRT_TOBJ_ESPACIO_DE_NOMBRES:
        return a.value.e == b.value.e;
    case PDCRT_TOBJ_ENTERO:
        return a.value.i == b.value.i;
    case PDCRT_TOBJ_FLOAT:
        return a.value.f == b.value.f;
    case PDCRT_TOBJ_BOOLEANO:
        return a.value.b == b.value.b;
    case PDCRT_TOBJ_MARCA_DE_PILA:
        return true;
    case PDCRT_TOBJ_CLOSURE:
    case PDCRT_TOBJ_OBJETO:
        return (a.value.c.proc == b.value.c.proc) && (a.value.c.env == b.value.c.env);
    default:
        pdcrt_inalcanzable();
    }
}


// Hasheo:

static pdcrt_entero pdcrt_hashear_bytes(const char* bytes, size_t longitud, pdcrt_uentero n)
{
    pdcrt_uentero hash = 0;
    for(size_t i = 0; i < longitud; i++)
    {
        unsigned char c = bytes[i];
        hash = ((hash << 8) + c) * (pdcrt_uentero) n;
    }
    return hash % (pdcrt_uentero) n;
}

pdcrt_entero pdcrt_hashear_objeto(pdcrt_objeto obj, pdcrt_uentero n)
{
    switch(obj.tag)
    {
    case PDCRT_TOBJ_ENTERO:
        return obj.value.i % n;
    case PDCRT_TOBJ_FLOAT:
        if(isnan(obj.value.f))
        {
            return n / 3;
        }
        else if(isinf(obj.value.f))
        {
            return (obj.value.f < 0) ? 0 : (n - 1);
        }
        else
        {
            int exp;
            PDCRT_FLOAT_FREXP(obj.value.f, &exp);
            if(exp > 0 && ((size_t) exp) < PDCRT_ENTERO_BITS)
            {
                return ((pdcrt_entero) obj.value.f) % n;
            }
            else if(exp > 0)
            {
                return ((pdcrt_entero) PDCRT_FLOAT_FMOD(obj.value.f, (pdcrt_float) n)) % n;
            }
            else
            {
                PDCRT_ASSERT(exp <= 0);
                return pdcrt_hashear_bytes((const char*)&obj.value.f, sizeof(obj.value.f), n);
            }
        }
    case PDCRT_TOBJ_BOOLEANO:
        return obj.value.b? 0 : (n - 1);
    case PDCRT_TOBJ_NULO:
        return n / 2;
    case PDCRT_TOBJ_MARCA_DE_PILA:
        return n - 1;
    case PDCRT_TOBJ_TEXTO:
        return pdcrt_hashear_bytes(obj.value.t->contenido, obj.value.t->longitud, n);
    case PDCRT_TOBJ_VOIDPTR:
        return ((pdcrt_entero) obj.value.p) % n;
    default:
        pdcrt_inalcanzable();
    }
}


struct pdcrt_constructor_de_texto
{
    PDCRT_ARR(capacidad) char* contenido;
    size_t longitud;
    size_t capacidad;
};

static void pdcrt_inic_constructor_de_texto(PDCRT_OUT struct pdcrt_constructor_de_texto* cons, pdcrt_alojador alojador, size_t capacidad)
{
    cons->longitud = 0;
    cons->capacidad = capacidad;
    cons->contenido = NULL;
    if(cons->capacidad > 0)
    {
        cons->contenido = pdcrt_alojar_simple(alojador, sizeof(char) * cons->capacidad);
        if(cons->contenido == NULL)
        {
            PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, __func__);
            no_falla(PDCRT_ENOMEM);
        }
    }
}

static void pdcrt_constructor_agregar(pdcrt_alojador alojador, struct pdcrt_constructor_de_texto* cons, char* contenido, size_t longitud)
{
    if((cons->longitud + longitud) >= cons->capacidad)
    {
        size_t nueva_cap = pdcrt_siguiente_capacidad(cons->capacidad, cons->longitud, longitud);
        char* nuevo = pdcrt_realojar_simple(alojador, cons->contenido, cons->capacidad * sizeof(char), nueva_cap * sizeof(char));
        if(nuevo == NULL)
        {
            PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, __func__);
            no_falla(PDCRT_ENOMEM);
        }
        cons->contenido = nuevo;
        cons->capacidad = nueva_cap;
    }
    memcpy(cons->contenido + cons->longitud, contenido, longitud);
    cons->longitud += longitud;
    PDCRT_ASSERT(cons->longitud <= cons->capacidad);
}

static void pdcrt_finalizar_constructor(pdcrt_alojador alojador, struct pdcrt_constructor_de_texto* cons, PDCRT_OUT pdcrt_texto** res)
{
    no_falla(pdcrt_aloj_texto(res, alojador, cons->longitud));
    if((*res)->contenido == NULL || cons->contenido == NULL)
    {
        PDCRT_ASSERT(cons->longitud == 0);
    }
    else
    {
        PDCRT_ASSERT(cons->longitud > 0);
        memcpy((*res)->contenido, cons->contenido, cons->longitud);
    }
}

static void pdcrt_deainic_constructor_de_texto(pdcrt_alojador alojador, struct pdcrt_constructor_de_texto* cons)
{
    pdcrt_dealojar_simple(alojador, cons->contenido, cons->capacidad * sizeof(char));
    cons->contenido = NULL;
    cons->capacidad = 0;
    cons->longitud = 0;
}


// Receptores:

static int pdcrt_texto_cmp_lit(pdcrt_texto* lhs, const char* rhs)
{
    // FIXME: El siguiente strlen itera sobre todo rhs lo cual realentiza este
    // caso incluso si sus tamaños son distintos. La solución sería unificar el
    // bucle de memcmp y el de strlen.
    size_t rhslon = strlen(rhs);
    if(lhs->longitud != rhslon)
    {
        return lhs->longitud - rhslon;
    }
    else
    {
        return memcmp(lhs->contenido, rhs, lhs->longitud);
    }
}

static void pdcrt_necesita_args_y_rets(int args, int rets, int eargs, int erets)
{
    if(args != eargs || rets != erets)
    {
        fprintf(stderr,
                "Error: Se esperaban %d argumentos y %d valores devueltos, pero se obtuvieron %d argumentos y %d valores devueltos\n",
                eargs, erets, args, rets);
        pdcrt_abort();
    }
}

static void pdcrt_ajustar_argumentos_para_c(pdcrt_contexto* ctx, int args, int params)
{
    if(args < params)
    {
        for(int i = args; i < params; i++)
        {
            no_falla(pdcrt_empujar_en_pila(&ctx->pila, ctx->alojador, pdcrt_objeto_nulo()));
        }
    }
    else if(args > params)
    {
        for(int i = args; i > params; i--)
        {
            (void) pdcrt_sacar_de_pila(&ctx->pila);
        }
    }
}

static void pdcrt_ajustar_valores_devueltos_para_c(pdcrt_contexto* ctx, int esperados, int devueltos)
{
    if(esperados < devueltos)
    {
        for(int i = esperados; i < devueltos; i++)
        {
            (void) pdcrt_sacar_de_pila(&ctx->pila);
        }
    }
    else if(esperados > devueltos)
    {
        for(int i = esperados; i > devueltos; i--)
        {
            no_falla(pdcrt_empujar_en_pila(&ctx->pila, ctx->alojador, pdcrt_objeto_nulo()));
        }
    }
}

static pdcrt_objeto pdcrt_closure_desde_callback_del_runtime(pdcrt_marco* marco, pdcrt_proc_t cb)
{
    pdcrt_objeto_debe_tener_closure(marco, marco->contexto->entornoBootstrap);
    pdcrt_objeto clz;
    clz.tag = PDCRT_TOBJ_CLOSURE;
    clz.value.c.proc = (pdcrt_funcion_generica) cb;
    clz.recv = (pdcrt_funcion_generica) &pdcrt_recv_closure;
    clz.value.c.env = marco->contexto->entornoBootstrap.value.c.env;
    return clz;
}

static enum pdcrt_comparacion pdcrt_invertir_comparacion(enum pdcrt_comparacion op)
{
    switch(op)
    {
    case PDCRT_MENOR_QUE:
        return PDCRT_MAYOR_O_IGUAL_A;
    case PDCRT_MENOR_O_IGUAL_A:
        return PDCRT_MAYOR_QUE;
    case PDCRT_MAYOR_QUE:
        return PDCRT_MENOR_O_IGUAL_A;
    case PDCRT_MAYOR_O_IGUAL_A:
        return PDCRT_MENOR_QUE;
    case PDCRT_IGUAL_A:
        return PDCRT_IGUAL_A;
    }
    pdcrt_inalcanzable();
}

static void pdcrt_comparar_numeros(pdcrt_marco* marco, pdcrt_objeto yo, int args, int rets, enum pdcrt_comparacion op)
{
    enum pdcrt_comparacion rop = pdcrt_invertir_comparacion(op);
    pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
    pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_uno_de_los_tipos(marco, rhs, PDCRT_TOBJ_ENTERO, PDCRT_TOBJ_FLOAT);
    switch(yo.tag)
    {
    case PDCRT_TOBJ_ENTERO:
        switch(rhs.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_comparar_enteros(yo.value.i, rhs.value.i, op))));
            break;
        case PDCRT_TOBJ_FLOAT:
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_comparar_entero_y_float(yo.value.i, rhs.value.f, op))));
            break;
        default:
            pdcrt_inalcanzable();
        }
        break;
    case PDCRT_TOBJ_FLOAT:
        switch(rhs.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_comparar_entero_y_float(rhs.value.i, yo.value.f, rop))));
            break;
        case PDCRT_TOBJ_FLOAT:
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_comparar_floats(yo.value.f, rhs.value.f, op))));
            break;
        default:
            pdcrt_inalcanzable();
        }
        break;
    default:
        pdcrt_inalcanzable();
    }
    pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
}

pdcrt_continuacion pdcrt_recv_numero(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);

#define PDCRT_NUMOP(op, ffn, efn)                                       \
    do                                                                  \
    {                                                                   \
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);      \
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila); \
        pdcrt_objeto_debe_tener_uno_de_los_tipos(marco, rhs, PDCRT_TOBJ_ENTERO, PDCRT_TOBJ_FLOAT); \
        switch(rhs.tag)                                                 \
        {                                                               \
        case PDCRT_TOBJ_ENTERO:                                         \
            switch(yo.tag)                                              \
            {                                                           \
            case PDCRT_TOBJ_ENTERO:                                     \
                no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, efn(yo.value.i op rhs.value.i))); \
                break;                                                  \
            case PDCRT_TOBJ_FLOAT:                                      \
                no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, ffn(yo.value.f op ((pdcrt_float)rhs.value.i)))); \
                break;                                                  \
            default:                                                    \
                pdcrt_inalcanzable();                                   \
            }                                                           \
            break;                                                      \
        case PDCRT_TOBJ_FLOAT:                                          \
            switch(yo.tag)                                              \
            {                                                           \
            case PDCRT_TOBJ_ENTERO:                                     \
                no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, ffn(((pdcrt_float)yo.value.i) op rhs.value.f))); \
                break;                                                  \
            case PDCRT_TOBJ_FLOAT:                                      \
                no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, ffn(yo.value.f op rhs.value.f))); \
                break;                                                  \
            default:                                                    \
                pdcrt_inalcanzable();                                   \
            }                                                           \
            break;                                                      \
        default:                                                        \
            pdcrt_inalcanzable();                                       \
        }                                                               \
    } while(0)

    if(pdcrt_texto_cmp_lit(msj.value.t, "operador_+") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "sumar") == 0)
    {
        PDCRT_NUMOP(+, pdcrt_objeto_float, pdcrt_objeto_entero);
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_-") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "restar") == 0)
    {
        PDCRT_NUMOP(-, pdcrt_objeto_float, pdcrt_objeto_entero);
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_*") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "multiplicar") == 0)
    {
        PDCRT_NUMOP(*, pdcrt_objeto_float, pdcrt_objeto_entero);
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_/") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "dividir") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_uno_de_los_tipos(marco, rhs, PDCRT_TOBJ_ENTERO, PDCRT_TOBJ_FLOAT);
        pdcrt_float flhs, frhs;
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            flhs = yo.value.i;
            break;
        case PDCRT_TOBJ_FLOAT:
            flhs = yo.value.f;
            break;
        default:
            pdcrt_inalcanzable();
        }
        switch(rhs.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            frhs = rhs.value.i;
            break;
        case PDCRT_TOBJ_FLOAT:
            frhs = rhs.value.f;
            break;
        default:
            pdcrt_inalcanzable();
        }
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_float(flhs / frhs)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_<") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "menorQue") == 0)
    {
        pdcrt_comparar_numeros(marco, yo, args, rets, PDCRT_MENOR_QUE);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_>") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "mayorQue") == 0)
    {
        pdcrt_comparar_numeros(marco, yo, args, rets, PDCRT_MAYOR_QUE);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_=<") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "menorOIgualA") == 0)
    {
        pdcrt_comparar_numeros(marco, yo, args, rets, PDCRT_MENOR_O_IGUAL_A);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_>=") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "mayorOIgualA") == 0)
    {
        pdcrt_comparar_numeros(marco, yo, args, rets, PDCRT_MAYOR_O_IGUAL_A);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
#define PDCRT_LONGITUD_BUFFER 60
        char buffer[PDCRT_LONGITUD_BUFFER];
        memset(buffer, '\0', PDCRT_LONGITUD_BUFFER);
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            snprintf(buffer, PDCRT_LONGITUD_BUFFER, PDCRT_ENTERO_FMT, yo.value.i);
            break;
        case PDCRT_TOBJ_FLOAT:
            snprintf(buffer, PDCRT_LONGITUD_BUFFER, PDCRT_FLOAT_FMT, yo.value.f);
            break;
        default:
            pdcrt_inalcanzable();
        }
        size_t lonbuff = strlen(buffer);
        pdcrt_objeto res;
        no_falla(pdcrt_objeto_aloj_texto(&res, marco->contexto->alojador, lonbuff));
        memcpy(res.value.t->contenido, buffer, lonbuff);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, res));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
#undef PDCRT_LONGITUD_BUFFER
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "negar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(-yo.value.i)));
            break;
        case PDCRT_TOBJ_FLOAT:
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_float(-yo.value.f)));
            break;
        default:
            pdcrt_inalcanzable();
        }
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "clonar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "igualA") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, u8"distíntoDe") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_no=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(!pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "truncar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        pdcrt_entero r;
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            r = yo.value.i;
            break;
        case PDCRT_TOBJ_FLOAT:
            r = yo.value.f;
            break;
        default:
            pdcrt_inalcanzable();
        }
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(r)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "piso") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        pdcrt_float r;
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            r = yo.value.i;
            break;
        case PDCRT_TOBJ_FLOAT:
            r = yo.value.f;
            break;
        default:
            pdcrt_inalcanzable();
        }
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(floor(r))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "techo") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        pdcrt_float r;
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            r = yo.value.i;
            break;
        case PDCRT_TOBJ_FLOAT:
            r = yo.value.f;
            break;
        default:
            pdcrt_inalcanzable();
        }
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(ceil(r))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoByteEnTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        pdcrt_objeto texto;
        no_falla(pdcrt_objeto_aloj_texto(&texto, marco->contexto->alojador, 1));
        char c;
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            c = yo.value.i;
            break;
        case PDCRT_TOBJ_FLOAT:
            c = (pdcrt_entero) yo.value.f;
            break;
        default:
            pdcrt_inalcanzable();
        }
        texto.value.t->contenido[0] = c;
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, texto));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "hashPara") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto on = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo(on, PDCRT_TOBJ_ENTERO);
        pdcrt_uentero n = on.value.i;
        pdcrt_entero hash = pdcrt_hashear_objeto(yo, n);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(hash)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else
    {
        printf("Mensaje ");
        pdcrt_escribir_texto(msj.value.t);
        printf(" no entendido para el número ");
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            printf(PDCRT_ENTERO_FMT, yo.value.i);
            break;
        case PDCRT_TOBJ_FLOAT:
            printf(PDCRT_FLOAT_FMT, yo.value.f);
            break;
        default:
            pdcrt_inalcanzable();
        }
        printf("\n");
        pdcrt_abort();
    }

    return pdcrt_continuacion_devolver();
#undef PDCRT_NUMOP
}

pdcrt_continuacion pdcrt_recv_texto(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);
    if(pdcrt_texto_cmp_lit(msj.value.t, "longitud") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(yo.value.t->longitud)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "clonar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "igualA") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, u8"distíntoDe") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_no=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(!pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoNumeroEntero") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        char* buff = pdcrt_alojar_simple(marco->contexto->alojador, sizeof(char) * (yo.value.t->longitud + 1));
        if(buff == NULL)
        {
            PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, "Texto#comoNumeroEntero: alojando buffer temporal");
            no_falla(PDCRT_ENOMEM);
        }
        memcpy(buff, yo.value.t->contenido, yo.value.t->longitud);
        buff[yo.value.t->longitud] = '\0';
        errno = 0;
        long r = strtol(buff, NULL, 10);
        if(errno != 0)
        {
            perror("strtol");
            pdcrt_abort();
        }
        _Static_assert(sizeof(r) <= sizeof(pdcrt_entero),
                       "necesito almacenar el resultado de strtol() en un entero");
        pdcrt_dealojar_simple(marco->contexto->alojador, buff, sizeof(char) * (yo.value.t->longitud + 1));
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(r)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoNumeroReal") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        char* buff = pdcrt_alojar_simple(marco->contexto->alojador, sizeof(char) * (yo.value.t->longitud + 1));
        if(buff == NULL)
        {
            PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, "Texto#comoNumeroEntero: alojando buffer temporal");
            no_falla(PDCRT_ENOMEM);
        }
        memcpy(buff, yo.value.t->contenido, yo.value.t->longitud);
        buff[yo.value.t->longitud] = '\0';
        errno = 0;
        long double r = strtold(buff, NULL);
        if(errno != 0)
        {
            perror("strtold");
            pdcrt_abort();
        }
        pdcrt_dealojar_simple(marco->contexto->alojador, buff, sizeof(char) * (yo.value.t->longitud + 1));
        // NOTE: Estamos procesando un long double, pdcrt_float puede ser
        // cualquier tipo de coma flotante (float, double, long double).
        //
        // En el estándar de C no hay tipos de coma flotante mayores que long
        // double así que la conversión implícita en pdcrt_objeto_float()
        // siempre tendrá buena precisión.
        //
        // Sin embargo, es posible que el compilador exponga un tipo no
        // estándar de mayor precisión que long double, ¡En ese caso la
        // siguiente conversión *perdería precisión*!
        //
        // Este _Static_assert es muy simple: mientras tengamos más bits de
        // precisión en long double que en pdcrt_float, entonces la converción
        // nunca perderá dígitos con respecto al texto original (ya que leímos
        // más datos de los que vamos a usar).
        //
        // Si estuviésemos en C++, lo ideal sería algo así como
        // parse_float<pdcrt_float>(std::string). Entonces el template leería
        // el texto con la mayor precisión posible.
        //
        // Posdata: cuando hablo de ganar o perder precisión, me refiero con
        // respecto al texto original que contenía la representación del
        // real. Queremos mantener la mayor cantidad de dígitos posible del
        // texto original.
        _Static_assert(sizeof(r) >= sizeof(pdcrt_float),
                       "pdcrt_float de muy alta precision: vease comentario en el codigo");
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_float(r)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "en") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, obj, PDCRT_TOBJ_ENTERO);
        pdcrt_entero i = obj.value.i;
        if((i < 0) || (((size_t) i) >= yo.value.t->longitud))
        {
            fprintf(stderr,
                    u8"Error: índice " PDCRT_ENTERO_FMT " fuera del rango válido para indexar al texto (rango válido: desde 0 hasta %zd). Texto: «",
                    i, yo.value.t->longitud);
            pdcrt_escribir_texto_max(yo.value.t, 30);
            fprintf(stderr, u8"»");
            if(yo.value.t->longitud >= 30)
            {
                fprintf(stderr, "...");
            }
            fprintf(stderr, "\n");
            pdcrt_abort();
        }
        pdcrt_texto* texto;
        no_falla(pdcrt_aloj_texto(&texto, marco->contexto->alojador, 1));
        texto->contenido[0] = yo.value.t->contenido[i];
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_desde_texto(texto)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "concatenar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, obj, PDCRT_TOBJ_TEXTO);
        pdcrt_texto* res;
        no_falla(pdcrt_aloj_texto(&res, marco->contexto->alojador, obj.value.t->longitud + yo.value.t->longitud));
        memcpy(res->contenido, yo.value.t->contenido, yo.value.t->longitud);
        memcpy(res->contenido + yo.value.t->longitud, obj.value.t->contenido, obj.value.t->longitud);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_desde_texto(res)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "parteDelTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_objeto ofinal = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto oinic = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, oinic, PDCRT_TOBJ_ENTERO);
        pdcrt_objeto_debe_tener_tipo_tb(marco, ofinal, PDCRT_TOBJ_ENTERO);
        pdcrt_entero inic = oinic.value.i, final = ofinal.value.i;
        PDCRT_ASSERT(inic >= 0);
        PDCRT_ASSERT(final >= 0);
        pdcrt_texto* res;
        if((final <= inic) || (((size_t) inic) >= yo.value.t->longitud))
        {
            no_falla(pdcrt_aloj_texto_desde_c(&res, marco->contexto->alojador, ""));
        }
        else
        {
            if(((size_t) final) > yo.value.t->longitud)
            {
                final = yo.value.t->longitud;
            }
            no_falla(pdcrt_aloj_texto(&res, marco->contexto->alojador, final - inic));
            memcpy(res->contenido, yo.value.t->contenido + inic, final - inic);
        }
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_desde_texto(res)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "subTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_objeto olon = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto oinic = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, oinic, PDCRT_TOBJ_ENTERO);
        pdcrt_objeto_debe_tener_tipo_tb(marco, olon, PDCRT_TOBJ_ENTERO);
        pdcrt_entero inic = oinic.value.i, lon = olon.value.i;
        PDCRT_ASSERT(inic >= 0);
        PDCRT_ASSERT(lon >= 0);
        if(((size_t) inic) > yo.value.t->longitud)
        {
            inic = yo.value.t->longitud;
            lon = 0;
        }
        if(((size_t) (inic + lon)) > yo.value.t->longitud)
        {
            lon = yo.value.t->longitud - inic;
        }
        pdcrt_texto* res;
        no_falla(pdcrt_aloj_texto(&res, marco->contexto->alojador, lon));
        memcpy(res->contenido, yo.value.t->contenido + inic, lon);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_desde_texto(res)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "buscar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_objeto otxt = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto oinic = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, otxt, PDCRT_TOBJ_TEXTO);
        pdcrt_objeto_debe_tener_tipo_tb(marco, oinic, PDCRT_TOBJ_ENTERO);
        size_t inic = oinic.value.i, pos = 0;
        bool encontrado = false;
        if((otxt.value.t->longitud > yo.value.t->longitud) || (inic >= yo.value.t->longitud))
        {
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_nulo()));
        }
        else if(otxt.value.t->longitud == 0)
        {
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(inic)));
        }
        else
        {
            for(size_t i = inic; i < (yo.value.t->longitud - (otxt.value.t->longitud - 1)); i++)
            {
                size_t j = 0;
                for(; j < otxt.value.t->longitud; j++)
                {
                    if(yo.value.t->contenido[i + j] != otxt.value.t->contenido[j])
                    {
                        break;
                    }
                }
                if(j == otxt.value.t->longitud)
                {
                    encontrado = true;
                    pos = i;
                    break;
                }
            }

            pdcrt_objeto res;
            if(encontrado)
            {
                res = pdcrt_objeto_entero(pos);
            }
            else
            {
                res = pdcrt_objeto_nulo();
            }
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, res));
        }
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "buscarEnReversa") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_no_implementado("Texto#buscarEnReversa");
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "formatear") == 0)
    {
        pdcrt_insertar_elemento_en_pila(&marco->contexto->pila, marco->contexto->alojador, args, yo);
        return pdcrt_continuacion_tail_enviar_mensaje(marco,
                                                      pdcrt_closure_desde_callback_del_runtime(marco, &pdcrt_frt_texto_formatear),
                                                      pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar),
                                                      args + 1,
                                                      rets);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "byteEn") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, obj, PDCRT_TOBJ_ENTERO);
        pdcrt_entero i = obj.value.i;
        if((i < 0) || (((size_t) i) >= yo.value.t->longitud))
        {
            fprintf(stderr,
                    u8"Error: índice " PDCRT_ENTERO_FMT " fuera del rango válido para indexar al texto (rango válido: desde 0 hasta %zd). Texto: «",
                    i, yo.value.t->longitud);
            pdcrt_escribir_texto_max(yo.value.t, 30);
            fprintf(stderr, u8"»");
            if(yo.value.t->longitud >= 30)
            {
                fprintf(stderr, "...");
            }
            fprintf(stderr, "\n");
            pdcrt_abort();
        }
        int c = yo.value.t->contenido[i];
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(c)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "hashPara") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto on = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo(on, PDCRT_TOBJ_ENTERO);
        pdcrt_uentero n = on.value.i;
        pdcrt_entero hash = pdcrt_hashear_objeto(yo, n);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(hash)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else
    {
        printf("Mensaje ");
        pdcrt_escribir_texto(msj.value.t);
        printf(" no entendido para el texto «");
        pdcrt_escribir_texto_max(yo.value.t, 30);
        printf("»");
        if(yo.value.t->longitud > 30)
        {
            printf("...");
        }
        printf("\n");
        pdcrt_abort();
    }

    return pdcrt_continuacion_devolver();
}

pdcrt_continuacion pdcrt_recv_closure(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);
    if(pdcrt_texto_cmp_lit(msj.value.t, "llamar") == 0)
    {
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        return pdcrt_continuacion_tail_iniciar((pdcrt_proc_t) yo.value.c.proc, marco, args + 1, rets);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "igualA") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, u8"distíntoDe") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_no=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(!pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoObjeto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        yo.tag = PDCRT_TOBJ_OBJETO;
        yo.recv = (pdcrt_funcion_generica) &pdcrt_recv_objeto;
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, u8"__códigoIgualA") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo(rhs, PDCRT_TOBJ_CLOSURE);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(yo.value.c.proc == rhs.value.c.proc)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "__entornoIgualA") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo(rhs, PDCRT_TOBJ_CLOSURE);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(yo.value.c.env == rhs.value.c.env)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "clonar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        char* texto = malloc(128);
        snprintf(texto, 127,
                 u8"(Procedimiento proc: 0x%zX  env: 0x%zX #%zd)",
                 (intptr_t) yo.value.c.proc,
                 (intptr_t) yo.value.c.env,
                 yo.value.c.env->env_size);
        pdcrt_objeto res;
        no_falla(pdcrt_objeto_aloj_texto_desde_cstr(&res, marco->contexto->alojador, texto));
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, res));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else
    {
        printf("Mensaje ");
        pdcrt_escribir_texto(msj.value.t);
        printf(" no entendido para la closure ");
        printf(u8"(Procedimiento proc: 0x%zX  env: 0x%zX #%zd)\n",
               (intptr_t) yo.value.c.proc,
               (intptr_t) yo.value.c.env,
               yo.value.c.env->env_size);
        pdcrt_abort();
    }

    return pdcrt_continuacion_devolver();
}

pdcrt_continuacion pdcrt_recv_marca_de_pila(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    (void) marco;
    (void) yo;
    (void) msj;
    (void) args;
    (void) rets;
    fprintf(stderr, u8"Error: se trató de enviar un mensaje a una marca de pila.\n");
    pdcrt_notifica_error_interno();
    pdcrt_abort();
}

pdcrt_continuacion pdcrt_recv_booleano(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);
    if(pdcrt_texto_cmp_lit(msj.value.t, "comoTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        pdcrt_texto* texto = NULL;
        if(yo.value.b)
        {
            texto = marco->contexto->constantes.txt_verdadero;
        }
        else
        {
            texto = marco->contexto->constantes.txt_falso;
        }
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_desde_texto(texto)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "igualA") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, u8"distíntoDe") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_no=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(!pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "clonar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "escojer") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_objeto a = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto b = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto res = yo.value.b? a : b;
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, res));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "llamarSegun") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_objeto a = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto b = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto res = yo.value.b? a : b;
        pdcrt_objeto llamar_msj = pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar);
        return pdcrt_continuacion_tail_enviar_mensaje(marco, res, llamar_msj, 0, rets);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "y") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_&&") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_objeto otro = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, otro, PDCRT_TOBJ_BOOLEANO);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(yo.value.b && otro.value.b)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "o") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_||") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_objeto otro = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, otro, PDCRT_TOBJ_BOOLEANO);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(yo.value.b || otro.value.b)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "hashPara") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto on = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo(on, PDCRT_TOBJ_ENTERO);
        pdcrt_uentero n = on.value.i;
        pdcrt_entero hash = pdcrt_hashear_objeto(yo, n);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(hash)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else
    {
        printf("Mensaje ");
        pdcrt_escribir_texto(msj.value.t);
        printf(" no entendido para el booleano %s\n", yo.value.b? "VERDADERO" : "FALSO");
        pdcrt_abort();
    }
    return pdcrt_continuacion_devolver();
}

pdcrt_continuacion pdcrt_recv_nulo(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);
    if(pdcrt_texto_cmp_lit(msj.value.t, "comoTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila,
                                       marco->contexto->alojador,
                                       pdcrt_objeto_desde_texto(marco->contexto->constantes.txt_nulo)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "clonar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "igualA") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, u8"distíntoDe") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_no=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(!pdcrt_objeto_iguales(yo, rhs))));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "hashPara") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto on = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo(on, PDCRT_TOBJ_ENTERO);
        pdcrt_uentero n = on.value.i;
        pdcrt_entero hash = pdcrt_hashear_objeto(yo, n);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(hash)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else
    {
        printf("Mensaje ");
        pdcrt_escribir_texto(msj.value.t);
        printf(" no entendido para NULO (instancia de TipoNulo)\n");
        pdcrt_abort();
    }
    return pdcrt_continuacion_devolver();
}

pdcrt_continuacion pdcrt_recv_objeto(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, yo, PDCRT_TOBJ_OBJETO);
    pdcrt_insertar_elemento_en_pila(&marco->contexto->pila, marco->contexto->alojador, args, msj);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
    return pdcrt_continuacion_tail_iniciar((pdcrt_proc_t) yo.value.c.proc, marco, args + 2, rets);
}

pdcrt_continuacion pdcrt_recv_arreglo(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, yo, PDCRT_TOBJ_ARREGLO);
    pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);
    if(pdcrt_texto_cmp_lit(msj.value.t, "agregarAlFinal") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto el = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_arreglo_agregar_al_final(marco->contexto->alojador, yo.value.a, el));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 0);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "longitud") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        // FIXME: Overflow
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(yo.value.a->longitud)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        return pdcrt_continuacion_tail_enviar_mensaje(marco,
                                                      pdcrt_closure_desde_callback_del_runtime(marco, &pdcrt_frt_arreglo_como_texto),
                                                      pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar),
                                                      1,
                                                      rets);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "en") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto obj_indice = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, obj_indice, PDCRT_TOBJ_ENTERO);
        PDCRT_ASSERT(obj_indice.value.i >= 0);
        size_t indice = obj_indice.value.i;
        PDCRT_ASSERT(indice < yo.value.a->longitud);
        pdcrt_objeto elemento = yo.value.a->elementos[indice];
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, elemento));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "fijarEn") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_objeto obj_valor = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto obj_indice = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, obj_indice, PDCRT_TOBJ_ENTERO);
        PDCRT_ASSERT(obj_indice.value.i >= 0);
        size_t indice = obj_indice.value.i;
        PDCRT_ASSERT(indice < yo.value.a->longitud);
        yo.value.a->elementos[indice] = obj_valor;
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 0);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "redimensionar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto nueva_longitud_obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, nueva_longitud_obj, PDCRT_TOBJ_ENTERO);
        PDCRT_ASSERT(nueva_longitud_obj.value.i >= 0);
        size_t nueva_longitud = nueva_longitud_obj.value.i;
        no_falla(pdcrt_arreglo_redimensionar(marco->contexto->alojador, yo.value.a, nueva_longitud));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 0);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "mapear") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_insertar_elemento_en_pila(&marco->contexto->pila, marco->contexto->alojador, 1, yo);
        return pdcrt_continuacion_tail_enviar_mensaje(marco,
                                                      pdcrt_closure_desde_callback_del_runtime(marco, &pdcrt_frt_arreglo_mapear),
                                                      pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar),
                                                      2,
                                                      rets);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "clonar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        return pdcrt_continuacion_tail_enviar_mensaje(marco,
                                                      pdcrt_closure_desde_callback_del_runtime(marco, &pdcrt_frt_clonar_arreglo),
                                                      pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar),
                                                      1,
                                                      rets);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "igualA") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto otro = pdcrt_sacar_de_pila(&marco->contexto->pila);
        if(otro.tag != PDCRT_TOBJ_ARREGLO)
        {
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(false)));
            pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
            return pdcrt_continuacion_devolver();
        }
        else
        {
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, otro));
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
            return pdcrt_continuacion_tail_enviar_mensaje(marco,
                                                          pdcrt_closure_desde_callback_del_runtime(marco, &pdcrt_frt_arreglo_igual_a),
                                                          pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar),
                                                          2,
                                                          rets);
        }
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, u8"distíntoDe") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_no=") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto otro = pdcrt_sacar_de_pila(&marco->contexto->pila);
        if(otro.tag != PDCRT_TOBJ_ARREGLO)
        {
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(true)));
            pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
            return pdcrt_continuacion_devolver();
        }
        else
        {
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, otro));
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
            return pdcrt_continuacion_tail_enviar_mensaje(marco,
                                                          pdcrt_closure_desde_callback_del_runtime(marco, &pdcrt_frt_arreglo_distinto_de),
                                                          pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar),
                                                          2,
                                                          rets);
        }
    }
    else
    {
        printf("Mensaje ");
        pdcrt_escribir_texto(msj.value.t);
        printf(" no entendido para el arreglo\n");
        pdcrt_abort();
    }
    return pdcrt_continuacion_devolver();
}

pdcrt_continuacion pdcrt_recv_espacio_de_nombres(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, yo, PDCRT_TOBJ_ESPACIO_DE_NOMBRES);
    pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);
    pdcrt_edn_triple* encontrado = NULL;
    for(size_t i = 0; i < yo.value.e->num_nombres; i++)
    {
        pdcrt_edn_triple* triple = &yo.value.e->nombres[i];
        if(pdcrt_textos_son_iguales(triple->nombre, msj.value.t))
        {
            encontrado = triple;
            break;
        }
    }
    if(!encontrado)
    {
        pdcrt_escribir_texto(msj.value.t);
        printf(" no existe en el espacio de nombres %p.\nEste espacio contiene los nombres:\n", (void*) yo.value.e);
        for(size_t i = 0; i < yo.value.e->num_nombres; i++)
        {
            pdcrt_edn_triple* triple = &yo.value.e->nombres[i];
            printf("  - ");
            if(triple->es_autoejecutable)
            {
                printf("autoejecutable ");
            }
            pdcrt_escribir_texto(triple->nombre);
            printf("\n");
        }
        pdcrt_abort();
    }
    else if(encontrado->es_autoejecutable)
    {
        pdcrt_objeto llamar = pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar);
        return pdcrt_continuacion_tail_enviar_mensaje(marco, encontrado->valor, llamar, args, rets);
    }
    else if(args != 0 || (rets != 0 && rets != 1))
    {
        fprintf(stderr,
                "Error: Se esperaban 0 argumentos y 1 valor devuelto, pero se obtuvieron %d argumentos y %d valores devueltos\n",
                args, rets);
        pdcrt_abort();
    }
    else
    {
        if(rets == 1)
        {
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, encontrado->valor));
        }
        return pdcrt_continuacion_devolver();
    }
}

pdcrt_continuacion pdcrt_recv_voidptr(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    (void) marco;
    (void) args;
    (void) rets;
    pdcrt_objeto_debe_tener_tipo_tb(marco, yo, PDCRT_TOBJ_VOIDPTR);
    printf("Mensaje ");
    pdcrt_escribir_texto(msj.value.t);
    printf(" no entendido para el puntero a C %p\n", yo.value.p);
    pdcrt_abort();
}



// Pila:

pdcrt_error pdcrt_inic_pila(pdcrt_pila* pila, pdcrt_alojador alojador)
{
    pila->capacidad = 1;
    pila->num_elementos = 0;
    pila->elementos = pdcrt_alojar_simple(alojador, sizeof(pdcrt_objeto) * pila->capacidad);
    if(!pila->elementos)
    {
        pila->capacidad = 0;
        PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, __func__);
        return PDCRT_ENOMEM;
    }
    else
    {
        return PDCRT_OK;
    }
}

void pdcrt_deinic_pila(pdcrt_pila* pila, pdcrt_alojador alojador)
{
    pila->capacidad = 0;
    pila->num_elementos = 0;
    pdcrt_dealojar_simple(alojador, pila->elementos, sizeof(pdcrt_objeto) * pila->capacidad);
}

pdcrt_error pdcrt_empujar_en_pila(pdcrt_pila* pila, pdcrt_alojador alojador, pdcrt_objeto val)
{
    if(pila->num_elementos >= pila->capacidad)
    {
        size_t nuevacap = pdcrt_siguiente_capacidad(pila->capacidad, pila->num_elementos, 1);
        pdcrt_objeto* nuevosels = pdcrt_realojar_simple(alojador, pila->elementos, pila->capacidad * sizeof(pdcrt_objeto), nuevacap * sizeof(pdcrt_objeto));
        if(!nuevosels)
        {
            PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, __func__);
            return PDCRT_ENOMEM;
        }
        else
        {
            pila->elementos = nuevosels;
            pila->capacidad = nuevacap;
        }
    }
    PDCRT_ASSERT(pila->num_elementos < pila->capacidad);
    pila->elementos[pila->num_elementos++] = val;
    return PDCRT_OK;
}

pdcrt_objeto pdcrt_sacar_de_pila(pdcrt_pila* pila)
{
    PDCRT_ASSERT(pila->num_elementos > 0);
    return pila->elementos[--pila->num_elementos];
}

pdcrt_objeto pdcrt_cima_de_pila(pdcrt_pila* pila)
{
    PDCRT_ASSERT(pila->num_elementos > 0);
    return pila->elementos[pila->num_elementos - 1];
}

pdcrt_objeto pdcrt_elemento_de_pila(pdcrt_pila* pila, size_t n)
{
    PDCRT_ASSERT(pila->num_elementos > n);
    return pila->elementos[pila->num_elementos - (n + 1)];
}

pdcrt_objeto pdcrt_eliminar_elemento_en_pila(pdcrt_pila* pila, size_t n)
{
    PDCRT_ASSERT(pila->num_elementos > n);
    size_t I = pila->num_elementos - n - 1;
    pdcrt_objeto r = pila->elementos[I];
    for(size_t i = I; i < (pila->num_elementos - 1); i++)
    {
        pila->elementos[i] = pila->elementos[i + 1];
    }
    pila->num_elementos -= 1;
    return r;
}

void pdcrt_insertar_elemento_en_pila(pdcrt_pila* pila, pdcrt_alojador alojador, size_t n, pdcrt_objeto obj)
{
    no_falla(pdcrt_empujar_en_pila(pila, alojador, pdcrt_objeto_nulo()));
    size_t I = pila->num_elementos - n - 1;
    for(size_t i = pila->num_elementos - 1; i > I; i--)
    {
        pila->elementos[i] = pila->elementos[i - 1];
    }
    pila->elementos[I] = obj;
}


// Constantes:

#define PDCRT_TABLA(M)                                              \
    M(operador_mas, "operador_+");                                  \
    M(operador_menos, "operador_-");                                \
    M(operador_por, "operador_*");                                  \
    M(operador_entre, "operador_/");                                \
    M(operador_menorQue, "operador_<");                             \
    M(operador_mayorQue, "operador_>");                             \
    M(operador_menorOIgualA, "operador_=<");                        \
    M(operador_mayorOIgualA, "operador_>=");                        \
    M(operador_igualA, "operador_=");                               \
    M(operador_noIgualA, "operador_no=");                           \
    M(msj_igualA, "igualA");                                        \
    M(msj_distintoDe, u8"distíntoDe");                              \
    M(msj_clonar, "clonar");                                        \
    M(msj_llamar, "llamar");                                        \
    M(msj_comoTexto, "comoTexto");                                  \
    M(msj_mapear, "mapear");                                        \
    M(msj_reducir, "reducir");                                      \
    M(msj_argc, "argc");                                            \
    M(msj_argv, "argv");                                            \
    M(msj_fallarConMensaje, "fallarConMensaje");                    \
    M(msj_objeto, "objeto");                                        \
    M(msj_fijar_objeto, "fijar_objeto");                            \
    M(txt_verdadero, "VERDADERO");                                  \
    M(txt_falso, "FALSO");                                          \
    M(txt_nulo, "NULO");

pdcrt_error pdcrt_aloj_constantes(pdcrt_alojador alojador, PDCRT_OUT pdcrt_constantes* consts)
{
    pdcrt_error pderrno;
    consts->textos = NULL;
    consts->num_textos = 0;

#define PDCRT_INIC_CONST_TXT(cm, lit)                                   \
    if((pderrno = pdcrt_aloj_texto_desde_c(&consts->cm, alojador, lit)) != PDCRT_OK) \
    {                                                                   \
        goto error;                                                     \
    }
#define PDCRT_DEINIC_CONST_TXT(cm, _lit)            \
    if(consts->cm != NULL)                          \
    {                                               \
        pdcrt_dealoj_texto(alojador, consts->cm);   \
    }
#define PDCRT_NULL_CONST_TXT(cm, _lit)          \
    consts->cm = NULL;

    PDCRT_TABLA(PDCRT_NULL_CONST_TXT)
    PDCRT_TABLA(PDCRT_INIC_CONST_TXT)
    return PDCRT_OK;
error:
    PDCRT_TABLA(PDCRT_DEINIC_CONST_TXT)

#undef PDCRT_DEINIC_CONST_TXT
#undef PDCRT_INIC_CONST_TXT
#undef PDCRT_NULL_CONST_TXT

    PDCRT_ESCRIBIR_ERROR(pderrno, __func__);
    return pderrno;
}

void pdcrt_dealoj_constantes_internas(pdcrt_alojador alojador, pdcrt_constantes* consts)
{
#define PDCRT_DEALOJ(cm, _lit) pdcrt_dealoj_texto(alojador, consts->cm);

    PDCRT_TABLA(PDCRT_DEALOJ)

#undef PDCRT_DEALOJ
}

void pdcrt_dealoj_constante(pdcrt_alojador alojador, pdcrt_constantes* consts, size_t idx)
{
    pdcrt_dealoj_texto(alojador, consts->textos[idx]);
}

#undef PDCRT_TABLA

pdcrt_error pdcrt_registrar_constante_textual(pdcrt_alojador alojador, pdcrt_constantes* consts, size_t idx, pdcrt_texto* texto)
{
    if(idx < consts->num_textos)
    {
        consts->textos[idx] = texto;
    }
    else
    {
        size_t nuevo_tam = idx >= consts->num_textos ? (idx + 1) : (consts->num_textos + 1);
        consts->textos = pdcrt_realojar_simple(alojador, consts->textos, consts->num_textos * sizeof(pdcrt_texto*), nuevo_tam * sizeof(pdcrt_texto*));
        if(consts->textos == NULL)
        {
            PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, __func__);
            no_falla(PDCRT_ENOMEM);
        }
        consts->num_textos += 1;
        consts->textos[idx] = texto;
    }
    return PDCRT_OK;
}


// Registro de módulos:

pdcrt_error pdcrt_aloj_registro_de_modulos(pdcrt_alojador alojador,
                                           PDCRT_OUT pdcrt_registro_de_modulos* registro,
                                           size_t tam)
{
    registro->num_modulos = tam;
    if(tam > 0)
    {
        registro->modulos = pdcrt_alojar_simple(alojador, tam * sizeof(pdcrt_modulo));
        if(!registro->modulos)
            return PDCRT_ENOMEM;

        for(size_t i = 0; i < registro->num_modulos; i++)
        {
            registro->modulos[i] = (pdcrt_modulo){
                .nombre = NULL,
                .cuerpo = NULL,
                .valor = pdcrt_objeto_nulo()
            };
        }
    }
    else
    {
        registro->modulos = NULL;
    }
    return PDCRT_OK;
}

void pdcrt_dealoj_registro_de_modulos(pdcrt_alojador alojador,
                                      PDCRT_OUT pdcrt_registro_de_modulos* registro)
{
    if(registro->num_modulos > 0)
        pdcrt_dealojar_simple(alojador, registro->modulos, registro->num_modulos * sizeof(pdcrt_modulo));
}

pdcrt_error pdcrt_agregar_modulo(pdcrt_registro_de_modulos* registro, size_t i, pdcrt_modulo modulo)
{
    if(i >= registro->num_modulos)
        return PDCRT_EINVALOP;
    pdcrt_modulo* mod = &registro->modulos[i];
    if(mod->nombre || mod->cuerpo)
        return PDCRT_EINVALOP;
    mod->nombre = modulo.nombre;
    mod->cuerpo = modulo.cuerpo;
    mod->valor = modulo.valor;
    return PDCRT_OK;
}

bool pdcrt_obtener_modulo(pdcrt_registro_de_modulos* registro, pdcrt_texto* nombre, PDCRT_OUT pdcrt_modulo** modulo)
{
    for(size_t i = 0; i < registro->num_modulos; i++)
    {
        if(registro->modulos[i].nombre == nombre)
        {
            *modulo = &registro->modulos[i];
            return true;
        }
    }
    *modulo = NULL;
    return false;
}


// Alojador en contexto:

PDCRT_NULL void* pdcrt_alojar(pdcrt_contexto* ctx, size_t tam)
{
    return pdcrt_alojar_simple(ctx->alojador, tam);
}

void pdcrt_dealojar(pdcrt_contexto* ctx, void* ptr, size_t tam)
{
    return pdcrt_dealojar_simple(ctx->alojador, ptr, tam);
}

PDCRT_NULL void* pdcrt_realojar(pdcrt_contexto* ctx, void* ptr, size_t tam_actual, size_t tam_nuevo)
{
    return pdcrt_realojar_simple(ctx->alojador, ptr, tam_actual, tam_nuevo);
}


// Contexto:

pdcrt_error pdcrt_inic_contexto(pdcrt_contexto* ctx, pdcrt_alojador alojador, size_t num_mods)
{
    ctx->argc = 0;
    ctx->argv = NULL;
    ctx->claseObjeto = pdcrt_objeto_nulo();
    ctx->entornoBootstrap = pdcrt_objeto_nulo();
    pdcrt_error pderrno;
    if((pderrno = pdcrt_inic_pila(&ctx->pila, alojador)) != PDCRT_OK)
    {
        return pderrno;
    }
    ctx->alojador = alojador;
    if((pderrno = pdcrt_aloj_registro_de_modulos(alojador, &ctx->registro, num_mods)) != PDCRT_OK)
    {
        pdcrt_deinic_pila(&ctx->pila, alojador);
        return pderrno;
    }
    if((pderrno = pdcrt_aloj_constantes(alojador, &ctx->constantes)) != PDCRT_OK)
    {
        pdcrt_deinic_pila(&ctx->pila, alojador);
        return pderrno;
    }
    return PDCRT_OK;
}

void pdcrt_deinic_contexto(pdcrt_contexto* ctx, pdcrt_alojador alojador)
{
    PDCRT_DEPURAR_CONTEXTO(ctx, "Deinicializando el contexto");
    pdcrt_deinic_pila(&ctx->pila, alojador);
    pdcrt_dealoj_constantes_internas(alojador, &ctx->constantes);
}

static void pdcrt_depurar_objeto(pdcrt_objeto obj)
{
    switch(obj.tag)
    {
    case PDCRT_TOBJ_ENTERO:
        printf("|    i" PDCRT_ENTERO_FMT "\n", obj.value.i);
        break;
    case PDCRT_TOBJ_BOOLEANO:
        printf("|    %s\n", obj.value.b? "VERDADERO" : "FALSO");
        break;
    case PDCRT_TOBJ_MARCA_DE_PILA:
        printf("|    Marca de pila\n");
        break;
    case PDCRT_TOBJ_FLOAT:
        printf("|    f" PDCRT_FLOAT_FMT "\n", obj.value.f);
        break;
    case PDCRT_TOBJ_CLOSURE:
        printf(u8"|    Closure/función\n");
        printf(u8"|      proc => 0x%zX\n", (intptr_t) obj.value.c.proc);
        printf(u8"|      env 0x%zX  #%zd\n", (intptr_t) obj.value.c.env, obj.value.c.env->env_size);
        break;
    default:
        pdcrt_inalcanzable();
    }
}

void pdcrt_depurar_contexto(pdcrt_contexto* ctx, const char* extra)
{
    printf("|Contexto: %s\n", extra);
    printf("|  Pila [%zd elementos de %zd max.]\n", ctx->pila.num_elementos, ctx->pila.capacidad);
    for(size_t i = 0; i < ctx->pila.num_elementos; i++)
    {
        pdcrt_objeto obj = ctx->pila.elementos[i];
        pdcrt_depurar_objeto(obj);
    }
}


// Procesa el CLI:

// Perdón. (<https://www.chiark.greenend.org.uk/~sgtatham/coroutines.html>)
#define PDCRT_CORO_BEGIN                        \
    static int coro_state = 0;                  \
    switch(coro_state)                          \
    { case 0:
#define PDCRT_CORO_END }
#define PDCRT_CORO_LYIELD(i, x)                 \
    do { coro_state = i; return x; case i:; } while(0)
#define PDCRT_CORO_YIELD(x) PDCRT_CORO_LYIELD(__LINE__, x)

// Co-rutina (cofunción?): solo se puede llamar una vez.
static int pdcrt_getopt(int argc, char* argv[], const char* opts, char** optarg, int* optind)
{
    static char op;
    static char* arg;
    static int len, i;
    PDCRT_CORO_BEGIN;
    while(*optind < argc)
    {
        if(argv[*optind][0] != '-')
        {
            // Argumento posicional.
            break;
        }
        if(argv[*optind][1] == 0)
        {
            // El argumento "-" cuenta como posicional.
            break;
        }
        else if(argv[*optind][1] == '-')
        {
            // "--" termina las opciones
            *optind += 1;
            break;
        }
        // Opciones cortas: -abc...
        arg = argv[(*optind)++];
        len = strlen(arg);
        for(i = 1; i < len; i++)
        {
            op = arg[i];
            // Busca op en opts.
            int j;
            for(j = 0; opts[j] != 0 && opts[j] != op; j++);
            if(opts[j] == 0)
            {
                // op no es una opción válida.
                optind += 1;
                PDCRT_CORO_YIELD('?');
                continue;
            }
            else if(opts[j + 1] == ':')
            {
                // Pide un argumento
                PDCRT_ASSERT(*optind < argc);
                *optarg = argv[(*optind)++];
            }
            PDCRT_CORO_YIELD(op);
        }
    }
    PDCRT_CORO_END;
    return -1;
}

void pdcrt_procesar_cli(pdcrt_contexto* ctx, int argc, char* argv[])
{
    ctx->argc = argc - 1;
    ctx->argv = argv + 1;

    return;

    static bool check = false;
    PDCRT_ASSERT(!check);
    check = true;

    int opt, optind = 1, mostrarAyuda = 0;
    char* optarg = NULL;
    while((opt = pdcrt_getopt(argc, argv, "h", &optarg, &optind)) != -1)
    {
        switch(opt)
        {
        case 'h':
            mostrarAyuda = 1;
            break;
        default:
            PDCRT_ASSERT(0 && u8"opción sin reconocer");
        }
    }
    if(optind != argc)
    {
        puts("Argumentos adicionales inesperados.\nUso: programa [opciones...]\nUsa la opción -h para más ayuda.");
        exit(PDCRT_SALIDA_ERROR);
    }
    if(mostrarAyuda)
    {
        puts("Uso: programa [opciones...] [argumentos...]\n\n"
             "Opciones soportadas:\n"
             "  -t N   Registra las llamadas a funciones si N = 1. No lo hagas si N = 0.\n"
             "  -h     Muestra esta ayuda y termina.");
        exit(PDCRT_SALIDA_ERROR);
    }
}

void pdcrt_agregar_modulo_al_contexto(pdcrt_contexto* ctx, size_t i, int const_nombre, pdcrt_proc_t proc)
{
    pdcrt_modulo mod = {
        .nombre = ctx->constantes.textos[const_nombre],
        .cuerpo = proc,
        .valor = pdcrt_objeto_nulo()
    };
    no_falla(pdcrt_agregar_modulo(&ctx->registro, i, mod));
}


// Marcos:

pdcrt_error pdcrt_inic_marco(pdcrt_marco* marco, pdcrt_contexto* contexto, size_t num_locales, PDCRT_NULL pdcrt_marco* marco_anterior, int num_valores_a_devolver)
{
    size_t num_real_de_locales = num_locales + PDCRT_NUM_LOCALES_ESP;
    marco->locales = pdcrt_alojar(contexto, sizeof(pdcrt_objeto) * num_real_de_locales);
    if(!marco->locales)
    {
        PDCRT_ESCRIBIR_ERROR(PDCRT_ENOMEM, "pdcrt_inic_marco: alojando las locales");
        return PDCRT_ENOMEM;
    }
    marco->contexto = contexto;
    marco->nombre = NULL;
    marco->marco_anterior = marco_anterior;
    marco->num_valores_a_devolver = num_valores_a_devolver;
    marco->num_locales = num_real_de_locales;
    for(size_t i = 0; i < marco->num_locales; i++)
    {
        marco->locales[i] = pdcrt_objeto_nulo();
    }
    return PDCRT_OK;
}

void pdcrt_deinic_marco(pdcrt_marco* marco)
{
    PDCRT_DEPURAR_CONTEXTO(marco->contexto, "Deinicializando un marco");
    pdcrt_dealojar(marco->contexto, marco->locales, sizeof(pdcrt_objeto) * marco->num_locales);
    marco->num_locales = 0;
}

void pdcrt_fijar_local(pdcrt_marco* marco, pdcrt_local_index n, pdcrt_objeto obj)
{
    PDCRT_ASSERT(n != PDCRT_ID_NIL);
    marco->locales[n + PDCRT_NUM_LOCALES_ESP] = obj;
}

pdcrt_objeto pdcrt_obtener_local(pdcrt_marco* marco, pdcrt_local_index n)
{
    PDCRT_ASSERT(n != PDCRT_ID_NIL);
    return marco->locales[n + PDCRT_NUM_LOCALES_ESP];
}

void pdcrt_mostrar_marco(pdcrt_marco* marco, const char* procname, const char* info)
{
    FILE* out = stdout;
    size_t n = 0;
    fprintf(out, "|Marco de %s (0x%zX)\n", procname, (intptr_t) marco);
    fprintf(out, "|  %d:", PDCRT_NUM_LOCALES_ESP);
    for(pdcrt_marco* m = marco; m != NULL; m = m->marco_anterior)
    {
        fprintf(out, " > 0x%zX(%zd)", (intptr_t) m, m->num_locales);
        n += 1;
    }
    fprintf(out, "  (Tiene %zd marcos.)\n", n);
    pdcrt_objeto frm = pdcrt_obtener_local(marco, PDCRT_ID_EACT);
    if(frm.tag == PDCRT_TOBJ_CLOSURE)
    {
        n = 0;
        fprintf(out, "|  env %d:", PDCRT_NUM_LOCALES_ESP);
        for(pdcrt_objeto f = frm; f.tag == PDCRT_TOBJ_CLOSURE; f = f.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP])
        {
            fprintf(out, " > %zd", f.value.c.env->env_size);
            n += 1;
        }
        fprintf(out, "  (Tiene %zd envs.)\n", n);
    }
    fprintf(out, "|  %s\n", info);
}

pdcrt_objeto pdcrt_ajustar_parametros(pdcrt_marco* marco, size_t nargs, size_t nparams, bool variadic)
{
    PDCRT_ASSERT(nargs >= 1);
    pdcrt_objeto esup = pdcrt_sacar_de_pila(&marco->contexto->pila);
    nargs -= 1;
    while(nargs < nparams)
    {
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_nulo()));
        nargs += 1;
    }
    while(nargs > nparams && !variadic)
    {
        (void) pdcrt_sacar_de_pila(&marco->contexto->pila);
        nargs -= 1;
    }
    pdcrt_insertar_elemento_en_pila(&marco->contexto->pila, marco->contexto->alojador, variadic? nargs : nparams, pdcrt_objeto_marca_de_pila());
    return esup;
}

void pdcrt_marco_fijar_nombre(pdcrt_marco* marco, const char* nombre)
{
    marco->nombre = nombre;
}


// Recolector de basura (sencillo):

static void pdcrt_fijar_generacion_objeto(pdcrt_objeto obj, unsigned int gen)
{
    switch(obj.tag)
    {
    case PDCRT_TOBJ_ENTERO:
    case PDCRT_TOBJ_FLOAT:
    case PDCRT_TOBJ_MARCA_DE_PILA:
    case PDCRT_TOBJ_BOOLEANO:
    case PDCRT_TOBJ_NULO:
    case PDCRT_TOBJ_VOIDPTR:
    case PDCRT_TOBJ_TEXTO:
        return;
    case PDCRT_TOBJ_CLOSURE:
    case PDCRT_TOBJ_OBJETO:
        if(obj.value.c.env->generacion == gen)
            return;
        obj.value.c.env->generacion = gen;
        for(size_t i = 0; i < obj.value.c.env->env_size; i++)
        {
            pdcrt_fijar_generacion_objeto(obj.value.c.env->env[i], gen);
        }
        break;
    case PDCRT_TOBJ_ARREGLO:
        if(obj.value.a->generacion == gen)
            return;
        obj.value.a->generacion = gen;
        for(size_t i = 0; i < obj.value.a->longitud; i++)
        {
            pdcrt_fijar_generacion_objeto(obj.value.a->elementos[i], gen);
        }
        break;
    case PDCRT_TOBJ_ESPACIO_DE_NOMBRES:
        if(obj.value.e->generacion == gen)
            return;
        obj.value.e->generacion = gen;
        for(size_t i = 0; i < obj.value.e->ultimo_nombre_creado; i++)
        {
            pdcrt_fijar_generacion_objeto(obj.value.e->nombres[i].valor, gen);
        }
        break;
    }
}

static void pdcrt_fijar_generacion_en_objetos_vivos(pdcrt_marco* marco, unsigned int gen)
{
    for(pdcrt_marco* act = marco; act != NULL; act = act->marco_anterior)
    {
        for(size_t i = 0; i < act->num_locales; i++)
        {
            pdcrt_fijar_generacion_objeto(act->locales[i], gen);
        }
    }
}

static void pdcrt_recolectar_basura(pdcrt_marco* marco)
{
    if(marco->contexto->generacionDelRecolector == UINT_MAX)
    {
        marco->contexto->generacionDelRecolector = 0;
    }
    unsigned int gen = ++marco->contexto->generacionDelRecolector;
    pdcrt_fijar_generacion_en_objetos_vivos(marco, gen);
}


// Opcodes:

void pdcrt_op_iconst(pdcrt_marco* marco, pdcrt_entero c)
{
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(c)));
}

void pdcrt_op_bconst(pdcrt_marco* marco, bool c)
{
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(c)));
}

void pdcrt_op_lconst(pdcrt_marco* marco, int c)
{
    pdcrt_objeto txt = pdcrt_objeto_desde_texto(marco->contexto->constantes.textos[c]);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, txt));
}

void pdcrt_op_fconst(pdcrt_marco* marco, pdcrt_float c)
{
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_float(c)));
}

#define PDCRT_OP(marco, binop, proc)                                    \
    pdcrt_objeto a, b, msj;                                             \
    a = pdcrt_sacar_de_pila(&marco->contexto->pila);                    \
    b = pdcrt_sacar_de_pila(&marco->contexto->pila);                    \
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, a)); \
    msj = pdcrt_objeto_desde_texto(marco->contexto->constantes.binop);  \
    return pdcrt_continuacion_enviar_mensaje(proc, marco, b, msj, 1, 1);

pdcrt_continuacion pdcrt_op_sum(pdcrt_marco* marco, pdcrt_proc_continuacion proc)
{
    PDCRT_OP(marco, operador_mas, proc);
}

pdcrt_continuacion pdcrt_op_sub(pdcrt_marco* marco, pdcrt_proc_continuacion proc)
{
    PDCRT_OP(marco, operador_menos, proc);
}

pdcrt_continuacion pdcrt_op_mul(pdcrt_marco* marco, pdcrt_proc_continuacion proc)
{
    PDCRT_OP(marco, operador_por, proc);
}

pdcrt_continuacion pdcrt_op_div(pdcrt_marco* marco, pdcrt_proc_continuacion proc)
{
    PDCRT_OP(marco, operador_entre, proc);
}

pdcrt_continuacion pdcrt_op_gt(pdcrt_marco* marco, pdcrt_proc_continuacion proc)
{
    PDCRT_OP(marco, operador_mayorQue, proc);
}

pdcrt_continuacion pdcrt_op_ge(pdcrt_marco* marco, pdcrt_proc_continuacion proc)
{
    PDCRT_OP(marco, operador_mayorOIgualA, proc);
}

pdcrt_continuacion pdcrt_op_lt(pdcrt_marco* marco, pdcrt_proc_continuacion proc)
{
    PDCRT_OP(marco, operador_menorQue, proc);
}

pdcrt_continuacion pdcrt_op_le(pdcrt_marco* marco, pdcrt_proc_continuacion proc)
{
    PDCRT_OP(marco, operador_menorOIgualA, proc);
}

pdcrt_continuacion pdcrt_op_opeq(pdcrt_marco* marco, pdcrt_proc_continuacion proc)
{
    PDCRT_OP(marco, operador_igualA, proc);
}

#undef PDCRT_OP

void pdcrt_op_pop(pdcrt_marco* marco)
{
    pdcrt_sacar_de_pila(&marco->contexto->pila);
}

pdcrt_objeto pdcrt_op_lset(pdcrt_marco* marco)
{
    return pdcrt_sacar_de_pila(&marco->contexto->pila);
}

void pdcrt_op_lget(pdcrt_marco* marco, pdcrt_objeto v)
{
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, v));
}

void pdcrt_op_lsetc(pdcrt_marco* marco, pdcrt_objeto env, size_t alt, size_t ind)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    for(size_t i = 0; i < alt; i++)
    {
        pdcrt_objeto_debe_tener_closure(marco, env);
        env = env.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP];
    }
    pdcrt_objeto_debe_tener_closure(marco, env);
    env.value.c.env->env[((pdcrt_local_index) ind) + PDCRT_NUM_LOCALES_ESP] = obj;
}

void pdcrt_op_lgetc(pdcrt_marco* marco, pdcrt_objeto env, size_t alt, size_t ind)
{
    for(size_t i = 0; i < alt; i++)
    {
        pdcrt_objeto_debe_tener_closure(marco, env);
        env = env.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP];
    }
    pdcrt_objeto_debe_tener_closure(marco, env);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila,
                                   marco->contexto->alojador,
                                   env.value.c.env->env[((pdcrt_local_index) ind) + PDCRT_NUM_LOCALES_ESP]));
}

pdcrt_objeto pdcrt_op_open_frame(pdcrt_marco* marco, pdcrt_local_index padreidx, size_t tam)
{
    pdcrt_objeto padre;
    if(padreidx == PDCRT_ID_NIL)
    {
        padre = pdcrt_objeto_nulo();
    }
    else
    {
        padre = pdcrt_obtener_local(marco, padreidx);
    }
    pdcrt_objeto env;
    no_falla(pdcrt_objeto_aloj_closure(marco->contexto->alojador, NULL, tam, &env));
    for(size_t i = 0; i < env.value.c.env->env_size; i++)
    {
        env.value.c.env->env[i] = pdcrt_objeto_nulo();
    }
    env.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP] = padre;
    return env;
}

void pdcrt_op_einit(pdcrt_marco* marco, pdcrt_objeto env, size_t i, pdcrt_objeto local)
{
    (void) marco;
    pdcrt_objeto_debe_tener_closure(marco, env);
    env.value.c.env->env[i + PDCRT_NUM_LOCALES_ESP] = local;
}

void pdcrt_op_close_frame(pdcrt_marco* marco, pdcrt_objeto env)
{
    // nada que hacer.
    (void) env;
    PDCRT_RASTREAR_MARCO(marco, "<unk>", "CLSFRM");
}

void pdcrt_op_mkclz(pdcrt_marco* marco, pdcrt_local_index env, pdcrt_proc_t proc)
{
    pdcrt_objeto cima = pdcrt_obtener_local(marco, env);
    pdcrt_objeto_debe_tener_closure(marco, cima);
    pdcrt_objeto nuevo_env;
    nuevo_env.tag = PDCRT_TOBJ_CLOSURE;
    nuevo_env.value.c.env = cima.value.c.env;
    nuevo_env.value.c.proc = (pdcrt_funcion_generica) proc;
    nuevo_env.recv = (pdcrt_funcion_generica) &pdcrt_recv_closure;
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, nuevo_env));
}

void pdcrt_op_mk0clz(pdcrt_marco* marco, pdcrt_proc_t proc)
{
    pdcrt_objeto clz;
    clz.tag = PDCRT_TOBJ_CLOSURE;
    clz.value.c.proc = (pdcrt_funcion_generica) proc;
    clz.recv = (pdcrt_funcion_generica) &pdcrt_recv_closure;
    no_falla(pdcrt_aloj_env(&clz.value.c.env, marco->contexto->alojador, 0));
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, clz));
}

void pdcrt_op_mkarr(pdcrt_marco* marco, size_t tam)
{
    pdcrt_objeto arr;
    no_falla(pdcrt_objeto_aloj_arreglo(marco->contexto->alojador, tam, &arr));
    arr.value.a->longitud = tam;
    for(size_t i = 0; i < tam; i++)
    {
        pdcrt_objeto elemento = pdcrt_sacar_de_pila(&marco->contexto->pila);
        arr.value.a->elementos[tam - i - 1] = elemento;
    }
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, arr));
}

void pdcrt_op_call(pdcrt_marco* marco, pdcrt_proc_t proc, int acepta, int devuelve)
{
    (void) marco;
    (void) proc;
    (void) acepta;
    (void) devuelve;
    pdcrt_no_implementado("Opcode CALL");
}

void pdcrt_op_retn(pdcrt_marco* marco, int n)
{
    for(size_t i = marco->contexto->pila.num_elementos - n; i < marco->contexto->pila.num_elementos; i++)
    {
        pdcrt_objeto obj = marco->contexto->pila.elementos[i];
        if(obj.tag == PDCRT_TOBJ_MARCA_DE_PILA)
        {
            fprintf(stderr, "Trato de devolver a traves de una marca de pila\n");
            pdcrt_abort();
        }
    }

    if(n > marco->num_valores_a_devolver)
    {
        for(size_t i = n - marco->num_valores_a_devolver; i > 0; i--)
        {
            pdcrt_sacar_de_pila(&marco->contexto->pila);
        }
    }
    else if(n < marco->num_valores_a_devolver)
    {
        for(size_t i = marco->num_valores_a_devolver - n; i > 0; i--)
        {
            pdcrt_insertar_elemento_en_pila(&marco->contexto->pila, marco->contexto->alojador, n, pdcrt_objeto_nulo());
        }
    }

    pdcrt_objeto marca = pdcrt_eliminar_elemento_en_pila(&marco->contexto->pila, marco->num_valores_a_devolver);
    pdcrt_objeto_debe_tener_tipo_tb(marco, marca, PDCRT_TOBJ_MARCA_DE_PILA);
}

int pdcrt_real_return(pdcrt_marco* marco)
{
    (void) marco;
    return 0;
}

int pdcrt_passthru_return(pdcrt_marco* marco)
{
    (void) marco;
    return 0;
}

bool pdcrt_op_choose(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo_tb(marco, obj, PDCRT_TOBJ_BOOLEANO);
    return obj.value.b;
}

void pdcrt_op_rot(pdcrt_marco* marco, int n)
{
    // El caso n == 0 es un caso especial.
    if(n == 0)
    {
        return;
    }
    else
    {
        PDCRT_ASSERT(n > 0);
        pdcrt_pila* pila = &marco->contexto->pila;
        pdcrt_objeto obj = pdcrt_eliminar_elemento_en_pila(pila, n);
        no_falla(pdcrt_empujar_en_pila(pila, marco->contexto->alojador, obj));
    }
}

void pdcrt_op_rotm(pdcrt_marco* marco, int n)
{
    // El caso n == 0 es un caso especial.
    if(n == 0)
    {
        return;
    }
    else
    {
        PDCRT_ASSERT(n > 0);
        pdcrt_pila* pila = &marco->contexto->pila;
        pdcrt_objeto mensaje = pdcrt_eliminar_elemento_en_pila(pila, n);
        pdcrt_objeto obj = pdcrt_eliminar_elemento_en_pila(pila, n);
        no_falla(pdcrt_empujar_en_pila(pila, marco->contexto->alojador, obj));
        no_falla(pdcrt_empujar_en_pila(pila, marco->contexto->alojador, mensaje));
    }
}

pdcrt_continuacion pdcrt_op_cmp(pdcrt_marco* marco, pdcrt_cmp cmp, pdcrt_proc_continuacion proc)
{
    pdcrt_objeto a, b;
    a = pdcrt_sacar_de_pila(&marco->contexto->pila);
    b = pdcrt_sacar_de_pila(&marco->contexto->pila);
    if(cmp == PDCRT_CMP_REFEQ)
    {
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(pdcrt_objeto_identicos(a, b))));
        return pdcrt_continuacion_normal(proc, marco);
    }
    else
    {
        PDCRT_ASSERT(cmp == PDCRT_CMP_EQ || cmp == PDCRT_CMP_NEQ);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, b));
        pdcrt_texto* mensaje = (cmp == PDCRT_CMP_EQ)? marco->contexto->constantes.operador_igualA : marco->contexto->constantes.operador_noIgualA;
        return pdcrt_continuacion_enviar_mensaje(proc, marco, a, pdcrt_objeto_desde_texto(mensaje), 1, 1);
    }
}

void pdcrt_op_not(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo_tb(marco, obj, PDCRT_TOBJ_BOOLEANO);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(!obj.value.b)));
}

void pdcrt_op_mtrue(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo_tb(marco, obj, PDCRT_TOBJ_BOOLEANO);
    if(!obj.value.b)
    {
        fprintf(stderr, u8"Error: instrucción `necesitas` con condición falsa.\n");
        pdcrt_abort();
    }
}

void pdcrt_op_prn(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    switch(obj.tag)
    {
    case PDCRT_TOBJ_ENTERO:
        printf(PDCRT_ENTERO_FMT, obj.value.i);
        break;
    case PDCRT_TOBJ_BOOLEANO:
        printf("%s", obj.value.b? "VERDADERO" : "FALSO");
        break;
    case PDCRT_TOBJ_FLOAT:
        printf(PDCRT_FLOAT_FMT, obj.value.f);
        break;
    case PDCRT_TOBJ_TEXTO:
        for(size_t i = 0; i < obj.value.t->longitud; i++)
        {
            printf("%c", obj.value.t->contenido[i]);
        }
        break;
    case PDCRT_TOBJ_NULO:
        printf("NULO");
        break;
    default:
        PDCRT_ASSERT(0 && "cannot prn obj");
    }
}

void pdcrt_op_nl(pdcrt_marco* marco)
{
    (void) marco;
    printf("\n");
}

pdcrt_continuacion pdcrt_op_msg(pdcrt_marco* marco, pdcrt_proc_continuacion proc, int cid, int args, int rets)
{
    pdcrt_objeto mensaje = pdcrt_objeto_desde_texto(marco->contexto->constantes.textos[cid]);
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    return pdcrt_continuacion_enviar_mensaje(proc, marco, obj, mensaje, args, rets);
}

pdcrt_continuacion pdcrt_op_tail_msg(pdcrt_marco* marco, int cid, int args, int rets)
{
    pdcrt_objeto marca = pdcrt_eliminar_elemento_en_pila(&marco->contexto->pila, args + 1);
    pdcrt_objeto_debe_tener_tipo_tb(marco, marca, PDCRT_TOBJ_MARCA_DE_PILA);

    pdcrt_marco* marco_superior = marco->marco_anterior;
    pdcrt_deinic_marco(marco);
    pdcrt_objeto mensaje = pdcrt_objeto_desde_texto(marco_superior->contexto->constantes.textos[cid]);
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco_superior->contexto->pila);
    return pdcrt_continuacion_tail_enviar_mensaje(marco_superior, obj, mensaje, args, rets);
}

static size_t pdcrt_desvariadicear_argumentos(pdcrt_marco* marco, const unsigned char* proto, int args)
{
    size_t total = 0;
    for(size_t i = 0; i < (size_t) args; i++)
    {
        if(proto[i] > 0)
        {
            size_t rel = args - (i + 1);
            pdcrt_objeto arreglo = pdcrt_eliminar_elemento_en_pila(&marco->contexto->pila, rel);
            pdcrt_objeto_debe_tener_tipo_tb(marco, arreglo, PDCRT_TOBJ_ARREGLO);
            for(size_t j = 0; j < arreglo.value.a->longitud; j++)
            {
                pdcrt_objeto el = arreglo.value.a->elementos[j];
                pdcrt_insertar_elemento_en_pila(&marco->contexto->pila, marco->contexto->alojador, rel, el);
            }
            total += arreglo.value.a->longitud;
        }
        else
        {
            total += 1;
        }
    }
    return total;
}

pdcrt_continuacion pdcrt_op_msgv(pdcrt_marco* marco, pdcrt_proc_continuacion proc, int cid, const unsigned char* proto, int args, int rets)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    size_t total = pdcrt_desvariadicear_argumentos(marco, proto, args);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, obj));
    return pdcrt_op_msg(marco, proc, cid, total, rets);
}

pdcrt_continuacion pdcrt_op_tail_msgv(pdcrt_marco* marco, int cid, const unsigned char* proto, int args, int rets)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    size_t total = pdcrt_desvariadicear_argumentos(marco, proto, args);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, obj));
    return pdcrt_op_tail_msg(marco, cid, total, rets);
}

pdcrt_continuacion pdcrt_op_dynmsg(pdcrt_marco* marco, pdcrt_proc_continuacion proc, int args, int rets)
{
    pdcrt_objeto mensaje = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    return pdcrt_continuacion_enviar_mensaje(proc, marco, obj, mensaje, args, rets);
}

pdcrt_continuacion pdcrt_op_tail_dynmsg(pdcrt_marco* marco, int args, int rets)
{
    pdcrt_objeto marca = pdcrt_eliminar_elemento_en_pila(&marco->contexto->pila, args + 1);
    pdcrt_objeto_debe_tener_tipo_tb(marco, marca, PDCRT_TOBJ_MARCA_DE_PILA);

    pdcrt_marco* marco_superior = marco->marco_anterior;
    pdcrt_deinic_marco(marco);
    pdcrt_objeto mensaje = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco_superior->contexto->pila);
    return pdcrt_continuacion_tail_enviar_mensaje(marco_superior, obj, mensaje, args, rets);
}

pdcrt_continuacion pdcrt_op_dynmsgv(pdcrt_marco* marco, pdcrt_proc_continuacion proc, const unsigned char* proto, int args, int rets)
{
    pdcrt_objeto mensaje = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    size_t total = pdcrt_desvariadicear_argumentos(marco, proto, args);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, obj));
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, mensaje));
    return pdcrt_op_dynmsg(marco, proc, total, rets);
}

pdcrt_continuacion pdcrt_op_tail_dynmsgv(pdcrt_marco* marco, const unsigned char* proto, int args, int rets)
{
    pdcrt_objeto mensaje = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    size_t total = pdcrt_desvariadicear_argumentos(marco, proto, args);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, obj));
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, mensaje));
    return pdcrt_op_tail_dynmsg(marco, total, rets);
}

void pdcrt_op_spush(pdcrt_marco* marco, pdcrt_local_index eact, pdcrt_local_index esup)
{
    pdcrt_objeto o_eact = pdcrt_obtener_local(marco, eact);
    pdcrt_objeto o_esup = pdcrt_obtener_local(marco, esup);
    o_esup = o_eact;
    o_eact = pdcrt_objeto_nulo();
    pdcrt_fijar_local(marco, eact, o_eact);
    pdcrt_fijar_local(marco, esup, o_esup);
    PDCRT_RASTREAR_MARCO(marco, "<unk>", "SPUSH");
}

void pdcrt_op_spop(pdcrt_marco* marco, pdcrt_local_index eact, pdcrt_local_index esup)
{
    pdcrt_objeto o_eact = pdcrt_obtener_local(marco, eact);
    pdcrt_objeto o_esup = pdcrt_obtener_local(marco, esup);
    pdcrt_objeto_debe_tener_closure(marco, o_eact);
    pdcrt_objeto_debe_tener_closure(marco, o_esup);
    PDCRT_ASSERT(o_eact.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP].value.c.env == o_esup.value.c.env);
    o_esup = o_esup.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP];
    o_eact = o_eact.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP];
    pdcrt_objeto_debe_tener_closure(marco, o_eact);
    // No es necesario verificar que ESUP sea una CLOSURE porque el primer EACT
    // de un procedimiento no tiene ESUP, o en otras palabras: si EACT es el
    // primero del procedimiento (lo que corresponde con `SPOP`-ear al ámbito
    // principal) entonces ESUP será nulo.
    pdcrt_fijar_local(marco, eact, o_eact);
    pdcrt_fijar_local(marco, esup, o_esup);
    PDCRT_RASTREAR_MARCO(marco, "<unk>", "SPOP");
}

void pdcrt_op_clztoobj(pdcrt_marco* marco)
{
    pdcrt_objeto clz = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo_tb(marco, clz, PDCRT_TOBJ_CLOSURE);
    clz.tag = PDCRT_TOBJ_OBJETO;
    clz.recv = (pdcrt_funcion_generica) &pdcrt_recv_objeto;
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, clz));
}

void pdcrt_op_objtoclz(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo_tb(marco, obj, PDCRT_TOBJ_OBJETO);
    obj.tag = PDCRT_TOBJ_CLOSURE;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_closure;
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, obj));
}

void pdcrt_op_objattr(pdcrt_marco* marco)
{
    pdcrt_objeto idx = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo_tb(marco, idx, PDCRT_TOBJ_ENTERO);
    pdcrt_objeto_debe_tener_uno_de_los_tipos(marco, obj, PDCRT_TOBJ_CLOSURE, PDCRT_TOBJ_OBJETO);
    size_t real_idx = idx.value.i;
    PDCRT_ASSERT(real_idx >= 0 && real_idx < obj.value.c.env->env_size);
    pdcrt_objeto v = obj.value.c.env->env[real_idx];
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, v));
}

void pdcrt_op_objattrset(pdcrt_marco* marco)
{
    pdcrt_objeto v = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto idx = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo_tb(marco, idx, PDCRT_TOBJ_ENTERO);
    pdcrt_objeto_debe_tener_uno_de_los_tipos(marco, obj, PDCRT_TOBJ_CLOSURE, PDCRT_TOBJ_OBJETO);
    size_t real_idx = idx.value.i;
    PDCRT_ASSERT(real_idx >= 0 && real_idx < obj.value.c.env->env_size);
    obj.value.c.env->env[real_idx] = v;
}

void pdcrt_op_objsz(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_uno_de_los_tipos(marco, obj, PDCRT_TOBJ_CLOSURE, PDCRT_TOBJ_OBJETO);
    size_t tam = obj.value.c.env->env_size;
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(tam)));
}

void pdcrt_op_opnexp(pdcrt_marco* marco, size_t num_exp)
{
    pdcrt_objeto edn;
    no_falla(pdcrt_objeto_aloj_espacio_de_nombres(&edn, marco->contexto->alojador, num_exp));
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, edn));
}

void pdcrt_op_clsexp(pdcrt_marco* marco)
{
    pdcrt_objeto edn = pdcrt_cima_de_pila(&marco->contexto->pila);
    if(edn.tag != PDCRT_TOBJ_ESPACIO_DE_NOMBRES)
    {
        fprintf(stderr, "El objeto del opcode CLSEXP no era un espacio de nombres.\n");
        pdcrt_abort();
    }
}

void pdcrt_op_exp(pdcrt_marco* marco, int idx, bool autoejec)
{
    pdcrt_objeto valor = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto edn = pdcrt_cima_de_pila(&marco->contexto->pila);
    pdcrt_texto* nombre = marco->contexto->constantes.textos[idx];
    pdcrt_agregar_nombre_al_espacio_de_nombres(edn.value.e, nombre, autoejec, valor);
}

pdcrt_continuacion pdcrt_op_import(pdcrt_marco* marco, int cid, pdcrt_proc_continuacion cont)
{
    pdcrt_texto* nombre = marco->contexto->constantes.textos[cid];
    pdcrt_modulo* modulo;
    if(!pdcrt_obtener_modulo(&marco->contexto->registro, nombre, &modulo))
    {
        fprintf(stderr, "IMPORT: No se pudo encontrar el módulo #%d\n", cid);
        pdcrt_inalcanzable();
    }
    if(modulo->valor.tag == PDCRT_TOBJ_NULO)
    {
        pdcrt_op_mk0clz(marco, modulo->cuerpo);
        pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto mensaje = pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar);
        return pdcrt_continuacion_enviar_mensaje(cont, marco, obj, mensaje, 0, 1);
    }
    else
    {
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, modulo->valor));
        return pdcrt_continuacion_normal(cont, marco);
    }
}

void pdcrt_op_saveimport(pdcrt_marco* marco, int cid)
{
    pdcrt_objeto edn = pdcrt_cima_de_pila(&marco->contexto->pila);
    if(edn.tag != PDCRT_TOBJ_ESPACIO_DE_NOMBRES)
    {
        fprintf(stderr, "El objeto del opcode SAVEIMPORT no era un espacio de nombres.\n");
        pdcrt_abort();
    }
    pdcrt_texto* nombre = marco->contexto->constantes.textos[cid];
    pdcrt_modulo* modulo;
    if(!pdcrt_obtener_modulo(&marco->contexto->registro, nombre, &modulo))
    {
        fprintf(stderr, "SAVEIMPORT: No se pudo encontrar el módulo #%d\n", cid);
        pdcrt_inalcanzable();
    }
    modulo->valor = edn;
}

void pdcrt_op_objtag(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(obj.tag)));
}

void pdcrt_op_dup(pdcrt_marco* marco)
{
    pdcrt_objeto valor = pdcrt_sacar_de_pila(&marco->contexto->pila);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, valor));
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, valor));
}

void pdcrt_op_drop(pdcrt_marco* marco)
{
    (void) pdcrt_sacar_de_pila(&marco->contexto->pila);
}

void pdcrt_op_nslookup(pdcrt_marco* marco, int cid)
{
    pdcrt_objeto valor = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo_tb(marco, valor, PDCRT_TOBJ_ESPACIO_DE_NOMBRES);
    pdcrt_objeto res = pdcrt_objeto_nulo();
    pdcrt_texto* nombre = marco->contexto->constantes.textos[cid];
    if(pdcrt_obtener_campo_del_espacio_de_nombres(valor.value.e, nombre, &res))
    {
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, res));
    }
    else
    {
        fprintf(stderr, "El espacio de nombres de NSLOOKUP no poseía el nombre ");
        pdcrt_escribir_texto_al_archivo(stderr, nombre);
        fprintf(stderr, "\n");
        pdcrt_abort();
    }
}

void pdcrt_op_getclsobj(pdcrt_marco* marco)
{
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, marco->contexto->claseObjeto));
}

pdcrt_continuacion pdcrt_frt_obtener_rt(pdcrt_marco* marco_actual, pdcrt_marco* marco_superior, int args, int rets)
{
    no_falla(pdcrt_inic_marco(marco_actual, marco_superior->contexto, 0, marco_superior, rets));
    pdcrt_ajustar_argumentos_para_c(marco_actual->contexto, args, 1);
    (void) pdcrt_sacar_de_pila(&marco_actual->contexto->pila);
    pdcrt_objeto obj = pdcrt_objeto_voidptr(NULL);
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_rt;
    no_falla(pdcrt_empujar_en_pila(&marco_actual->contexto->pila, marco_actual->contexto->alojador, obj));
    pdcrt_ajustar_valores_devueltos_para_c(marco_actual->contexto, rets, 1);
    return pdcrt_continuacion_devolver();
}

struct pdcrt_archivo
{
    FILE* archivo;
    pdcrt_objeto nombre_del_archivo;
    int modo;
};

pdcrt_continuacion pdcrt_recv_archivo(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets);

struct pdcrt_archivo* pdcrt_abrir_archivo(pdcrt_alojador alojador, pdcrt_texto* nombre, pdcrt_entero modo)
{
    bool write = false, binary = false, truncate = false;
    write = modo % 10 == 1;
    binary = (modo / 10) % 10 == 1;
    truncate = (modo / 100) % 10 == 1;
    char modo_cstr[] = {0, 0, 0, 0};
    char* ptr = modo_cstr;
    if(write)
        *ptr++ = 'w';
    else
        *ptr++ = 'r';
    if(binary)
        *ptr++ = 'b';
    // TODO: Agrega el '+' para no truncar
    if(truncate && !write)
    {
        fprintf(stderr, "Error abriendo un archivo: no se puede abrir para leer y truncar. Modo " PDCRT_ENTERO_FMT " archivo: ", modo);
        pdcrt_escribir_texto(nombre);
        fprintf(stderr, "\n");
        pdcrt_abort();
    }

    char* nombre_cstr = pdcrt_alojar_simple(alojador, nombre->longitud + 1);
    if(!nombre_cstr)
    {
        fprintf(stderr, "Error abriendo un archivo: no se pudo cambiar el tipo de nombre. Archivo: ");
        pdcrt_escribir_texto(nombre);
        fprintf(stderr, "\n");
        pdcrt_abort();
    }
    memcpy(nombre_cstr, nombre->contenido, nombre->longitud);
    nombre_cstr[nombre->longitud] = '\0';

    printf("!!!! %s  %03ld  %s\n", modo_cstr, modo, nombre_cstr);

    struct pdcrt_archivo* archivo = pdcrt_alojar_simple(alojador, sizeof(struct pdcrt_archivo));
    if(!archivo)
    {
        fprintf(stderr, "Error abriendo un archivo: no se pudo alojar al archivo. Archivo: ");
        pdcrt_escribir_texto(nombre);
        fprintf(stderr, "\n");
        pdcrt_abort();
    }

    FILE* handle = fopen(nombre_cstr, modo_cstr);
    if(!handle)
    {
        int errno_viejo = errno;
        fprintf(stderr, "No se pudo abrir el archivo: ");
        pdcrt_escribir_texto(nombre);
        fprintf(stderr, " con el modo  " PDCRT_ENTERO_FMT ": %s\n", modo, strerror(errno_viejo));
        pdcrt_abort();
    }

    archivo->nombre_del_archivo = pdcrt_objeto_desde_texto(nombre);
    archivo->modo = modo;
    archivo->archivo = handle;
    return archivo;
}

pdcrt_continuacion pdcrt_recv_rt(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, yo, PDCRT_TOBJ_VOIDPTR);
    pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);
    if(pdcrt_texto_cmp_lit(msj.value.t, "argc") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(marco->contexto->argc)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "argv") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto i = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, i, PDCRT_TOBJ_ENTERO);
        if(i.value.i < 0 || i.value.i >= marco->contexto->argc)
        {
            fprintf(stderr, u8"Índice inválido para __RT#argv: %ld\n", i.value.i);
            pdcrt_abort();
        }
        pdcrt_objeto res;
        pdcrt_objeto_aloj_texto_desde_cstr(&res, marco->contexto->alojador, marco->contexto->argv[i.value.i]);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, res));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "fallarConMensaje") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto msj = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);
        fprintf(stderr, "Error: ");
        pdcrt_escribir_texto_al_archivo(stderr, msj.value.t);
        fprintf(stderr, "\n");
        pdcrt_abort();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "objeto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, marco->contexto->claseObjeto));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "fijar_objeto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        marco->contexto->claseObjeto = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 0);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "entornoBootstrap") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, marco->contexto->entornoBootstrap));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "fijar_entornoBootstrap") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        marco->contexto->entornoBootstrap = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 0);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "construirTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        pdcrt_objeto arr = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, arr, PDCRT_TOBJ_ARREGLO);
        struct pdcrt_constructor_de_texto cons;
        size_t capacidad = 0;
        for(size_t i = 0; i < arr.value.a->longitud; i++)
        {
            pdcrt_objeto el = arr.value.a->elementos[i];
            pdcrt_objeto_debe_tener_tipo_tb(marco, el, PDCRT_TOBJ_TEXTO);
            capacidad += el.value.t->longitud;
        }
        pdcrt_inic_constructor_de_texto(&cons, marco->contexto->alojador, capacidad);
        for(size_t i = 0; i < arr.value.a->longitud; i++)
        {
            pdcrt_objeto el = arr.value.a->elementos[i];
            pdcrt_objeto_debe_tener_tipo_tb(marco, el, PDCRT_TOBJ_TEXTO);
            pdcrt_constructor_agregar(marco->contexto->alojador, &cons,
                                      el.value.t->contenido, el.value.t->longitud);
        }
        pdcrt_texto* texto;
        pdcrt_finalizar_constructor(marco->contexto->alojador, &cons, &texto);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_desde_texto(texto)));
        pdcrt_deainic_constructor_de_texto(marco->contexto->alojador, &cons);
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "leerCaracter") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        int c = getchar();
        _Static_assert(sizeof(c) <= sizeof(pdcrt_entero),
                       "necesito almacenar un int (char o EOF) en un entero");
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero((c == EOF)? -1 : c)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "abrirArchivo") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 2);
        pdcrt_objeto modo = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto nombre = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, nombre, PDCRT_TOBJ_TEXTO);
        pdcrt_objeto_debe_tener_tipo_tb(marco, modo, PDCRT_TOBJ_ENTERO);
        struct pdcrt_archivo* archivo = pdcrt_abrir_archivo(marco->contexto->alojador, nombre.value.t, modo.value.i);
        pdcrt_objeto obj = pdcrt_objeto_voidptr(archivo);
        obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_archivo;
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, obj));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else
    {
        printf("Mensaje ");
        pdcrt_escribir_texto(msj.value.t);
        printf(" no entendido para el objeto especial del runtime (__RT): %p\n", yo.value.p);
        pdcrt_abort();
    }
}

pdcrt_continuacion pdcrt_recv_archivo(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo_tb(marco, yo, PDCRT_TOBJ_VOIDPTR);
    pdcrt_objeto_debe_tener_tipo_tb(marco, msj, PDCRT_TOBJ_TEXTO);
    struct pdcrt_archivo* archivo = yo.value.p;

#define PDCRT_FALLA_SI_ESTA_CERRADO(metodo)                             \
    if(archivo->archivo == NULL)                                        \
    {                                                                   \
        fprintf(stderr, metodo "" u8": El archivo %p está cerrado\n", yo.value.p); \
        pdcrt_abort();                                                  \
    }

    if(pdcrt_texto_cmp_lit(msj.value.t, "clonar") == 0)
    {
        fprintf(stderr, "No se pueden clonar archivos %p\n", yo.value.p);
        pdcrt_abort();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "igualA") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "operador_=") == 0)
    {
        fprintf(stderr, "No se pueden comparar archivos %p\n", yo.value.p);
        pdcrt_abort();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
#define PDCRT_LONGITUD_BUFFER 40
        char buffer[PDCRT_LONGITUD_BUFFER];
        memset(buffer, '\0', PDCRT_LONGITUD_BUFFER);
        snprintf(buffer, PDCRT_LONGITUD_BUFFER, "Archivo %p", yo.value.p);
        size_t lonbuff = strlen(buffer);
        pdcrt_objeto res;
        no_falla(pdcrt_objeto_aloj_texto(&res, marco->contexto->alojador, lonbuff));
        memcpy(res.value.t->contenido, buffer, lonbuff);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, res));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
#undef PDCRT_LONGITUD_BUFFER
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "estaAbierto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(archivo->archivo != NULL)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "cerrar") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        if(archivo->archivo != NULL)
        {
            fclose(archivo->archivo);
            archivo->archivo = NULL;
        }
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 0);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "leerByte") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        PDCRT_FALLA_SI_ESTA_CERRADO("leerByte");
        int c = fgetc(archivo->archivo);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero((c == EOF)? -1 : c)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "obtenerSiguenteByte") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        PDCRT_FALLA_SI_ESTA_CERRADO("obtenerSiguenteByte");
        int c = fgetc(archivo->archivo);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero((c == EOF)? -1 : c)));
        // El manua de ungetc(3) no dice nada sobre si se debería ungetc-ear
        // EOF.
        if(c != EOF)
        {
            int res = ungetc(c, archivo->archivo);
            if(res == EOF)
            {
                // Hubo un error, pero ungetc(3) dice que al menos un push-back
                // está garantizado. En teoría esto nunca debería pasar...
                fprintf(stderr, "obtenerSiguenteByte: No se pudo invocar a ungetc(3) en el archivo %p\n", yo.value.p);
                pdcrt_abort();
            }
        }
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "escribirByte") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        PDCRT_FALLA_SI_ESTA_CERRADO("escribirByte");
        pdcrt_objeto entero = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, entero, PDCRT_TOBJ_ENTERO);
        fputc(entero.value.i, archivo->archivo);
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 0);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "escribirTexto") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        PDCRT_FALLA_SI_ESTA_CERRADO("escribirTexto");
        pdcrt_objeto texto = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, texto, PDCRT_TOBJ_TEXTO);
        fwrite(texto.value.t->contenido, sizeof(char), texto.value.t->longitud, archivo->archivo);
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 0);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "posicionActual") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        PDCRT_FALLA_SI_ESTA_CERRADO("posicionActual");
        long pos = ftell(archivo->archivo);
        _Static_assert(sizeof(pos) <= sizeof(pdcrt_entero),
                       "necesito almacenar una posicion textual (ftell) en un entero");
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(pos)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "cambiarPosicion") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 1);
        PDCRT_FALLA_SI_ESTA_CERRADO("cambiarPosicion");
        pdcrt_objeto entero = pdcrt_sacar_de_pila(&marco->contexto->pila);
        pdcrt_objeto_debe_tener_tipo_tb(marco, entero, PDCRT_TOBJ_ENTERO);
        _Static_assert(sizeof(long) <= sizeof(pdcrt_entero),
                       "necesito sacar una posicion textual (ftell) de un entero");
        fseek(archivo->archivo, entero.value.i, SEEK_SET);
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 0);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "finDelArchivo") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        PDCRT_FALLA_SI_ESTA_CERRADO("finDelArchivo");
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(feof(archivo->archivo) != 0)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "error") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        PDCRT_FALLA_SI_ESTA_CERRADO("error");
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_booleano(ferror(archivo->archivo) != 0)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "nombreDelArchivo") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, archivo->nombre_del_archivo));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "modo") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        _Static_assert(sizeof(archivo->modo) <= sizeof(pdcrt_entero),
                       "necesito almacenar un int en un entero");
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(archivo->modo)));
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "__leerTodo") == 0)
    {
        pdcrt_ajustar_argumentos_para_c(marco->contexto, args, 0);
        pdcrt_no_implementado("archivo#__leerTodo");
        pdcrt_ajustar_valores_devueltos_para_c(marco->contexto, rets, 1);
        return pdcrt_continuacion_devolver();
    }
    else
    {
        printf("Mensaje ");
        pdcrt_escribir_texto(msj.value.t);
        printf(" no entendido para el archivo: %p\n", yo.value.p);
        pdcrt_abort();
    }
#undef PDCRT_FALLA_SI_ESTA_CERRADO
}
