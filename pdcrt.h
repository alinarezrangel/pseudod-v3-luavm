#ifndef PDCRT_H
#define PDCRT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>


typedef struct pdcrt_objeto
{
    enum pdcrt_tipo_de_objeto
    {
        PDCRT_TOBJ_ENTERO = 0,
        PDCRT_TOBJ_FLOAT = 1,
        PDCRT_TOBJ_MARCA_DE_PILA = 2,
    } tag;
    union
    {
        int i;
    } value;
} pdcrt_objeto;

typedef enum pdcrt_tipo_de_objeto pdcrt_tipo_de_objeto;

const char* pdcrt_tipo_como_texto(pdcrt_tipo_de_objeto tipo);

void pdcrt_objeto_debe_tener_tipo(pdcrt_objeto obj, pdcrt_tipo_de_objeto tipo);

pdcrt_objeto pdcrt_objeto_entero(int v);
pdcrt_objeto pdcrt_objeto_marca_de_pila(void);

typedef struct pdcrt_pila
{
    pdcrt_objeto* elementos;
    size_t num_elementos;
    size_t capacidad;
} pdcrt_pila;

typedef enum pdcrt_error
{
    PDCRT_OK = 0,
    PDCRT_ENOMEM = 1,
    PDCRT_WPARTIALMEM = 2,
} pdcrt_error;

const char* pdcrt_perror(pdcrt_error err);

pdcrt_error pdcrt_aloj_pila(pdcrt_pila* pila);
void pdcrt_dealoj_pila(pdcrt_pila* pila);
pdcrt_error pdcrt_empujar_en_pila(pdcrt_pila* pila, pdcrt_objeto val);
pdcrt_objeto pdcrt_sacar_de_pila(pdcrt_pila* pila);
pdcrt_objeto pdcrt_cima_de_pila(pdcrt_pila* pila);
pdcrt_objeto pdcrt_eliminar_elemento_en_pila(pdcrt_pila* pila, size_t n);
void pdcrt_insertar_elemento_en_pila(pdcrt_pila* pila, size_t n, pdcrt_objeto obj);

typedef struct pdcrt_contexto
{
    pdcrt_pila pila;
} pdcrt_contexto;

pdcrt_error pdcrt_inic_contexto(pdcrt_contexto* ctx);
void pdcrt_deinic_contexto(pdcrt_contexto* ctx);
void pdcrt_depurar_contexto(pdcrt_contexto* ctx, const char* extra);


#define PDCRT_MAIN()                            \
    int main(int argc, char* argv[])

#define PDCRT_MAIN_PRELUDE()                                    \
    pdcrt_contexto ctx_real;                                    \
    pdcrt_error pderrno;                                        \
    pdcrt_contexto* ctx = &ctx_real;                            \
    if((pderrno = pdcrt_inic_contexto(&ctx_real)) != PDCRT_OK)  \
    {                                                           \
        puts(pdcrt_perror(pderrno));                            \
        exit(EXIT_FAILURE);                                     \
    }                                                           \
    do {} while(0)

#define PDCRT_MAIN_POSTLUDE()                   \
    pdcrt_deinic_contexto(ctx);                 \
    exit(EXIT_SUCCESS);                         \
    do {} while(0)

#define PDCRT_LOCAL(ctx, idx, name)             \
    pdcrt_objeto name
#define PDCRT_SET_LVAR(ctx, name, val)          \
    name = val
#define PDCRT_GET_LVAR(ctx, name)               \
    name

#define PDCRT_PROC(name)                            \
    int pdproc_##name(pdcrt_contexto* ctx) // {}
#define PDCRT_PROC_PRELUDE(ctx, name)                                   \
    do                                                                  \
    {                                                                   \
        pdcrt_depurar_contexto(ctx, "P1 " #name);                       \
        pdcrt_empujar_en_pila(&ctx->pila, pdcrt_objeto_marca_de_pila()); \
        pdcrt_depurar_contexto(ctx, "P2 " #name);                       \
    }                                                                   \
    while(0)
#define PDCRT_ASSERT_PARAMS(ctx, nparams)       \
    pdcrt_assert_params(ctx, nparams)
#define PDCRT_PARAM(ctx, idx, param)                        \
    pdcrt_objeto param = pdcrt_sacar_de_pila(&ctx->pila)
#define PDCRT_PROC_POSTLUDE(ctx, name)          \
    do {} while(0)
#define PDCRT_PROC_NAME(ctx, name)              \
    &pdproc_##name
#define PDCRT_DECLARE_PROC(name)                \
    PDCRT_PROC(name);

typedef int (*pdcrt_proc_t)(pdcrt_contexto* ctx);

void pdcrt_op_iconst(pdcrt_contexto* ctx, int c);
void pdcrt_op_sum(pdcrt_contexto* ctx);
void pdcrt_op_sub(pdcrt_contexto* ctx);
void pdcrt_op_mul(pdcrt_contexto* ctx);
void pdcrt_op_div(pdcrt_contexto* ctx);
pdcrt_objeto pdcrt_op_lset(pdcrt_contexto* ctx);
void pdcrt_op_lget(pdcrt_contexto* ctx, pdcrt_objeto v);
void pdcrt_op_call(pdcrt_contexto* ctx, pdcrt_proc_t proc, int acepta, int devuelve);
void pdcrt_op_retn(pdcrt_contexto* ctx, int n);
void pdcrt_assert_params(pdcrt_contexto* ctx, int nparams);
int pdcrt_real_return(pdcrt_contexto* ctx);
int pdcrt_passthru_return(pdcrt_contexto* ctx);

#endif /* PDCRT_H */
