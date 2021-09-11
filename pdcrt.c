#include "pdcrt.h"

#include <assert.h>


const char* pdcrt_tipo_como_texto(pdcrt_tipo_de_objeto tipo)
{
    static const char* const tipos[] =
        { u8"Entero",
          u8"Float",
          u8"Marca de pila"
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
    return obj;
}

pdcrt_objeto pdcrt_objeto_marca_de_pila(void)
{
    pdcrt_objeto obj;
    obj.tag = PDCRT_TOBJ_MARCA_DE_PILA;
    return obj;
}

const char* pdcrt_perror(pdcrt_error err)
{
    static const char* const errores[] =
        { u8"Ok",
          u8"No hay memoria",
          u8"No se pudo alojar más memoria"
        };
    return errores[err];
}

pdcrt_error pdcrt_aloj_pila(pdcrt_pila* pila)
{
    pila->capacidad = 1;
    pila->num_elementos = 0;
    pila->elementos = malloc(sizeof(pdcrt_objeto) * pila->capacidad);
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

void pdcrt_dealoj_pila(pdcrt_pila* pila)
{
    pila->capacidad = 0;
    pila->num_elementos = 0;
    free(pila->elementos);
}

pdcrt_error pdcrt_empujar_en_pila(pdcrt_pila* pila, pdcrt_objeto val)
{
    if(pila->num_elementos >= pila->capacidad)
    {
        size_t nuevacap = pila->capacidad * 2;
        pdcrt_objeto* nuevosels = realloc(pila->elementos, nuevacap * sizeof(pdcrt_objeto));
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
    size_t I = pila->num_elementos - n - 1;
    pdcrt_objeto r = pila->elementos[I];
    for(size_t i = I; i < pila->num_elementos; i++)
    {
        pila->elementos[i] = pila->elementos[i + 1];
    }
    pila->num_elementos--;
    return r;
}

void pdcrt_insertar_elemento_en_pila(pdcrt_pila* pila, size_t n, pdcrt_objeto obj)
{
    pdcrt_empujar_en_pila(pila, pdcrt_objeto_entero(0));
    size_t I = pila->num_elementos - n - 1;
    for(size_t i = pila->num_elementos - 1; i > I; i--)
    {
        pila->elementos[i] = pila->elementos[i - 1];
    }
    pila->elementos[I] = obj;
}

pdcrt_error pdcrt_inic_contexto(pdcrt_contexto* ctx)
{
    pdcrt_error err;
    if((err = pdcrt_aloj_pila(&ctx->pila)) != PDCRT_OK)
    {
        return err;
    }
    return PDCRT_OK;
}

void pdcrt_deinic_contexto(pdcrt_contexto* ctx)
{
    pdcrt_depurar_contexto(ctx, "deinic");
    pdcrt_dealoj_pila(&ctx->pila);
}

void pdcrt_depurar_contexto(pdcrt_contexto* ctx, const char* extra)
{
    printf("Contexto: %s\n", extra);
    printf("  Pila [%zd elementos de %zd max.]\n", ctx->pila.num_elementos, ctx->pila.capacidad);
    for(size_t i = 0; i < ctx->pila.num_elementos; i++)
    {
        pdcrt_objeto obj = ctx->pila.elementos[i];
        switch(obj.tag)
        {
        case PDCRT_TOBJ_ENTERO:
            printf("    %d\n", obj.value.i);
            break;
        case PDCRT_TOBJ_MARCA_DE_PILA:
            printf("    Marca de pila\n");
            break;
        default:
            assert(0);
        }
    }
}


static void no_falla(pdcrt_error err)
{
    if(err != PDCRT_OK)
    {
        fprintf(stderr, "Error (que no debia fallar): %s\n", pdcrt_perror(err));
        abort();
    }
}

void pdcrt_op_iconst(pdcrt_contexto* ctx, int c)
{
    no_falla(pdcrt_empujar_en_pila(&ctx->pila, pdcrt_objeto_entero(c)));
}

void pdcrt_op_sum(pdcrt_contexto* ctx)
{
    pdcrt_objeto a, b;
    a = pdcrt_sacar_de_pila(&ctx->pila);
    b = pdcrt_sacar_de_pila(&ctx->pila);
    pdcrt_objeto_debe_tener_tipo(a, PDCRT_TOBJ_ENTERO);
    pdcrt_objeto_debe_tener_tipo(b, PDCRT_TOBJ_ENTERO);
    no_falla(pdcrt_empujar_en_pila(&ctx->pila, pdcrt_objeto_entero(a.value.i + b.value.i)));
}

void pdcrt_op_sub(pdcrt_contexto* ctx)
{
    pdcrt_objeto a, b;
    a = pdcrt_sacar_de_pila(&ctx->pila);
    b = pdcrt_sacar_de_pila(&ctx->pila);
    pdcrt_objeto_debe_tener_tipo(a, PDCRT_TOBJ_ENTERO);
    pdcrt_objeto_debe_tener_tipo(b, PDCRT_TOBJ_ENTERO);
    no_falla(pdcrt_empujar_en_pila(&ctx->pila, pdcrt_objeto_entero(a.value.i - b.value.i)));
}

void pdcrt_op_mul(pdcrt_contexto* ctx)
{
    pdcrt_objeto a, b;
    a = pdcrt_sacar_de_pila(&ctx->pila);
    b = pdcrt_sacar_de_pila(&ctx->pila);
    pdcrt_objeto_debe_tener_tipo(a, PDCRT_TOBJ_ENTERO);
    pdcrt_objeto_debe_tener_tipo(b, PDCRT_TOBJ_ENTERO);
    no_falla(pdcrt_empujar_en_pila(&ctx->pila, pdcrt_objeto_entero(a.value.i * b.value.i)));
}

void pdcrt_op_div(pdcrt_contexto* ctx)
{
    pdcrt_objeto a, b;
    a = pdcrt_sacar_de_pila(&ctx->pila);
    b = pdcrt_sacar_de_pila(&ctx->pila);
    pdcrt_objeto_debe_tener_tipo(a, PDCRT_TOBJ_ENTERO);
    pdcrt_objeto_debe_tener_tipo(b, PDCRT_TOBJ_ENTERO);
    no_falla(pdcrt_empujar_en_pila(&ctx->pila, pdcrt_objeto_entero(a.value.i / b.value.i)));
}

pdcrt_objeto pdcrt_op_lset(pdcrt_contexto* ctx)
{
    return pdcrt_sacar_de_pila(&ctx->pila);
}

void pdcrt_op_lget(pdcrt_contexto* ctx, pdcrt_objeto v)
{
    no_falla(pdcrt_empujar_en_pila(&ctx->pila, v));
}

void pdcrt_op_call(pdcrt_contexto* ctx, pdcrt_proc_t proc, int acepta, int devuelve)
{
    (void) acepta;
    (void) devuelve;
    pdcrt_depurar_contexto(ctx, "precall");
    (*proc)(ctx, acepta, devuelve);
    pdcrt_depurar_contexto(ctx, "postcall");
}

void pdcrt_op_retn(pdcrt_contexto* ctx, int n)
{
    pdcrt_depurar_contexto(ctx, "P1 retn");
    for(int i = ctx->pila.num_elementos; i >= (int)(ctx->pila.num_elementos - n); i--)
    {
        pdcrt_objeto obj = ctx->pila.elementos[i];
        if(obj.tag == PDCRT_TOBJ_MARCA_DE_PILA)
        {
            fprintf(stderr, "Trato de devolver a traves de una marca de pila\n");
            abort();
        }
    }
    pdcrt_objeto marca = pdcrt_eliminar_elemento_en_pila(&ctx->pila, n);
    pdcrt_objeto_debe_tener_tipo(marca, PDCRT_TOBJ_MARCA_DE_PILA);
    pdcrt_depurar_contexto(ctx, "P2 retn");
}

void pdcrt_assert_params(pdcrt_contexto* ctx, int nparams)
{
    pdcrt_depurar_contexto(ctx, "P1 assert_params");
    pdcrt_objeto marca = pdcrt_sacar_de_pila(&ctx->pila);
    if(marca.tag != PDCRT_TOBJ_MARCA_DE_PILA)
    {
        fprintf(stderr, "Se esperaba una marca de pila pero se obtuvo un %s\n", pdcrt_tipo_como_texto(marca.tag));
        abort();
    }
    if(ctx->pila.num_elementos < (size_t)nparams)
    {
        fprintf(stderr, "Se esperaban al menos %d elementos.\n", nparams);
        abort();
    }
    for(size_t i = ctx->pila.num_elementos - nparams; i < ctx->pila.num_elementos; i++)
    {
        pdcrt_objeto obj = ctx->pila.elementos[i];
        if(obj.tag == PDCRT_TOBJ_MARCA_DE_PILA)
        {
            fprintf(stderr, "Faltaron elementos en el marco de llamada\n");
            abort();
        }
    }
    pdcrt_insertar_elemento_en_pila(&ctx->pila, nparams, marca);
    pdcrt_depurar_contexto(ctx, "P2 assert_params");
}

int pdcrt_real_return(pdcrt_contexto* ctx)
{
    (void) ctx;
    return 0;
}

int pdcrt_passthru_return(pdcrt_contexto* ctx)
{
    (void) ctx;
    puts("[Advertencia] Retorno \"passthru\".");
    return 0;
}
