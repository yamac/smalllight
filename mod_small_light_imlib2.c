/* 
**  mod_small_light_imlib2.c -- imlib2 support
*/

#include "mod_small_light.h"
#include "mod_small_light_ext_jpeg.h"

#define X_DISPLAY_MISSING // specifies without-x for Imlib2.h
#include "Imlib2.h"
#ifdef ENABLE_JPEG
#include "jpeglib.h"
#include <setjmp.h>
#endif

small_light_filter_prototype(imlib2);

#ifndef ENABLE_IMLIB2
small_light_filter_dummy_template(imlib2);
#else

/*
** defines.
*/

#ifdef ENABLE_JPEG

// this IMAGE_DIMENSION_OK was ported from Imlib2 loader.
# define IMAGE_DIMENSIONS_OK(w, h) \
   ( ((w) > 0) && ((h) > 0) && \
     ((unsigned long long)(w) * (unsigned long long)(h) <= (1ULL << 29) - 1) )

// Some ImLib2_JPEG structures or JPEGHandler functions were ported from Imlib2 loader.
struct ImLib_JPEG_error_mgr {
   struct jpeg_error_mgr pub;
   sigjmp_buf          setjmp_buffer;
};
typedef struct ImLib_JPEG_error_mgr *emptr;

static void
_JPEGFatalErrorHandler(j_common_ptr cinfo)
{
   emptr               errmgr;

   errmgr = (emptr) cinfo->err;
/*   cinfo->err->output_message(cinfo);*/
   siglongjmp(errmgr->setjmp_buffer, 1);
   return;
}

static void
_JPEGErrorHandler(j_common_ptr cinfo)
{
   emptr               errmgr;

   errmgr = (emptr) cinfo->err;
/*   cinfo->err->output_message(cinfo);*/
/*   siglongjmp(errmgr->setjmp_buffer, 1);*/
   return;
}

static void
_JPEGErrorHandler2(j_common_ptr cinfo, int msg_level)
{
   emptr               errmgr;

   errmgr = (emptr) cinfo->err;
/*   cinfo->err->output_message(cinfo);*/
/*   siglongjmp(errmgr->setjmp_buffer, 1);*/
   return;
   msg_level = 0;
}

// The load_jpeg function is based on Imlib2 loader.
static int
load_jpeg(
    void **dest_data, int *width, int *height, const request_rec *r,
    const char *filename, int hint_w, int hint_h)
{
   int                 w, h;
   struct jpeg_decompress_struct cinfo;
   struct ImLib_JPEG_error_mgr jerr;
   FILE               *f;
   apr_status_t rv;
   apr_file_t *file;
   apr_os_file_t os_file;

   *dest_data = NULL;
   *width = *height = 0;

   rv = apr_file_open(
       &file, filename, APR_READ | APR_BINARY | APR_BUFFERED,
       APR_UREAD, r->pool);
   if (rv != APR_SUCCESS) {
       return 0;
   }
   rv = apr_os_file_get(&os_file, file);
   if (rv != APR_SUCCESS) {
       return 0;
   }
   f = fdopen(os_file, "rb");

   cinfo.err = jpeg_std_error(&(jerr.pub));
   jerr.pub.error_exit = _JPEGFatalErrorHandler;
   jerr.pub.emit_message = _JPEGErrorHandler2;
   jerr.pub.output_message = _JPEGErrorHandler;
   if (sigsetjmp(jerr.setjmp_buffer, 1))
     {
        jpeg_destroy_decompress(&cinfo);
        apr_file_close(file);
        return 0;
     }
   jpeg_create_decompress(&cinfo);
   jpeg_stdio_src(&cinfo, f);
   jpeg_read_header(&cinfo, TRUE);
   cinfo.do_fancy_upsampling = FALSE;
   cinfo.do_block_smoothing = FALSE;

   jpeg_start_decompress(&cinfo);
   w = cinfo.output_width;
   h = cinfo.output_height;
   int denom;
   denom = w / hint_w;
   if (denom > h / hint_h) {
       denom = h / hint_h;
   }
   denom = denom >= 1 ? denom : 1;
   denom = denom <= 8 ? denom : 8;
   jpeg_destroy_decompress(&cinfo);
   fseek(f, 0, SEEK_SET);
   jpeg_create_decompress(&cinfo);
   jpeg_stdio_src(&cinfo, f);
   jpeg_read_header(&cinfo, TRUE);
   cinfo.do_fancy_upsampling = FALSE;
   cinfo.do_block_smoothing = FALSE;
   cinfo.scale_denom = denom;

   jpeg_start_decompress(&cinfo);
   DATA8              *ptr, *line[16], *data;
   DATA32             *ptr2, *dest;
   int                 x, y, l, i, scans, count, prevy;

   w = cinfo.output_width;
   h = cinfo.output_height;

   if ((cinfo.rec_outbuf_height > 16) || (cinfo.output_components <= 0) ||
       !IMAGE_DIMENSIONS_OK(w, h))
     {
        jpeg_destroy_decompress(&cinfo);
        apr_file_close(file);
        return 0;
     }
   data = apr_palloc(r->pool, w * 16 * cinfo.output_components);
   if (!data)
     {
        jpeg_destroy_decompress(&cinfo);
        apr_file_close(file);
        return 0;
     }
   /* must set the im->data member before callign progress function */
   ptr2 = dest = apr_palloc(r->pool, w * h * sizeof(DATA32));
   if (!dest)
     {
        jpeg_destroy_decompress(&cinfo);
        apr_file_close(file);
        return 0;
     }
   count = 0;
   prevy = 0;
   if (cinfo.output_components > 1)
     {
        for (i = 0; i < cinfo.rec_outbuf_height; i++)
           line[i] = data + (i * w * cinfo.output_components);
        for (l = 0; l < h; l += cinfo.rec_outbuf_height)
          {
             jpeg_read_scanlines(&cinfo, line, cinfo.rec_outbuf_height);
             scans = cinfo.rec_outbuf_height;
             if ((h - l) < scans)
                scans = h - l;
             ptr = data;
             for (y = 0; y < scans; y++)
               {
                  for (x = 0; x < w; x++)
                    {
                       *ptr2 =
                           (0xff000000) | ((ptr[0]) << 16) | ((ptr[1]) <<
                                                              8) |
                           (ptr[2]);
                       ptr += cinfo.output_components;
                       ptr2++;
                    }
               }
          }
     }
   else if (cinfo.output_components == 1)
     {
        for (i = 0; i < cinfo.rec_outbuf_height; i++)
           line[i] = data + (i * w);
        for (l = 0; l < h; l += cinfo.rec_outbuf_height)
          {
             jpeg_read_scanlines(&cinfo, line, cinfo.rec_outbuf_height);
             scans = cinfo.rec_outbuf_height;
             if ((h - l) < scans)
                scans = h - l;
             ptr = data;
             for (y = 0; y < scans; y++)
               {
                  for (x = 0; x < w; x++)
                    {
                       *ptr2 =
                           (0xff000000) | ((ptr[0]) << 16) | ((ptr[0]) <<
                                                              8) |
                           (ptr[0]);
                       ptr++;
                       ptr2++;
                    }
               }
          }
     }
   jpeg_finish_decompress(&cinfo);
   jpeg_destroy_decompress(&cinfo);
   apr_file_close(file);

   *dest_data = dest;
   *width = w;
   *height = h;

   return 1;
}

#endif

static const char temp_file_template[] = "small_light.XXXXXX";

typedef struct {
    const char *temp_dir;
    char *filename;
    apr_file_t *file;
    apr_size_t image_len;
} small_light_module_imlib2_ctx_t;

/*
** functions.
*/
apr_status_t small_light_filter_imlib2_init(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_imlib2_init");

    request_rec *r = f->r;
    small_light_module_ctx_t *ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_imlib2_ctx_t *lctx = ctx->lctx;

    // create local context.
    if (ctx->lctx == NULL) {
        ctx->lctx = lctx =
            (small_light_module_imlib2_ctx_t *)apr_pcalloc(
                r->pool, sizeof(small_light_module_imlib2_ctx_t));
    }

    return APR_SUCCESS;
}

apr_status_t small_light_filter_imlib2_receive_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e,
    const char *data,
    apr_size_t len)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_imlib2_receive_data %d", len);

    request_rec *r = f->r;
    small_light_module_ctx_t* ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_imlib2_ctx_t *lctx = ctx->lctx;
    apr_status_t rv;

    // create temporary file for an image at first time.
    if (lctx->file == NULL) {
        rv = apr_temp_dir_get(&lctx->temp_dir, r->pool);
        if (rv == APR_SUCCESS) {
            rv = apr_filepath_merge(&lctx->filename, lctx->temp_dir, temp_file_template, APR_FILEPATH_TRUENAME, r->pool);
        }
        if (rv == APR_SUCCESS) {
            rv = apr_file_mktemp(&lctx->file, lctx->filename, 0, r->pool);
        }
        if (rv != APR_SUCCESS) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "could not create temporary file. %s", lctx->filename);
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return APR_EGENERAL;
        }
    }

    // append image data to the temporary file.
    if (len > 0) {
        apr_size_t num_writes = len;
        apr_file_write(lctx->file, data, &num_writes);
        if (num_writes < 0) {
            ap_log_rerror(
                APLOG_MARK, APLOG_ERR, 0, r,
                "an error occured while writing data to temporary file %d", num_writes);
            apr_file_close(lctx->file);
            lctx->file = NULL;
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return APR_EGENERAL;
        }
    }

    // increase length.
    lctx->image_len += len;

    return APR_SUCCESS;
}

apr_status_t small_light_filter_imlib2_output_data(
    ap_filter_t *f,
    apr_bucket_brigade *bb,
    void *v_ctx,
    apr_bucket *e)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, f->r, "small_light_filter_imlib2_output_data");

    request_rec *r = f->r;
    small_light_module_ctx_t *ctx = (small_light_module_ctx_t*)v_ctx;
    small_light_module_imlib2_ctx_t *lctx = ctx->lctx;
    apr_status_t rv;
    struct timeval t2, t21, t22, t23, t3;

    // check data received.
    if (lctx->file == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "no data received");
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }

    // start image modifing.
    gettimeofday(&t2, NULL);
    small_light_image_size_t sz;
    small_light_calc_image_size(&sz, r, ctx, 10000.0, 10000.0);

    // load image.
    gettimeofday(&t21, NULL);
    Imlib_Image image_org;
#ifdef ENABLE_JPEG
    if (sz.jpeghint_flg != 0) {
        void *data;
        int w, h;
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "load_jpeg");
        if (!load_jpeg((void**)&data, &w, &h, r, lctx->filename, sz.dw, sz.dh)) {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "load_jpeg_mem failed. uri=%s", r->uri);
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "imlib_load_image_immediately_without_cache(%s)", lctx->filename);
            image_org = imlib_load_image_immediately_without_cache(lctx->filename);
            if (image_org == NULL) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "imlib_load_image failed. uri=%s", r->uri);
                r->status = HTTP_INTERNAL_SERVER_ERROR;
                return APR_EGENERAL;
            }
        } else {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "imlib_create_image_using_data(%d, %d, data)", w, h);
            image_org = imlib_create_image_using_data(w, h, data);
        }
    } else {
#endif
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "imlib_load_image_immediately_without_cache(%s)", lctx->filename);
        image_org = imlib_load_image_immediately_without_cache(lctx->filename);
        if (image_org == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "imlib_load_image failed. uri=%s", r->uri);
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return APR_EGENERAL;
        }
#ifdef ENABLE_JPEG
    }
#endif

    // calc size.
    gettimeofday(&t22, NULL);
    imlib_context_set_image(image_org);
    double iw = (double)imlib_image_get_width();
    double ih = (double)imlib_image_get_height();
    small_light_calc_image_size(&sz, r, ctx, iw, ih);

    // load exif.
    unsigned char *exif_data = NULL;
    unsigned int exif_size = 0;
    if (sz.inhexif_flg != 0) {
        apr_mmap_t *org_image_mmap = NULL;
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "open mmap");
        rv = apr_mmap_create(&org_image_mmap, lctx->file, 0, lctx->image_len, APR_MMAP_READ, r->pool);
        if (rv != APR_SUCCESS) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "open mmap failed. uri=%s", r->uri);
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return rv;
        }
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "load exif");
        load_exif_from_memory(&exif_data, &exif_size, r, org_image_mmap->mm, lctx->image_len);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "close mmap");
        apr_mmap_delete(org_image_mmap);
        if (exif_size > 0) {
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Exif found. size = %d", exif_size);
        }
    }

    // pass through.
    if (sz.pt_flg != 0) {
        apr_mmap_t *org_image_mmap = NULL;
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "open mmap");
        // open mmap.
        // note that the mmap to be deleted automatically by apr, so we don't need to delete it manualy.
        rv = apr_mmap_create(&org_image_mmap, lctx->file, 0, lctx->image_len, APR_MMAP_READ, r->pool);
        if (rv != APR_SUCCESS) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "open mmap failed. uri=%s", r->uri);
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return rv;
        }
        apr_bucket *b = apr_bucket_pool_create(org_image_mmap->mm, lctx->image_len, r->pool, ctx->bb->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(ctx->bb, b);
        APR_BRIGADE_INSERT_TAIL(ctx->bb, apr_bucket_eos_create(ctx->bb->bucket_alloc));
        return ap_pass_brigade(f->next, ctx->bb);
    }

    // close temporary file. (the file to be deleted by apr on close)
    apr_file_close(lctx->file);
    lctx->file = NULL;

    // crop, scale.
    Imlib_Image image_dst;
    if (sz.scale_flg != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "imlib_create_cropped_scaled_image(%f,%f,%f,%f,%f,%f)",
            sz.sx, sz.sy, sz.sw, sz.sh, sz.dw, sz.dh);
        image_dst = imlib_create_cropped_scaled_image(
            (int)sz.sx, (int)sz.sy, (int)sz.sw, (int)sz.sh, (int)sz.dw, (int)sz.dh
        );
        imlib_context_set_image(image_org);
        imlib_free_image();
    } else {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "no scale");
        image_dst = image_org;
    }
    if (image_dst == NULL) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "imlib_create_cropped_scaled_image failed. uri=%s", r->uri);
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }

    // create canvas then draw image to the canvas.
    if (sz.cw > 0.0 && sz.ch > 0.0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "imlib_create_image(%f, %f)", sz.cw, sz.ch);
        Imlib_Image image_tmp = imlib_create_image(sz.cw, sz.ch);
        if (image_tmp == NULL) {
            imlib_context_set_image(image_dst);
            imlib_free_image();
            ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
                "imlib_create_image failed. uri=%s", r->uri);
            r->status = HTTP_INTERNAL_SERVER_ERROR;
            return APR_EGENERAL;
        }
        imlib_context_set_image(image_tmp);
        imlib_context_set_color(sz.cc.r, sz.cc.g, sz.cc.b, sz.cc.a);
        imlib_image_fill_rectangle(0, 0, sz.cw, sz.ch);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "imlib_blend_image_onto_image(image_dst,255,0,0,%f,%f,%f,%f,%f,%f)",
            sz.dw, sz.dh, sz.dx, sz.dy, sz.dw, sz.dh);
        imlib_blend_image_onto_image(image_dst, 255, 0, 0,
            (int)sz.dw, (int)sz.dh, (int)sz.dx, (int)sz.dy, (int)sz.dw, (int)sz.dh);
        imlib_context_set_image(image_dst);
        imlib_free_image();
        image_dst = image_tmp;
    }

    // effects.
    char *sharpen = (char *)apr_table_get(ctx->prm, "sharpen");
    if (sharpen) {
        int radius = small_light_parse_int(r, sharpen);
        if (radius > 0) {
            imlib_context_set_image(image_dst);
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                "imlib_image_sharpen(%df)", radius);
            imlib_image_sharpen(radius);
        }
    }

    char *blur = (char *)apr_table_get(ctx->prm, "blur");
    if (blur) {
        int radius = small_light_parse_int(r, blur);
        if (blur > 0) {
            imlib_context_set_image(image_dst);
            ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                "imlib_image_sharpen(%df)", radius);
            imlib_image_blur(radius);
        }
    }

    // border.
    if (sz.bw > 0.0 || sz.bh > 0.0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "draw border");
        imlib_context_set_color(sz.bc.r, sz.bc.g, sz.bc.b, sz.bc.a);
        imlib_context_set_image(image_dst);
        imlib_image_fill_rectangle(0, 0, sz.cw, sz.bh);
        imlib_image_fill_rectangle(0, 0, sz.bw, sz.ch);
        imlib_image_fill_rectangle(0, sz.ch - sz.bh, sz.cw, sz.bh);
        imlib_image_fill_rectangle(sz.cw - sz.bw, 0, sz.bw, sz.ch);
    }

    gettimeofday(&t23, NULL);

    // set params.
    imlib_context_set_image(image_dst);
    double q = small_light_parse_double(r, (char *)apr_table_get(ctx->prm, "q"));
    if (q > 0.0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
            "imlib_image_attach_data_value('quality', NULL, %f, NULL)", q);
        imlib_image_attach_data_value("quality", NULL, q, NULL);
    }
    char *of = (char *)apr_table_get(ctx->prm, "of");
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
        "imlib_image_set_format('%s')", of);
    imlib_image_set_format(of);

    // save image.
    char *sled_image_filename;
    apr_file_t *sled_image_fp = NULL;
    rv = apr_filepath_merge(&sled_image_filename, lctx->temp_dir, temp_file_template, APR_FILEPATH_TRUENAME, r->pool);
    if (rv == APR_SUCCESS) {
        rv = apr_file_mktemp(&sled_image_fp, sled_image_filename, 0, r->pool);
    }
    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "could not create temporary file.");
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }
    Imlib_Load_Error err;
    imlib_save_image_with_error_return(sled_image_filename, &err);
    imlib_free_image();

    // check error.
    if (err != IMLIB_LOAD_ERROR_NONE) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "imlib_save_error failed. uri=%s", r->uri);
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }

    // get small_lighted image by mmap.
    apr_finfo_t sled_image_finfo;
    apr_mmap_t *sled_image_mmap = NULL;
    rv = apr_file_info_get(&sled_image_finfo, APR_FINFO_SIZE, sled_image_fp);
    if (rv == APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "sled_image_finfo.size = %d", (int)sled_image_finfo.size);
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "open mmap");
        // open mmap.
        // note that the mmap to be deleted automatically by apr, so we don't need to delete it manualy.
        rv = apr_mmap_create(&sled_image_mmap, sled_image_fp, 0, sled_image_finfo.size, APR_MMAP_READ, r->pool);
    }
    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r, "open mmap failed. %s", r->uri);
        r->status = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }
    
    // insert new backet.
    if (exif_size > 0 && exif_data) {
        // insert new bucket to bucket brigade with exif if exists.
        exif_brigade_insert_tail(exif_data, exif_size, sled_image_mmap->mm, sled_image_finfo.size, r, ctx->bb);
        apr_bucket *b = apr_bucket_pool_create(sled_image_mmap->mm + 2, sled_image_finfo.size - 2, r->pool, ctx->bb->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(ctx->bb, b);
    } else {
        // insert new bucket to bucket brigade.
        apr_bucket *b = apr_bucket_pool_create(sled_image_mmap->mm, sled_image_finfo.size, r->pool, ctx->bb->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(ctx->bb, b);
    }
    
    // insert eos to bucket brigade.
    APR_BRIGADE_INSERT_TAIL(ctx->bb, apr_bucket_eos_create(ctx->bb->bucket_alloc));

    // set correct Content-Type and Content-Length.
    char *cont_type = apr_psprintf(r->pool, "image/%s", of);
    ap_set_content_type(r, cont_type);
    ap_set_content_length(r, sled_image_finfo.size);

    // end.
    gettimeofday(&t3, NULL);

    // http header.
    int info = small_light_parse_int(r, (char *)apr_table_get(ctx->prm, "info"));
    if (info != SMALL_LIGHT_INT_INVALID_VALUE && info != 0) {
        char *info = apr_psprintf(r->pool,
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

