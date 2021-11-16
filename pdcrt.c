#include "pdcrt.h"

#include <assert.h>
#include <math.h>

#ifdef PDCRT_OPT_GNU
#include <malloc.h>
#define PDCRT_MALLOC_SIZE(ptr) malloc_usable_size(ptr)
#endif


const char* pdcrt_perror(pdcrt_error err)
{
    static const char* const errores[] =
        { u8"Ok",
          u8"No hay memoria",
          u8"No se pudo alojar más memoria"
        };
    return errores[err];
}

static void no_falla(pdcrt_error err)
{
    if(err != PDCRT_OK)
    {
        fprintf(stderr, "Error (que no debia fallar): %s\n", pdcrt_perror(err));
        abort();
    }
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
    assert(narena != NULL);
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
        void* nptr = malloc(tam_nuevo);
        if(nptr != NULL)
        {
            pdcrt_arena_agregar(dt, nptr);
        }
        return nptr;
    }
    else
    {
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
        return PDCRT_ENOMEM;
    }
    dt->punteros = NULL;
    dt->num_punteros = 0;
    aloj->alojar = &pdcrt_alojar_en_arena;
    aloj->datos = dt;
    return PDCRT_OK;
}

void pdcrt_dealoj_alojador_de_arena(pdcrt_alojador aloj)
{
    pdcrt_alojador_de_arena* arena = aloj.datos;
#ifdef PDCRT_MALLOC_SIZE
    size_t total = 0, maxaloj = 0;
    for(size_t i = 0; i < arena->num_punteros; i++)
    {
        size_t tam = PDCRT_MALLOC_SIZE(arena->punteros[i]);
        total += tam;
        if(tam > maxaloj)
        {
            maxaloj = tam;
        }
    }
    printf(u8"Desalojando alojador de arena: %zd elementos, %zd bytes en total, máxima alojación de %zd bytes.\n", arena->num_punteros, total, maxaloj);
    printf(u8"  Total de %zd bytes, %zd KiB, %zd MiB\n", total, total / 1024, (total / 1024) / 1024);
    printf(u8"  Máxima alojación de %zd bytes, %zd KiB, %zd MiB\n", maxaloj, maxaloj / 1024, (maxaloj / 1024) / 1024);
#else
    printf(u8"Desalojando alojador de arena: %zd elementos\n", arena->num_punteros);
    printf(u8"  Advertencia: no se pudo solicitar el tamaño en bytes de los elementos\n");
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
        return PDCRT_ENOMEM;
    }
    (*texto)->contenido = pdcrt_alojar_simple(alojador, sizeof(char) * lon);
    if((*texto)->contenido == NULL)
    {
        pdcrt_dealojar_simple(alojador, *texto, sizeof(pdcrt_texto));
        return PDCRT_ENOMEM;
    }
    (*texto)->longitud = lon;
    return PDCRT_OK;
}

pdcrt_error pdcrt_aloj_texto_desde_c(PDCRT_OUT pdcrt_texto** texto, pdcrt_alojador alojador, const char* cstr)
{
    pdcrt_error errc = pdcrt_aloj_texto(texto, alojador, strlen(cstr));
    if(errc != PDCRT_OK)
    {
        return errc;
    }
    for(size_t i = 0; cstr[i] != '\0'; i++)
    {
        (*texto)->contenido[i] = cstr[i];
    }
    return PDCRT_OK;
}

void pdcrt_dealoj_texto(pdcrt_alojador alojador, pdcrt_texto* texto)
{
    pdcrt_dealojar_simple(alojador, texto->contenido, sizeof(char) * texto->longitud);
    pdcrt_dealojar_simple(alojador, texto, sizeof(pdcrt_texto));
}


// Entornos:

pdcrt_error pdcrt_aloj_env(pdcrt_env** env, pdcrt_alojador alojador, size_t env_size)
{
    *env = pdcrt_alojar_simple(alojador, sizeof(pdcrt_env) + sizeof(pdcrt_objeto) * env_size);
    if(!env)
    {
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
          u8"Objeto"
        };
    return tipos[tipo];
}

void pdcrt_objeto_debe_tener_tipo(pdcrt_objeto obj, pdcrt_tipo_de_objeto tipo)
{
    if(obj.tag != tipo)
    {
        fprintf(stderr, u8"Objeto de tipo %s debía tener tipo %s\n", pdcrt_tipo_como_texto(obj.tag), pdcrt_tipo_como_texto(tipo));
        abort();
    }
}

pdcrt_objeto pdcrt_objeto_entero(int v)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_ENTERO;
    obj.value.i = v;
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_numero;
    return obj;
}

pdcrt_objeto pdcrt_objeto_float(float v)
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
    obj.recv = (pdcrt_funcion_generica) &pdcrt_recv_texto;
    return obj;
}

pdcrt_error pdcrt_objeto_aloj_closure(pdcrt_alojador alojador, pdcrt_proc_t proc, size_t env_size, pdcrt_objeto* obj)
{
    obj->tag = PDCRT_TOBJ_CLOSURE;
    obj->value.c.proc = proc;
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
    obj->tag = PDCRT_TOBJ_OBJETO;
    obj->value.o.recv = recv;
    obj->recv = (pdcrt_funcion_generica) &pdcrt_recv_texto;
    return pdcrt_aloj_env(&obj->value.o.attrs, alojador, num_attrs);
}


// Igualdad:

bool pdcrt_objeto_iguales(pdcrt_objeto a, pdcrt_objeto b)
{
    if(a.tag != b.tag)
    {
        return false;
    }
    else
    {
        switch(a.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            return a.value.i == b.value.i;
        case PDCRT_TOBJ_FLOAT:
            return a.value.f == b.value.f;
        case PDCRT_TOBJ_MARCA_DE_PILA:
            return true;
        case PDCRT_TOBJ_CLOSURE:
            return (a.value.c.proc == b.value.c.proc) && (a.value.c.env == b.value.c.env);
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
            assert(0);
        }
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
    default:
        return pdcrt_objeto_iguales(a, b);
    }
}


// Receptores:

static int pdcrt_texto_cmp_lit(pdcrt_texto* lhs, const char* rhs)
{
    size_t rhslon = strlen(rhs);
    if(lhs->longitud != rhslon)
    {
        return lhs->longitud - rhslon;
    }
    else
    {
        for(size_t i = 0; i < lhs->longitud; i++)
        {
            if(lhs->contenido[i] != rhs[i])
            {
                return lhs->contenido[i] - rhs[i];
            }
        }
        return 0;
    }
}

int pdcrt_recv_numero(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo(msj, PDCRT_TOBJ_TEXTO);

#define PDCRT_NUMOP(op)                                                 \
    do                                                                  \
    {                                                                   \
        assert((args == 1) && (rets == 1));                             \
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila); \
        assert((rhs.tag == PDCRT_TOBJ_ENTERO) || (rhs.tag == PDCRT_TOBJ_FLOAT)); \
        switch(rhs.tag)                                                 \
        {                                                               \
        case PDCRT_TOBJ_ENTERO:                                         \
            switch(yo.tag)                                              \
            {                                                           \
            case PDCRT_TOBJ_ENTERO:                                     \
                no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(yo.value.i op rhs.value.i))); \
                break;                                                  \
            case PDCRT_TOBJ_FLOAT:                                      \
                no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_float(yo.value.f op ((float)rhs.value.i)))); \
                break;                                                  \
            default:                                                    \
                assert(0);                                              \
            }                                                           \
            break;                                                      \
        case PDCRT_TOBJ_FLOAT:                                          \
            switch(yo.tag)                                              \
            {                                                           \
            case PDCRT_TOBJ_ENTERO:                                     \
                no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_float(((float)yo.value.i) op rhs.value.f))); \
                break;                                                  \
            case PDCRT_TOBJ_FLOAT:                                      \
                no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_float(yo.value.f op rhs.value.f))); \
                break;                                                  \
            default:                                                    \
                assert(0);                                              \
            }                                                           \
            break;                                                      \
        default:                                                        \
            assert(0);                                                  \
        }                                                               \
        return 0;                                                       \
    } while(0)

    if(pdcrt_texto_cmp_lit(msj.value.t, "operador_+") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "sumar") == 0)
    {
        PDCRT_NUMOP(+);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_-") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "restar") == 0)
    {
        PDCRT_NUMOP(-);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_*") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "multiplicar") == 0)
    {
        PDCRT_NUMOP(*);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_/") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "dividir") == 0)
    {
        PDCRT_NUMOP(/);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_<") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "menorQue") == 0)
    {
        PDCRT_NUMOP(<);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_>") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "mayorQue") == 0)
    {
        PDCRT_NUMOP(>);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_=<") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "menorOIgualA") == 0)
    {
        PDCRT_NUMOP(<=);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "operador_>=") == 0 || pdcrt_texto_cmp_lit(msj.value.t, "mayorOIgualA") == 0)
    {
        PDCRT_NUMOP(>=);
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "comoTexto") == 0)
    {
        assert((args == 0) && (rets == 1));
#define PDCRT_LONGITUD_BUFFER 60
        char buffer[PDCRT_LONGITUD_BUFFER];
        memset(buffer, '\0', PDCRT_LONGITUD_BUFFER);
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            snprintf(buffer, PDCRT_LONGITUD_BUFFER, "%d", yo.value.i);
            break;
        case PDCRT_TOBJ_FLOAT:
            snprintf(buffer, PDCRT_LONGITUD_BUFFER, "%f", yo.value.f);
            break;
        default:
            assert(0);
        }
        size_t lonbuff = strlen(buffer);
        pdcrt_objeto res;
        no_falla(pdcrt_objeto_aloj_texto(&res, marco->contexto->alojador, lonbuff));
        memcpy(res.value.t->contenido, buffer, lonbuff);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, res));
        return 0;
#undef PDCRT_LONGITUD_BUFFER
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "negar") == 0)
    {
        assert((args == 0) && (rets == 1));
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(-yo.value.i)));
            break;
        case PDCRT_TOBJ_FLOAT:
            no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_float(-yo.value.f)));
            break;
        default:
            assert(0);
        }
        return 0;
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "clonar") == 0)
    {
        assert((args == 0) && (rets == 1));
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        return 0;
    }
    else if((pdcrt_texto_cmp_lit(msj.value.t, "igualA") == 0) || (pdcrt_texto_cmp_lit(msj.value.t, "operador_=") == 0))
    {
        assert((args == 1) && (rets == 1));
        pdcrt_objeto rhs = pdcrt_sacar_de_pila(&marco->contexto->pila);
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(pdcrt_objeto_iguales(yo, rhs))));
        return 0;
    }
    else if(pdcrt_texto_cmp_lit(msj.value.t, "truncar") == 0)
    {
        assert((args == 0) && (rets == 1));
        int r;
        switch(yo.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            r = yo.value.i;
            break;
        case PDCRT_TOBJ_FLOAT:
            r = yo.value.f;
            break;
        default:
            assert(0);
        }
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(r)));
        return 0;
    }
    /* else if(pdcrt_texto_cmp_lit(msj.value.t, "") == 0) */
    /* { */
    /*     // */
    /* } */
    else
    {
        printf("mensaje no entendido\n");
        abort();
    }

    return 0;
#undef PDCRT_NUMOP
}

int pdcrt_recv_texto(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo(msj, PDCRT_TOBJ_TEXTO);
    if(pdcrt_texto_cmp_lit(msj.value.t, "longitud") == 0)
    {
        assert((args == 0) && (rets == 1));
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(yo.value.t->longitud)));
        return 0;
    }
    else
    {
        printf("mensaje no entendido\n");
        abort();
    }

    return 0;
}

int pdcrt_recv_closure(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    pdcrt_objeto_debe_tener_tipo(msj, PDCRT_TOBJ_TEXTO);
    if(pdcrt_texto_cmp_lit(msj.value.t, "llamar") == 0)
    {
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, yo));
        return (*yo.value.c.proc)(marco, args + 1, rets);
    }
    else
    {
        printf("mensaje no entendido\n");
        abort();
    }

    return 0;
}

int pdcrt_recv_marca_de_pila(struct pdcrt_marco* marco, pdcrt_objeto yo, pdcrt_objeto msj, int args, int rets)
{
    (void) marco;
    (void) yo;
    (void) msj;
    (void) args;
    (void) rets;
    fprintf(stderr, u8"Error: se trató de enviar un mensaje a una marca de pila.\n");
    abort();
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
        size_t nuevacap = pila->capacidad * 2;
        pdcrt_objeto* nuevosels = pdcrt_realojar_simple(alojador, pila->elementos, pila->capacidad * sizeof(pdcrt_objeto), nuevacap * sizeof(pdcrt_objeto));
        if(!nuevosels)
        {
            return PDCRT_ENOMEM;
        }
        else
        {
            pila->elementos = nuevosels;
            pila->capacidad = nuevacap;
        }
    }
    assert(pila->num_elementos < pila->capacidad);
    pila->elementos[pila->num_elementos++] = val;
    return PDCRT_OK;
}

pdcrt_objeto pdcrt_sacar_de_pila(pdcrt_pila* pila)
{
    return pila->elementos[--pila->num_elementos];
}

pdcrt_objeto pdcrt_cima_de_pila(pdcrt_pila* pila)
{
    return pila->elementos[pila->num_elementos - 1];
}

pdcrt_objeto pdcrt_eliminar_elemento_en_pila(pdcrt_pila* pila, size_t n)
{
    assert(pila->num_elementos > 0);
    size_t I = pila->num_elementos - n - 1;
    pdcrt_objeto r = pila->elementos[I];
    for(size_t i = I; i < (pila->num_elementos - 1); i++)
    {
        pila->elementos[i] = pila->elementos[i + 1];
    }
    pila->num_elementos--;
    return r;
}

void pdcrt_insertar_elemento_en_pila(pdcrt_pila* pila, pdcrt_alojador alojador, size_t n, pdcrt_objeto obj)
{
    pdcrt_empujar_en_pila(pila, alojador, pdcrt_objeto_entero(0));
    size_t I = pila->num_elementos - n - 1;
    for(size_t i = pila->num_elementos - 1; i > I; i--)
    {
        pila->elementos[i] = pila->elementos[i - 1];
    }
    pila->elementos[I] = obj;
}


// Constantes:

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
#define PDCRT_DEINIC_CONST_TXT(cm)                  \
    if(consts->cm != NULL)                          \
    {                                               \
        pdcrt_dealoj_texto(alojador, consts->cm);   \
    }

    consts->operador_mas = NULL;
    consts->operador_menos = NULL;
    consts->operador_por = NULL;
    consts->operador_entre = NULL;
    consts->operador_menorQue = NULL;
    consts->operador_menorOIgualA = NULL;
    consts->operador_mayorQue = NULL;
    consts->operador_mayorOIgualA = NULL;
    consts->operador_igualA = NULL;
    consts->msj_igualA = NULL;
    consts->msj_clonar = NULL;
    consts->msj_llamar = NULL;
    PDCRT_INIC_CONST_TXT(operador_mas, "operador_+");
    PDCRT_INIC_CONST_TXT(operador_menos, "operador_-");
    PDCRT_INIC_CONST_TXT(operador_por, "operador_*");
    PDCRT_INIC_CONST_TXT(operador_entre, "operador_/");
    PDCRT_INIC_CONST_TXT(operador_menorQue, "operador_<");
    PDCRT_INIC_CONST_TXT(operador_menorOIgualA, "operador_=<");
    PDCRT_INIC_CONST_TXT(operador_mayorQue, "operador_>");
    PDCRT_INIC_CONST_TXT(operador_mayorOIgualA, "operador_>=");
    PDCRT_INIC_CONST_TXT(operador_igualA, "operador_=");
    PDCRT_INIC_CONST_TXT(msj_igualA, "igualA");
    PDCRT_INIC_CONST_TXT(msj_clonar, "clonar");
    PDCRT_INIC_CONST_TXT(msj_llamar, "llamar");
error:
    PDCRT_DEINIC_CONST_TXT(operador_mas);
    PDCRT_DEINIC_CONST_TXT(operador_menos);
    PDCRT_DEINIC_CONST_TXT(operador_por);
    PDCRT_DEINIC_CONST_TXT(operador_entre);
    PDCRT_DEINIC_CONST_TXT(operador_menorQue);
    PDCRT_DEINIC_CONST_TXT(operador_menorOIgualA);
    PDCRT_DEINIC_CONST_TXT(operador_mayorQue);
    PDCRT_DEINIC_CONST_TXT(operador_mayorOIgualA);
    PDCRT_DEINIC_CONST_TXT(operador_igualA);
    PDCRT_DEINIC_CONST_TXT(msj_igualA);
    PDCRT_DEINIC_CONST_TXT(msj_clonar);
    PDCRT_DEINIC_CONST_TXT(msj_llamar);

#undef PDCRT_DEINIC_CONST_TXT
#undef PDCRT_INIC_CONST_TXT

    return PDCRT_OK;
}

pdcrt_error pdcrt_registrar_constante_textual(pdcrt_alojador alojador, pdcrt_constantes* consts, size_t idx, pdcrt_texto* texto)
{
    if(idx < consts->num_textos)
    {
        consts->textos[idx] = texto;
    }
    else
    {
        consts->textos = pdcrt_realojar_simple(alojador, consts->textos, consts->num_textos * sizeof(pdcrt_texto*), (consts->num_textos + 1) * sizeof(pdcrt_texto*));
        assert(consts->textos != NULL);
        consts->num_textos += 1;
        consts->textos[idx] = texto;
    }
    return PDCRT_OK;
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

pdcrt_error pdcrt_inic_contexto(pdcrt_contexto* ctx, pdcrt_alojador alojador)
{
    pdcrt_error pderrno;
    if((pderrno = pdcrt_inic_pila(&ctx->pila, alojador)) != PDCRT_OK)
    {
        return pderrno;
    }
    ctx->alojador = alojador;
    ctx->rastrear_marcos = false;
    if((pderrno = pdcrt_aloj_constantes(alojador, &ctx->constantes)) != PDCRT_OK)
    {
        pdcrt_deinic_pila(&ctx->pila, alojador);
        return pderrno;
    }
    return PDCRT_OK;
}

void pdcrt_deinic_contexto(pdcrt_contexto* ctx, pdcrt_alojador alojador)
{
    pdcrt_depurar_contexto(ctx, "deinic");
    pdcrt_deinic_pila(&ctx->pila, alojador);
}

static void pdcrt_depurar_objeto(pdcrt_objeto obj)
{
    switch(obj.tag)
    {
    case PDCRT_TOBJ_ENTERO:
        printf("    i%d\n", obj.value.i);
        break;
    case PDCRT_TOBJ_MARCA_DE_PILA:
        printf("    Marca de pila\n");
        break;
    case PDCRT_TOBJ_FLOAT:
        printf("    f%f\n", obj.value.f);
        break;
    case PDCRT_TOBJ_CLOSURE:
        printf(u8"    Closure/función\n");
        printf(u8"      proc => 0x%zX\n", (intptr_t) obj.value.c.proc);
        printf(u8"      env 0x%zX  #%zd\n", (intptr_t) obj.value.c.env, obj.value.c.env->env_size);
        break;
    default:
        assert(0);
    }
}

void pdcrt_depurar_contexto(pdcrt_contexto* ctx, const char* extra)
{
    printf("Contexto: %s\n", extra);
    printf("  Pila [%zd elementos de %zd max.]\n", ctx->pila.num_elementos, ctx->pila.capacidad);
    for(size_t i = 0; i < ctx->pila.num_elementos; i++)
    {
        pdcrt_objeto obj = ctx->pila.elementos[i];
        pdcrt_depurar_objeto(obj);
    }
}


// Procesar el CLI:

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
                assert(*optind < argc);
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
    static bool check = false;
    assert(!check);
    check = true;

    ctx->rastrear_marcos = false;
    int opt, optind = 1, mostrarAyuda = 0;
    char* optarg = NULL;
    while((opt = pdcrt_getopt(argc, argv, "t:h", &optarg, &optind)) != -1)
    {
        switch(opt)
        {
        case 'h':
            mostrarAyuda = 1;
            break;
        case 't':
            ctx->rastrear_marcos = atoi(optarg) == 1;
            break;
        default:
            assert(0 && u8"opción sin reconocer");
        }
    }
    if(optind != argc)
    {
        puts("Argumentos adicionales inesperados.\nUso: programa [opciones...]\nUsa la opción -h para más ayuda.");
        exit(1);
    }
    if(mostrarAyuda)
    {
        puts("Uso: programa [opciones...] [argumentos...]\n\n"
             "Opciones soportadas:\n"
             "  -t N   Registra las llamadas a funciones si N = 1. No lo hagas si N = 0.\n"
             "  -h     Muestra esta ayuda y termina.");
        exit(1);
    }
}


// Marcos:

pdcrt_error pdcrt_inic_marco(pdcrt_marco* marco, pdcrt_contexto* contexto, size_t num_locales, PDCRT_NULL pdcrt_marco* marco_anterior)
{
    size_t num_real_de_locales = num_locales + PDCRT_NUM_LOCALES_ESP;
    marco->locales = pdcrt_alojar(contexto, sizeof(pdcrt_objeto) * num_real_de_locales);
    if(!marco->locales)
    {
        return PDCRT_ENOMEM;
    }
    marco->contexto = contexto;
    marco->marco_anterior = marco_anterior;
    marco->num_locales = num_real_de_locales;
    return PDCRT_OK;
}

void pdcrt_deinic_marco(pdcrt_marco* marco)
{
    if(marco->contexto->rastrear_marcos)
        pdcrt_depurar_contexto(marco->contexto, "Deinic marco");
    pdcrt_dealojar(marco->contexto, marco->locales, sizeof(pdcrt_objeto) * marco->num_locales);
    marco->num_locales = 0;
}

void pdcrt_fijar_local(pdcrt_marco* marco, pdcrt_local_index n, pdcrt_objeto obj)
{
    assert(n != PDCRT_ID_NIL);
    marco->locales[n + PDCRT_NUM_LOCALES_ESP] = obj;
}

pdcrt_objeto pdcrt_obtener_local(pdcrt_marco* marco, pdcrt_local_index n)
{
    assert(n != PDCRT_ID_NIL);
    return marco->locales[n + PDCRT_NUM_LOCALES_ESP];
}


// Opcodes:

void pdcrt_op_iconst(pdcrt_marco* marco, int c)
{
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(c)));
}

void pdcrt_op_lconst(pdcrt_marco* marco, int c)
{
    pdcrt_objeto txt = pdcrt_objeto_desde_texto(marco->contexto->constantes.textos[c]);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, txt));
}

#define PDCRT_OP(marco, binop)                                          \
    pdcrt_objeto a, b, msj;                                             \
    a = pdcrt_sacar_de_pila(&marco->contexto->pila);                    \
    b = pdcrt_sacar_de_pila(&marco->contexto->pila);                    \
    pdcrt_objeto_debe_tener_tipo(a, PDCRT_TOBJ_ENTERO);                 \
    pdcrt_objeto_debe_tener_tipo(b, PDCRT_TOBJ_ENTERO);                 \
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, a)); \
    msj = pdcrt_objeto_desde_texto(marco->contexto->constantes.binop); \
    PDCRT_ENVIAR_MENSAJE(marco, b, msj, 1, 1);

void pdcrt_op_sum(pdcrt_marco* marco)
{
    PDCRT_OP(marco, operador_mas);
}

void pdcrt_op_sub(pdcrt_marco* marco)
{
    PDCRT_OP(marco, operador_menos);
}

void pdcrt_op_mul(pdcrt_marco* marco)
{
    PDCRT_OP(marco, operador_por);
}

void pdcrt_op_div(pdcrt_marco* marco)
{
    PDCRT_OP(marco, operador_entre);
}

void pdcrt_op_gt(pdcrt_marco* marco)
{
    PDCRT_OP(marco, operador_mayorQue);
}

void pdcrt_op_ge(pdcrt_marco* marco)
{
    PDCRT_OP(marco, operador_mayorOIgualA);
}

void pdcrt_op_lt(pdcrt_marco* marco)
{
    PDCRT_OP(marco, operador_menorQue);
}

void pdcrt_op_le(pdcrt_marco* marco)
{
    PDCRT_OP(marco, operador_menorOIgualA);
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
        pdcrt_objeto_debe_tener_tipo(env, PDCRT_TOBJ_CLOSURE);
        env = env.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP];
    }
    pdcrt_objeto_debe_tener_tipo(env, PDCRT_TOBJ_CLOSURE);
    env.value.c.env->env[((pdcrt_local_index) ind) + PDCRT_NUM_LOCALES_ESP] = obj;
}

void pdcrt_op_lgetc(pdcrt_marco* marco, pdcrt_objeto env, size_t alt, size_t ind)
{
    for(size_t i = 0; i < alt; i++)
    {
        pdcrt_objeto_debe_tener_tipo(env, PDCRT_TOBJ_CLOSURE);
        env = env.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP];
    }
    pdcrt_objeto_debe_tener_tipo(env, PDCRT_TOBJ_CLOSURE);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, env.value.c.env->env[((pdcrt_local_index) ind) + PDCRT_NUM_LOCALES_ESP]));
}

pdcrt_objeto pdcrt_op_open_frame(pdcrt_marco* marco, PDCRT_NULL pdcrt_local_index padreidx, size_t tam)
{
    pdcrt_objeto padre;
    if(padreidx == PDCRT_ID_NIL)
    {
        padre = pdcrt_objeto_entero(0);
    }
    else
    {
        padre = pdcrt_obtener_local(marco, padreidx);
    }
    pdcrt_objeto env;
    no_falla(pdcrt_objeto_aloj_closure(marco->contexto->alojador, NULL, tam, &env));
    for(size_t i = 0; i < env.value.c.env->env_size; i++)
    {
        env.value.c.env->env[i] = pdcrt_objeto_entero(0);
    }
    env.value.c.env->env[PDCRT_NUM_LOCALES_ESP + PDCRT_ID_ESUP] = padre;
    return env;
}

void pdcrt_op_einit(pdcrt_marco* marco, pdcrt_objeto env, size_t i, pdcrt_objeto local)
{
    (void) marco;
    pdcrt_objeto_debe_tener_tipo(env, PDCRT_TOBJ_CLOSURE);
    env.value.c.env->env[i + 1] = local;
}

void pdcrt_op_close_frame(pdcrt_marco* marco, pdcrt_objeto env)
{
    // nada que hacer.
    (void) marco;
    (void) env;
}

void pdcrt_op_mkclz(pdcrt_marco* marco, pdcrt_local_index env, pdcrt_proc_t proc)
{
    pdcrt_objeto cima = pdcrt_obtener_local(marco, env);
    pdcrt_objeto_debe_tener_tipo(cima, PDCRT_TOBJ_CLOSURE);
    pdcrt_objeto nuevo_env;
    nuevo_env.tag = PDCRT_TOBJ_CLOSURE;
    nuevo_env.value.c.env = cima.value.c.env;
    nuevo_env.value.c.proc = proc;
    nuevo_env.recv = (pdcrt_funcion_generica) &pdcrt_recv_closure;
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, nuevo_env));
}

void pdcrt_op_mk0clz(pdcrt_marco* marco, pdcrt_proc_t proc)
{
    pdcrt_objeto clz;
    clz.tag = PDCRT_TOBJ_CLOSURE;
    clz.value.c.proc = proc;
    clz.recv = (pdcrt_funcion_generica) &pdcrt_recv_closure;
    no_falla(pdcrt_aloj_env(&clz.value.c.env, marco->contexto->alojador, 0));
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, clz));
}

void pdcrt_assert_params(pdcrt_marco* marco, int nparams)
{
    if(marco->contexto->rastrear_marcos)
        pdcrt_depurar_contexto(marco->contexto, "P1 assert_params");
    pdcrt_objeto marca = pdcrt_sacar_de_pila(&marco->contexto->pila);
    if(marca.tag != PDCRT_TOBJ_MARCA_DE_PILA)
    {
        fprintf(stderr, "Se esperaba una marca de pila pero se obtuvo un %s\n", pdcrt_tipo_como_texto(marca.tag));
        abort();
    }
    if(marco->contexto->pila.num_elementos < (size_t)nparams)
    {
        fprintf(stderr, "Se esperaban al menos %d elementos.\n", nparams);
        abort();
    }
    for(size_t i = marco->contexto->pila.num_elementos - nparams; i < marco->contexto->pila.num_elementos; i++)
    {
        pdcrt_objeto obj = marco->contexto->pila.elementos[i];
        if(obj.tag == PDCRT_TOBJ_MARCA_DE_PILA)
        {
            fprintf(stderr, "Faltaron elementos en el marco de llamada\n");
            abort();
        }
    }
    pdcrt_insertar_elemento_en_pila(&marco->contexto->pila, marco->contexto->alojador, nparams, marca);
    if(marco->contexto->rastrear_marcos)
        pdcrt_depurar_contexto(marco->contexto, "P2 assert_params");
}

void pdcrt_op_dyncall(pdcrt_marco* marco, int acepta, int devuelve)
{
    pdcrt_objeto cima = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo(cima, PDCRT_TOBJ_CLOSURE);
    PDCRT_ENVIAR_MENSAJE(marco, cima, pdcrt_objeto_desde_texto(marco->contexto->constantes.msj_llamar), acepta, devuelve);
}

void pdcrt_op_call(pdcrt_marco* marco, pdcrt_proc_t proc, int acepta, int devuelve)
{
    if(marco->contexto->rastrear_marcos)
        pdcrt_depurar_contexto(marco->contexto, "precall");
    (*proc)(marco, acepta, devuelve);
    if(marco->contexto->rastrear_marcos)
        pdcrt_depurar_contexto(marco->contexto, "postcall");
}

void pdcrt_op_retn(pdcrt_marco* marco, int n)
{
    for(size_t i = marco->contexto->pila.num_elementos - n; i < marco->contexto->pila.num_elementos; i++)
    {
        pdcrt_objeto obj = marco->contexto->pila.elementos[i];
        if(obj.tag == PDCRT_TOBJ_MARCA_DE_PILA)
        {
            fprintf(stderr, "Trato de devolver a traves de una marca de pila\n");
            abort();
        }
    }
    pdcrt_objeto marca = pdcrt_eliminar_elemento_en_pila(&marco->contexto->pila, n);
    pdcrt_objeto_debe_tener_tipo(marca, PDCRT_TOBJ_MARCA_DE_PILA);
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
    pdcrt_objeto_debe_tener_tipo(obj, PDCRT_TOBJ_ENTERO);
    return obj.value.i;
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
        assert(n > 0);
        pdcrt_pila* pila = &marco->contexto->pila;
        assert(pila->num_elementos > (size_t)n);
        // Guarda el elemento TOP-N
        pdcrt_objeto obj = pila->elementos[pila->num_elementos - 1 - (size_t)n];
        // Mueve todos los elementos de I a I-1
        for(size_t i = pila->num_elementos - 1 - (size_t)n; i < (pila->num_elementos - 1); i++)
        {
            pila->elementos[i] = pila->elementos[i + 1];
        }
        // Restaura el elemento guardado
        pila->elementos[pila->num_elementos - 1] = obj;
    }
}

void pdcrt_op_cmp(pdcrt_marco* marco, pdcrt_cmp cmp)
{
    pdcrt_objeto a, b;
    a = pdcrt_sacar_de_pila(&marco->contexto->pila);
    b = pdcrt_sacar_de_pila(&marco->contexto->pila);
    if(cmp == PDCRT_CMP_EQ)
    {
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila,
                                       marco->contexto->alojador,
                                       pdcrt_objeto_entero(pdcrt_objeto_iguales(a, b))));
    }
    else
    {
        no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila,
                                       marco->contexto->alojador,
                                       pdcrt_objeto_entero(!pdcrt_objeto_iguales(a, b))));
    }
}

void pdcrt_op_not(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo(obj, PDCRT_TOBJ_ENTERO);
    no_falla(pdcrt_empujar_en_pila(&marco->contexto->pila, marco->contexto->alojador, pdcrt_objeto_entero(!obj.value.i)));
}

void pdcrt_op_mtrue(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    pdcrt_objeto_debe_tener_tipo(obj, PDCRT_TOBJ_ENTERO);
    assert(obj.value.i != 0);
}

void pdcrt_op_prn(pdcrt_marco* marco)
{
    pdcrt_objeto obj = pdcrt_sacar_de_pila(&marco->contexto->pila);
    switch(obj.tag)
    {
    case PDCRT_TOBJ_ENTERO:
        printf("%d", obj.value.i);
        break;
    case PDCRT_TOBJ_FLOAT:
        printf("%f", obj.value.f);
        break;
    case PDCRT_TOBJ_TEXTO:
        for(size_t i = 0; i < obj.value.t->longitud; i++)
        {
            printf("%c", obj.value.t->contenido[i]);
        }
        break;
    default:
        assert(0 && "cannot prn obj");
    }
}

void pdcrt_op_nl(pdcrt_marco* marco)
{
    (void) marco;
    printf("\n");
}
