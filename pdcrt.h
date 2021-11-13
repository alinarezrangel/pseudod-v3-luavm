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

// Marca un parámetro como "de entrada".
#define PDCRT_IN

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

typedef PDCRT_NULL void* (*pdcrt_func_alojar)(void* datos_del_usuario, PDCRT_IN PDCRT_NULL void* ptr, size_t tam_viejo, size_t tam_nuevo);

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

pdcrt_error pdcrt_aloj_env(PDCRT_OUT pdcrt_env** env, pdcrt_alojador alojador, size_t env_size);
void pdcrt_dealoj_env(pdcrt_env* env, pdcrt_alojador alojador);

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

bool pdcrt_objeto_iguales(pdcrt_objeto a, pdcrt_objeto b);
bool pdcrt_objeto_identicos(pdcrt_objeto a, pdcrt_objeto b);

typedef struct pdcrt_pila
{
    PDCRT_ARR(capacidad) pdcrt_objeto* elementos;
    size_t num_elementos;
    size_t capacidad;
} pdcrt_pila;

pdcrt_error pdcrt_inic_pila(PDCRT_OUT pdcrt_pila* pila, pdcrt_alojador alojador);
void pdcrt_deinic_pila(pdcrt_pila* pila, pdcrt_alojador alojador);
pdcrt_error pdcrt_empujar_en_pila(pdcrt_pila* pila, pdcrt_alojador alojador, pdcrt_objeto val);
pdcrt_objeto pdcrt_sacar_de_pila(pdcrt_pila* pila);
pdcrt_objeto pdcrt_cima_de_pila(pdcrt_pila* pila);
pdcrt_objeto pdcrt_eliminar_elemento_en_pila(pdcrt_pila* pila, size_t n);
void pdcrt_insertar_elemento_en_pila(pdcrt_pila* pila, pdcrt_alojador alojador, size_t n, pdcrt_objeto obj);

typedef struct pdcrt_contexto
{
    pdcrt_pila pila;
    pdcrt_alojador alojador;
    bool rastrear_marcos;
} pdcrt_contexto;

pdcrt_alojador pdcrt_alojador_de_malloc(void);
pdcrt_error pdcrt_aloj_alojador_de_arena(pdcrt_alojador* aloj);
void pdcrt_dealoj_alojador_de_arena(pdcrt_alojador aloj);

pdcrt_error pdcrt_inic_contexto(pdcrt_contexto* ctx, pdcrt_alojador alojador);
void pdcrt_deinic_contexto(pdcrt_contexto* ctx, pdcrt_alojador alojador);
void pdcrt_depurar_contexto(pdcrt_contexto* ctx, const char* extra);

void* pdcrt_alojar(pdcrt_contexto* ctx, size_t tam);
void pdcrt_dealojar(pdcrt_contexto* ctx, void* ptr, size_t tam);
void* pdcrt_realojar(pdcrt_contexto* ctx, PDCRT_NULL void* ptr, size_t tam_actual, size_t tam_nuevo);
void* pdcrt_alojar_simple(pdcrt_alojador alojador, size_t tam);
void pdcrt_dealojar_simple(pdcrt_alojador alojador, void* ptr, size_t tam);
void* pdcrt_realojar_simple(pdcrt_alojador alojador, PDCRT_NULL void* ptr, size_t tam_actual, size_t tam_nuevo);

void pdcrt_procesar_cli(pdcrt_contexto* ctx, int argc, char* argv[]);

typedef struct pdcrt_marco
{
    pdcrt_contexto* contexto;
    PDCRT_ARR(num_locales) pdcrt_objeto* locales;
    size_t num_locales;
    PDCRT_NULL struct pdcrt_marco* marco_anterior;
} pdcrt_marco;

typedef long pdcrt_local_index;

pdcrt_error pdcrt_inic_marco(pdcrt_marco* marco, pdcrt_contexto* contexto, size_t num_locales, PDCRT_NULL pdcrt_marco* marco_anterior);
void pdcrt_deinic_marco(pdcrt_marco* marco);
void pdcrt_fijar_local(pdcrt_marco* marco, pdcrt_local_index n, pdcrt_objeto obj);
pdcrt_objeto pdcrt_obtener_local(pdcrt_marco* marco, pdcrt_local_index n);


#define PDCRT_ID_EACT -1
#define PDCRT_ID_ESUP -2
#define PDCRT_ID_NIL -3
#define PDCRT_NAME_EACT pdcrt_special_eact
#define PDCRT_NAME_ESUP pdcrt_special_esup
#define PDCRT_NAME_NIL pdcrt_special_nil
#define PDCRT_NUM_LOCALES_ESP 2

#define PDCRT_MAIN()                            \
    int main(int argc, char* argv[])

#define PDCRT_RETURN() goto pdcrt_return_point

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
        exit(EXIT_FAILURE);                                             \
    }                                                                   \
    if((pderrno = pdcrt_inic_contexto(&ctx_real, aloj)) != PDCRT_OK) \
    {                                                                   \
        puts(pdcrt_perror(pderrno));                                    \
        exit(EXIT_FAILURE);                                             \
    }                                                                   \
    pdcrt_procesar_cli(ctx, argc, argv);                                \
    if((pderrno = pdcrt_inic_marco(&marco_real, ctx, nlocals, NULL)))   \
    {                                                                   \
        puts(pdcrt_perror(pderrno));                                    \
        exit(EXIT_FAILURE);                                             \
    }                                                                   \
    do {} while(0)

#define PDCRT_MAIN_POSTLUDE()                       \
    do                                              \
    {                                               \
        pdcrt_deinic_marco(&marco_real);            \
        pdcrt_deinic_contexto(ctx, aloj);           \
        pdcrt_dealoj_alojador_de_arena(aloj);       \
        exit(EXIT_SUCCESS);                         \
    }                                               \
    while(0)

#define PDCRT_LOCAL(idx, name)                              \
    pdcrt_fijar_local(marco, idx, pdcrt_objeto_entero(0))
#define PDCRT_SET_LVAR(idx, val)                \
    pdcrt_fijar_local(marco, idx, val)
#define PDCRT_GET_LVAR(idx)                     \
    pdcrt_obtener_local(marco, idx)
#define PDCRT_LABEL(idx)                        \
        pdcrt_label_##idx

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
        if(marco->contexto->rastrear_marcos)                            \
            pdcrt_depurar_contexto(ctx, "P1 " #name);                   \
        pdcrt_empujar_en_pila(&ctx->pila, ctx->alojador, pdcrt_objeto_marca_de_pila()); \
        if(marco->contexto->rastrear_marcos)                            \
            pdcrt_depurar_contexto(ctx, "P2 " #name);                   \
    }                                                                   \
    while(0)
#define PDCRT_ASSERT_PARAMS(nparams)            \
    pdcrt_assert_params(marco, nparams)
#define PDCRT_PARAM(idx, param)                        \
    pdcrt_fijar_local(marco, idx, pdcrt_sacar_de_pila(&ctx->pila))
#define PDCRT_PROC_POSTLUDE(name)               \
    pdcrt_return_point: do {} while(0)
#define PDCRT_PROC_NAME(name)                   \
    &pdproc_##name
#define PDCRT_DECLARE_PROC(name)                \
    PDCRT_PROC(name);

void pdcrt_op_iconst(pdcrt_marco* marco, int c);

void pdcrt_op_sum(pdcrt_marco* marco);
void pdcrt_op_sub(pdcrt_marco* marco);
void pdcrt_op_mul(pdcrt_marco* marco);
void pdcrt_op_div(pdcrt_marco* marco);
void pdcrt_op_gt(pdcrt_marco* marco);
void pdcrt_op_ge(pdcrt_marco* marco);
void pdcrt_op_lt(pdcrt_marco* marco);
void pdcrt_op_le(pdcrt_marco* marco);

void pdcrt_op_pop(pdcrt_marco* marco);

pdcrt_objeto pdcrt_op_lset(pdcrt_marco* marco);
void pdcrt_op_lget(pdcrt_marco* marco, pdcrt_objeto v);

void pdcrt_op_mkenv(pdcrt_marco* marco, size_t tam);
void pdcrt_op_eset(pdcrt_marco* marco, pdcrt_objeto env, pdcrt_local_index i);
void pdcrt_op_eget(pdcrt_marco* marco, pdcrt_objeto env, pdcrt_local_index i);
void pdcrt_op_lsetc(pdcrt_marco* marco, pdcrt_objeto env, size_t alt, size_t ind);
void pdcrt_op_lgetc(pdcrt_marco* marco, pdcrt_objeto env, size_t alt, size_t ind);

pdcrt_objeto pdcrt_op_open_frame(pdcrt_marco* marco, PDCRT_NULL pdcrt_local_index padreidx, size_t tam);
void pdcrt_op_einit(pdcrt_marco* marco, pdcrt_objeto env, size_t i, pdcrt_objeto local);
void pdcrt_op_close_frame(pdcrt_marco* marco, pdcrt_objeto env);

void pdcrt_op_mkclz(pdcrt_marco* marco, pdcrt_local_index env, pdcrt_proc_t proc);
void pdcrt_op_mk0clz(pdcrt_marco* marco, pdcrt_proc_t proc);

void pdcrt_assert_params(pdcrt_marco* marco, int nparams);

void pdcrt_op_dyncall(pdcrt_marco* marco, int acepta, int devuelve);
void pdcrt_op_call(pdcrt_marco* marco, pdcrt_proc_t proc, int acepta, int devuelve);

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

void pdcrt_op_cmp(pdcrt_marco* marco, pdcrt_cmp cmp);
void pdcrt_op_not(pdcrt_marco* marco);
void pdcrt_op_mtrue(pdcrt_marco* marco);

void pdcrt_op_prn(pdcrt_marco* marco);
void pdcrt_op_nl(pdcrt_marco* marco);

#endif /* PDCRT_H */
