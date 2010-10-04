/*
**  mod_small_light_dummy.c -- dummy support
*/

#include "mod_small_light.h"

/*
** defines.
*/
typedef struct {
    unsigned char *image;
    apr_size_t image_len;
} small_light_module_dummy_ctx_t;

/*
** prototypes.
*/
apr_status_t small_light_filter_dummy_init(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx
);
apr_status_t small_light_filter_dummy_receive_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e,
    const char *data,
    apr_size_t len
);
apr_status_t small_light_filter_dummy_output_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e
);

/*
** functions.
*/

apr_status_t small_light_filter_dummy_init(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_dummy_init");

    request_rec *r = f->r;
    small_light_module_ctx_t* ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_dummy_ctx_t *lctx = ctx->lctx;

    // create local context.
    if (ctx->lctx == NULL) {
        ctx->lctx = lctx =
            (small_light_module_dummy_ctx_t *)apr_pcalloc(
                r->pool, sizeof(small_light_module_dummy_ctx_t));
    }

    return APR_SUCCESS;
}

apr_status_t small_light_filter_dummy_receive_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e,
    const char *data,
    apr_size_t len)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_dummy_receive_data %d", len);
    small_light_module_ctx_t* ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_dummy_ctx_t *lctx = ctx->lctx;
    return small_light_receive_data(&lctx->image, &lctx->image_len, f, bb, data, len);
}

// output data
apr_status_t small_light_filter_dummy_output_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_dummy_output_data");

    request_rec *r = f->r;
    small_light_module_ctx_t* ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_dummy_ctx_t *lctx = ctx->lctx;
    apr_bucket *b = apr_bucket_pool_create(lctx->image, lctx->image_len, r->pool, ctx->bb->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(ctx->bb, b);
    APR_BRIGADE_INSERT_TAIL(ctx->bb, apr_bucket_eos_create(ctx->bb->bucket_alloc));
    return ap_pass_brigade(f->next, ctx->bb);
}

