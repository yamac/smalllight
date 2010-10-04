/* 
**  mod_small_light_imagemagick.c -- imagemagick support
*/

#include "mod_small_light.h"
#include "mod_small_light_ext_jpeg.h"

small_light_filter_prototype(imagemagick);

#ifndef ENABLE_WAND
small_light_filter_dummy_template(imagemagick);
#else

/*
** defines.
*/
#include "wand/MagickWand.h"

typedef struct {
    unsigned char *image;
    apr_size_t image_len;
} small_light_module_imagemagick_ctx_t;

/*
** prototypes.
*/
void small_light_filter_imagemagick_output_data_init(void);
void small_light_filter_imagemagick_output_data_fini(MagickWand *wand);

/*
** functions.
*/
void small_light_filter_imagemagick_output_data_init(void)
{
    MagickWandGenesis();
}

void small_light_filter_imagemagick_output_data_fini(MagickWand *wand)
{
    if (wand != NULL)
    {
        DestroyMagickWand(wand);
        MagickWandTerminus();
    }
}

apr_status_t small_light_filter_imagemagick_init(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_imagemagick_init");

    request_rec *r = f->r;
    small_light_module_ctx_t *ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_imagemagick_ctx_t *lctx = ctx->lctx;

    // create local context.
    if (ctx->lctx == NULL) {
        ctx->lctx = lctx =
            (small_light_module_imagemagick_ctx_t *)apr_pcalloc(
                r->pool, sizeof(small_light_module_imagemagick_ctx_t));
    }

    return APR_SUCCESS;
}

apr_status_t small_light_filter_imagemagick_receive_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e,
    const char *data,
    apr_size_t len)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_imagemagick_receive_data %d", len);
    small_light_module_ctx_t* ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_imagemagick_ctx_t *lctx = ctx->lctx;
    return small_light_receive_data(&lctx->image, &lctx->image_len, f, bb, data, len);
}

apr_status_t small_light_filter_imagemagick_output_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_imagemagick_output_data");

    request_rec *r = f->r;
    small_light_module_ctx_t* ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_imagemagick_ctx_t *lctx = ctx->lctx;
    struct timeval t2, t21, t22, t23, t3;
    MagickWand *wand = NULL;
    MagickBooleanType status = MagickFalse;

    // check data received.
    if (lctx->image == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "no data received.");
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }

    // start image modifing.
    gettimeofday(&t2, NULL);

    // init wand
    small_light_filter_imagemagick_output_data_init();
    wand = NewMagickWand();

    // load image.
    gettimeofday(&t21, NULL);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "MagickReadImageBlob");
    status = MagickReadImageBlob(wand, (void *)lctx->image, lctx->image_len);
    if (status == MagickFalse) {
        small_light_filter_imagemagick_output_data_fini(wand);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "couldn't read image");
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }

    // calc size.
    gettimeofday(&t22, NULL);
    double iw = (double)MagickGetImageWidth(wand);
    double ih = (double)MagickGetImageHeight(wand);
    small_light_image_size_t sz;
    small_light_calc_image_size(&sz, r, ctx, iw, ih);

    // pass through.
    if (sz.pt_flg != 0) {
        small_light_filter_imagemagick_output_data_fini(wand);
        apr_bucket *b = apr_bucket_pool_create(lctx->image, lctx->image_len, r->pool, ctx->bb->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(ctx->bb, b);
        APR_BRIGADE_INSERT_TAIL(ctx->bb, apr_bucket_eos_create(ctx->bb->bucket_alloc));
        return ap_pass_brigade(f->next, ctx->bb);
    }

    // crop, scale.
    status = MagickTrue;
    if (sz.scale_flg != 0) {
        char *crop_geo = (char *)apr_psprintf(r->pool, "%f!x%f!+%f+%f",
            sz.sw, sz.sh, sz.sx, sz.sy);
        char *size_geo = (char *)apr_psprintf(r->pool, "%f!x%f!", sz.dw, sz.dh);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "MagickTransformImage(wand, ""%s"", ""%s"")",
            crop_geo, size_geo);
        wand = MagickTransformImage(wand, crop_geo, size_geo);
    } else {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "no scale");
    }
    if (status == MagickFalse) {
        small_light_filter_imagemagick_output_data_fini(wand);
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "MagickTransformImage failed");
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }

    // create canvas then draw image to the canvas.
    if (sz.cw > 0.0 && sz.ch > 0.0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "NewMagickWand()");
        MagickWand *canvas_wand = NewMagickWand();
        PixelWand *canvas_color = NewPixelWand();
        PixelSetRed(canvas_color, sz.cc.r / 255.0);
        PixelSetGreen(canvas_color, sz.cc.g / 255.0);
        PixelSetBlue(canvas_color, sz.cc.b / 255.0);
        PixelSetAlpha(canvas_color, sz.cc.a / 255.0);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "MagickNewImage(wand, %f, %f, bgcolor)", sz.cw, sz.ch);
        status = MagickNewImage(canvas_wand, sz.cw, sz.ch, canvas_color);
        DestroyPixelWand(canvas_color);
        if (status == MagickFalse) {
            small_light_filter_imagemagick_output_data_fini(wand);
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "MagickNewImage(wand, %f, %f, bgcolor) failed", sz.cw, sz.ch);
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return APR_EGENERAL;
        }
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "MagickCompositeImage(canvas_wand, wand, AtopCompositeOp, %f, %f)",
            sz.dx, sz.dy);
        MagickCompositeImage(canvas_wand, wand, AtopCompositeOp, sz.dx, sz.dy);
        DestroyMagickWand(wand);
        wand = canvas_wand;
    }

    // effects.
    char *unsharp = (char *)apr_table_get(ctx->prm, "unsharp");
    if (unsharp) {
        GeometryInfo geo;
        ParseGeometry(unsharp, &geo);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "MagickUnsharpMaskImage(wand, %f, %f, %f, %f)",
            geo.rho, geo.sigma, geo.xi, geo.psi);
        status = MagickUnsharpMaskImage(wand, geo.rho, geo.sigma, geo.xi, geo.psi);
        if (status == MagickFalse) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "unsharp failed");
        }
    }

    char *sharpen = (char *)apr_table_get(ctx->prm, "sharpen");
    if (sharpen) {
        GeometryInfo geo;
        ParseGeometry(sharpen, &geo);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "MagickSharpenImage(wand, %f, %f)",
            geo.rho, geo.sigma);
        status = MagickSharpenImage(wand, geo.rho, geo.sigma);
        if (status == MagickFalse) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "sharpen failed");
        }
    }

    char *blur = (char *)apr_table_get(ctx->prm, "blur");
    if (blur) {
        GeometryInfo geo;
        ParseGeometry(blur, &geo);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "MagickBlurImage(wand, %f, %f)",
            geo.rho, geo.sigma);
        status = MagickBlurImage(wand, geo.rho, geo.sigma);
        if (status == MagickFalse) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "blur failed");
        }
    }

    // border.
    if (sz.bw > 0.0 || sz.bh > 0.0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "draw border");
        DrawingWand *border_wand = NewDrawingWand();
        PixelWand *border_color;
        border_color = NewPixelWand();
        PixelSetRed(border_color, sz.bc.r / 255.0);
        PixelSetGreen(border_color, sz.bc.g / 255.0);
        PixelSetBlue(border_color, sz.bc.b / 255.0);
        PixelSetAlpha(border_color, sz.bc.a / 255.0);
        DrawSetFillColor(border_wand, border_color);
        DrawSetStrokeColor(border_wand, border_color);
        DrawSetStrokeWidth(border_wand, 1);
        DrawRectangle(border_wand, 0, 0, sz.cw - 1, sz.bh - 1);
        DrawRectangle(border_wand, 0, 0, sz.bw - 1, sz.ch - 1);
        DrawRectangle(border_wand, 0, sz.ch - sz.bh, sz.cw - 1, sz.ch - 1);
        DrawRectangle(border_wand, sz.cw - sz.bw, 0, sz.cw - 1, sz.ch - 1);
        MagickDrawImage(wand, border_wand);
        DestroyPixelWand(border_color);
        DestroyDrawingWand(border_wand);
    }

    gettimeofday(&t23, NULL);

    // set params.
    double q = small_light_parse_double(r, (char *)apr_table_get(ctx->prm, "q"));
    if (q > 0.0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "MagickSetImageComressionQualty(wand, %f)", q);
        MagickSetImageCompressionQuality(wand, q);
    }
    char *of = (char *)apr_table_get(ctx->prm, "of");
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
        "MagickSetFormat(wand, '%s')", of);
    MagickSetFormat(wand, of);

    // get small_lighted image as binary.
    unsigned char *canvas_buff;
    const char *sled_image;
    size_t sled_image_size;
    canvas_buff = MagickGetImageBlob(wand, &sled_image_size);
    sled_image = (const char *)apr_pmemdup(r->pool, canvas_buff, sled_image_size);
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sled_image_size = %d", sled_image_size);

    // free buffer and wand.
    MagickRelinquishMemory(canvas_buff);
    small_light_filter_imagemagick_output_data_fini(wand);

    // insert new bucket to bucket brigade.
    apr_bucket *b = apr_bucket_pool_create(sled_image, sled_image_size, r->pool, ctx->bb->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(ctx->bb, b);

    // insert eos to bucket brigade.
    APR_BRIGADE_INSERT_TAIL(ctx->bb, apr_bucket_eos_create(ctx->bb->bucket_alloc));

    // set correct Content-Type and Content-Length.
    char *cont_type = apr_psprintf(r->pool, "image/%s", of);
    ap_set_content_type(r, cont_type);
    ap_set_content_length(r, sled_image_size);

    // end.
    gettimeofday(&t3, NULL);

    // http header.
    int info = small_light_parse_int(r, (char *)apr_table_get(ctx->prm, "info"));
    if (info != SMALL_LIGHT_INT_INVALID_VALUE && info != 0) {
        char *info = (char *)apr_psprintf(r->pool,
            "transfer=%ldms, modify image=%ldms (load=%ldms, scale=%ldms, save=%ldms)",
            small_light_timeval_diff(&ctx->t, &t2) / 1000L,
            small_light_timeval_diff(&t2, &t3) / 1000L,
            small_light_timeval_diff(&t21, &t22) / 1000L,
            small_light_timeval_diff(&t22, &t23) / 1000L,
            small_light_timeval_diff(&t23, &t3) / 1000L
        );
        ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r,
            "uri=%s, info=%s)", r->unparsed_uri, info);
        apr_table_setn(r->headers_out, "X-SmallLight-Description", info);
    }

    return ap_pass_brigade(f->next, ctx->bb);
}

#endif

