#ifndef PDCRT_H
#define PDCRT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Marca un puntero que puede ser nulo
#define PDCRT_NULL

// Marca un parámetro como "de salida".
#define PDCRT_OUT

// Indica que un arreglo o puntero es realmente un arreglo de tamaño
// dinámico. `campo_tam` es una variable o campo que contiene el tamaño del
// arreglo.
#define PDCRT_ARR(campo_tam)

typedef enum pdcrt_error
{
    PDCRT_OK = 0,
    PDCRT_ENOMEM = 1,
    PDCRT_WPARTIALMEM = 2,
} pdcrt_error;

const char* pdcrt_perror(pdcrt_error err);

typedef void* (*pdcrt_func_alojar)(void* datos_del_usuario, void* ptr, size_t tam_viejo, size_t tam_nuevo);

typedef struct pdcrt_alojador
{
    pdcrt_func_alojar alojar;
    void* datos;
} pdcrt_alojador;

struct pdcrt_contexto;
struct pdcrt_objeto;
struct pdcrt_marco;

typedef int (*pdcrt_proc_t)(struct pdcrt_marco* marco, int args, int rets);

typedef struct pdcrt_env
{
    size_t env_size;
    PDCRT_ARR(env_size) struct pdcrt_objeto* env[];
} pdcrt_env;

pdcrt_error pdcrt_aloj_env(pdcrt_alojador alojador, size_t env_size, PDCRT_OUT pdcrt_env** env);
void pdcrt_dealoj_env(pdcrt_alojador alojador, pdcrt_env* env);

typedef struct pdcrt_closure
{
    pdcrt_proc_t proc;
    pdcrt_env* env;
} pdcrt_closure;

typedef struct pdcrt_objeto
{
    enum pdcrt_tipo_de_objeto
    {
        PDCRT_TOBJ_ENTERO = 0,
        PDCRT_TOBJ_FLOAT = 1,
        PDCRT_TOBJ_MARCA_DE_PILA = 2,
        PDCRT_TOBJ_CLOSURE = 3
    } tag;
    union
    {
        int i;
        float f;
        pdcrt_closure c;
    } value;
} pdcrt_objeto;

typedef enum pdcrt_tipo_de_objeto pdcrt_tipo_de_objeto;

const char* pdcrt_tipo_como_texto(pdcrt_tipo_de_objeto tipo);

void pdcrt_objeto_debe_tener_tipo(pdcrt_objeto obj, pdcrt_tipo_de_objeto tipo);

pdcrt_objeto pdcrt_objeto_entero(int v);
pdcrt_objeto pdcrt_objeto_float(float v);
pdcrt_objeto pdcrt_objeto_marca_de_pila(void);
pdcrt_error pdcrt_objeto_aloj_closure(pdcrt_alojador alojador, pdcrt_proc_t proc, size_t env_size, PDCRT_OUT pdcrt_objeto* out);

typedef struct pdcrt_pila
{
    PDCRT_ARR(capacidad) pdcrt_objeto* elementos;
    size_t num_elementos;
    size_t capacidad;
} pdcrt_pila;

pdcrt_error pdcrt_inic_pila(PDCRT_OUT pdcrt_pila* pila);
void pdcrt_deinic_pila(pdcrt_pila* pila);
pdcrt_error pdcrt_empujar_en_pila(pdcrt_pila* pila, pdcrt_objeto val);
pdcrt_objeto pdcrt_sacar_de_pila(pdcrt_pila* pila);
pdcrt_objeto pdcrt_cima_de_pila(pdcrt_pila* pila);
pdcrt_objeto pdcrt_eliminar_elemento_en_pila(pdcrt_pila* pila, size_t n);
void pdcrt_insertar_elemento_en_pila(pdcrt_pila* pila, size_t n, pdcrt_objeto obj);

typedef struct pdcrt_contexto
{
    pdcrt_pila* pila;
    pdcrt_alojador alojador;
} pdcrt_contexto;

pdcrt_alojador pdcrt_alojador_de_malloc(void);
pdcrt_error pdcrt_aloj_alojador_de_arena(pdcrt_alojador* aloj);
void pdcrt_dealoj_alojador_de_arena(pdcrt_alojador aloj);

pdcrt_error pdcrt_inic_contexto(pdcrt_contexto* ctx, pdcrt_pila* pila, pdcrt_alojador alojador);
void pdcrt_deinic_contexto(pdcrt_contexto* ctx);
void pdcrt_depurar_contexto(pdcrt_contexto* ctx, const char* extra);

void* pdcrt_alojar(pdcrt_contexto* ctx, size_t tam);
void pdcrt_dealojar(pdcrt_contexto* ctx, void* ptr, size_t tam);
void* pdcrt_realojar(pdcrt_contexto* ctx, void* ptr, size_t tam_actual, size_t tam_nuevo);
void* pdcrt_alojar_simple(pdcrt_alojador alojador, size_t tam);
void pdcrt_dealojar_simple(pdcrt_alojador alojador, void* ptr, size_t tam);
void* pdcrt_realojar_simple(pdcrt_alojador alojador, void* ptr, size_t tam_actual, size_t tam_nuevo);

typedef struct pdcrt_marco
{
    pdcrt_contexto* contexto;
    PDCRT_ARR(num_locales) pdcrt_objeto* locales;
    size_t num_locales;
    PDCRT_NULL struct pdcrt_marco* marco_anterior;
} pdcrt_marco;

pdcrt_error pdcrt_inic_marco(pdcrt_marco* marco, pdcrt_contexto* contexto, size_t num_locales, PDCRT_NULL pdcrt_marco* marco_anterior);
void pdcrt_deinic_marco(pdcrt_marco* marco);
void pdcrt_fijar_local(pdcrt_marco* marco, size_t n, pdcrt_objeto obj);
pdcrt_objeto pdcrt_obtener_local(pdcrt_marco* marco, size_t n);


#define PDCRT_MAIN()                            \
    int main(int argc, char* argv[])

#define PDCRT_MAIN_PRELUDE(nlocals)                                     \
    pdcrt_contexto ctx_real;                                            \
    pdcrt_error pderrno;                                                \
    pdcrt_contexto* ctx = &ctx_real;                                    \
    pdcrt_marco marco_real;                                             \
    pdcrt_marco* marco = &marco_real;                                   \
    pdcrt_pila pila;                                                    \
    pdcrt_alojador aloj;                                                \
    if((pderrno = pdcrt_aloj_alojador_de_arena(&aloj)) != PDCRT_OK)     \
    {                                                                   \
        puts(pdcrt_perror(pderrno));                                    \
        exit(EXIT_FAILURE);                                             \
    }                                                                   \
    if((pderrno = pdcrt_inic_pila(&pila)) != PDCRT_OK)                  \
    {                                                                   \
        puts(pdcrt_perror(pderrno));                                    \
        exit(EXIT_FAILURE);                                             \
    }                                                                   \
    if((pderrno = pdcrt_inic_contexto(&ctx_real, &pila, aloj)) != PDCRT_OK) \
    {                                                                   \
        puts(pdcrt_perror(pderrno));                                    \
        exit(EXIT_FAILURE);                                             \
    }                                                                   \
    if((pderrno = pdcrt_inic_marco(&marco_real, ctx, nlocals, NULL)))   \
    {                                                                   \
        puts(pdcrt_perror(pderrno));                                    \
        exit(EXIT_FAILURE);                                             \
    }                                                                   \
    do {} while(0)

#define PDCRT_MAIN_POSTLUDE()                   \
    do                                          \
    {                                           \
        pdcrt_deinic_marco(&marco_real);        \
        pdcrt_deinic_contexto(ctx);             \
        pdcrt_dealoj_alojador_de_arena(aloj);   \
        exit(EXIT_SUCCESS);                     \
    }                                           \
    while(0)

#define PDCRT_LOCAL(idx, name)                              \
    pdcrt_fijar_local(marco, idx, pdcrt_objeto_entero(0))
#define PDCRT_SET_LVAR(idx, val)                                    \
    pdcrt_fijar_local(marco, idx, pdcrt_sacar_de_pila(ctx->pila))
#define PDCRT_GET_LVAR(idx)                     \
    pdcrt_obtener_local(marco, idx)

#define PDCRT_PROC(name)                                                \
    int pdproc_##name(pdcrt_marco* name##marco_anterior, int name##nargs, int name##nrets) // {}
#define PDCRT_PROC_PRELUDE(name, nlocals)                               \
    pdcrt_marco marco_real;                                             \
    pdcrt_error pderrno;                                                \
    pdcrt_contexto* ctx = name##marco_anterior->contexto;               \
    pdcrt_marco* marco = &marco_real;                                   \
    do                                                                  \
    {                                                                   \
        if((pderrno = pdcrt_inic_marco(&marco_real, ctx, nlocals, name##marco_anterior))) \
        {                                                               \
            puts(pdcrt_perror(pderrno));                                \
            exit(EXIT_FAILURE);                                         \
        }                                                               \
        pdcrt_depurar_contexto(ctx, "P1 " #name);                       \
        pdcrt_empujar_en_pila(ctx->pila, pdcrt_objeto_marca_de_pila()); \
        pdcrt_depurar_contexto(ctx, "P2 " #name);                       \
    }                                                                   \
    while(0)
#define PDCRT_ASSERT_PARAMS(nparams)       \
    pdcrt_assert_params(marco, nparams)
#define PDCRT_PARAM(idx, param)                        \
    pdcrt_fijar_local(marco, idx, pdcrt_sacar_de_pila(ctx->pila))
#define PDCRT_PROC_POSTLUDE(name)          \
    do {} while(0)
#define PDCRT_PROC_NAME(name)              \
    &pdproc_##name
#define PDCRT_DECLARE_PROC(name)                \
    PDCRT_PROC(name);

void pdcrt_op_iconst(pdcrt_marco* marco, int c);
void pdcrt_op_sum(pdcrt_marco* marco);
void pdcrt_op_sub(pdcrt_marco* marco);
void pdcrt_op_mul(pdcrt_marco* marco);
void pdcrt_op_div(pdcrt_marco* marco);
pdcrt_objeto pdcrt_op_lset(pdcrt_marco* marco);
void pdcrt_op_lget(pdcrt_marco* marco, pdcrt_objeto v);
void pdcrt_op_mkenv(pdcrt_marco* marco, size_t tam);
void pdcrt_op_eset(pdcrt_marco* marco, pdcrt_objeto env, size_t i);
void pdcrt_op_eget(pdcrt_marco* marco, pdcrt_objeto env, size_t i);
void pdcrt_op_mkclz(pdcrt_marco* marco, pdcrt_proc_t proc);
void pdcrt_op_mk0clz(pdcrt_marco* marco, pdcrt_proc_t proc);
void pdcrt_op_dyncall(pdcrt_marco* marco, int acepta, int devuelve);
void pdcrt_op_call(pdcrt_marco* marco, pdcrt_proc_t proc, int acepta, int devuelve);
void pdcrt_op_retn(pdcrt_marco* marco, int n);
void pdcrt_assert_params(pdcrt_marco* marco, int nparams);
int pdcrt_real_return(pdcrt_marco* marco);
int pdcrt_passthru_return(pdcrt_marco* marco);

#endif /* PDCRT_H */
