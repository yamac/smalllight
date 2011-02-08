#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "http_log.h"
#include "ap_config.h"
#include "ap_mpm.h"
#include "apr_file_io.h"
#include "apr_file_info.h"
#include "apr_mmap.h"
#include "apr_hash.h"
#include "apr_strings.h"
#include <stdio.h>
#include <regex.h>
#include <sys/mman.h>

#define SMALL_LIGHT_PARAM_STR_MAX 512
#define SMALL_LIGHT_PATTERN_MAX_LENGTH 64
#define SMALL_LIGHT_ENGINE_MAX_LENGTH 20
#define SMALL_LIGHT_ENGINE_DUMMY "dummy"
#define SMALL_LIGHT_ENGINE_IMLIB2 "imlib2"
#define SMALL_LIGHT_ENGINE_IMAGEMAGICK "imagemagick"
#define SMALL_LIGHT_DEFAULT_ENGINE SMALL_LIGHT_ENGINE_IMLIB2
#define SMALL_LIGHT_INT_INVALID_VALUE (INT_MAX)
#define SMALL_LIGHT_INT_MIN_VALUE (INT_MIN + 1)
#define SMALL_LIGHT_INT_MAX_VALUE (INT_MAX)
#define SMALL_LIGHT_COORD_INVALID_VALUE (1.e+38)

// function defines.

typedef apr_status_t (*SMALL_LIGHT_FILTER_INIT) (
    ap_filter_t *,
    apr_bucket_brigade *,
    void *);

typedef apr_status_t (*SMALL_LIGHT_FILTER_RECEIVE_DATA) (
    ap_filter_t *,
    apr_bucket_brigade *,
    void *,
    apr_bucket *,
    const char *,
    apr_size_t);

typedef apr_status_t (*SMALL_LIGHT_FILTER_OUTPUT_DATA) (
    ap_filter_t *,
    apr_bucket_brigade *,
    void *,
    apr_bucket *);

// typedefs.

typedef struct {
    const char *name;
    char *param_str;
} small_light_pattern_t;

typedef enum {
    SMALL_LIGHT_COORD_UNIT_NONE,
    SMALL_LIGHT_COORD_UNIT_PIXEL,
    SMALL_LIGHT_COORD_UNIT_PERCENT
} small_light_coord_unit_t;

typedef struct {
    double v;
    small_light_coord_unit_t u;
} small_light_coord_t;

typedef struct {
    short r;
    short g;
    short b;
    short a;
} small_light_color_t;

typedef struct {
    double sx;
    double sy;
    double sw;
    double sh;
    double dx;
    double dy;
    double dw;
    double dh;
    double cw;
    double ch;
    small_light_color_t cc;
    double bw;
    double bh;
    small_light_color_t bc;
    double aspect;
    int pt_flg;
    int scale_flg;
    int inhexif_flg;
    int jpeghint_flg;
} small_light_image_size_t;

typedef struct {
    apr_pool_t *p;
    apr_hash_t *h;
} small_light_server_conf_t;

typedef struct {
    apr_bucket_brigade *bb;
    SMALL_LIGHT_FILTER_INIT init_func;
    SMALL_LIGHT_FILTER_RECEIVE_DATA receive_data_func;
    SMALL_LIGHT_FILTER_OUTPUT_DATA output_data_func;
    struct timeval t;
    apr_table_t *prm;
    void *lctx;
} small_light_module_ctx_t;

// external functions.
int small_light_parse_flag(request_rec *r, const char *str);
int small_light_parse_int(request_rec *r, const char *str);
double small_light_parse_double(request_rec *r, const char *str);
int small_light_parse_coord(request_rec *r, small_light_coord_t *crd, const char *str);
double small_light_calc_coord(small_light_coord_t *crd, double v);
int small_light_parse_color(request_rec *r, small_light_color_t *color, const char *str);
void *small_light_alloc(apr_pool_t *pool, apr_size_t size);
void *small_light_realloc(apr_pool_t *pool, void *ptr, apr_size_t size, apr_size_t old_size);
void small_light_free(apr_pool_t *pool, void *ptr);
long small_light_timeval_diff(struct timeval *st, struct timeval *et);
void small_light_calc_image_size(
    small_light_image_size_t *sz,
    request_rec *r,
    small_light_module_ctx_t *ctx,
    double iw, double ih);
void small_light_init_param(apr_table_t *prm);
int small_light_parse_param(
    request_rec *r,
    apr_table_t *prm,
    const char *param_str);
int small_light_parse_uri_param(request_rec *r, char *param_str, const char *uri_str);
apr_status_t small_light_receive_data(
    unsigned char **image,
    size_t *image_len,
    const ap_filter_t *f,
    const apr_bucket_brigade *bb,
    const char *data,
    const apr_size_t data_len);

#define small_light_filter_prototype(name)\
apr_status_t small_light_filter_##name##_init(\
    ap_filter_t *f,\
    apr_bucket_brigade *bb,\
    void *v_ctx);\
apr_status_t small_light_filter_##name##_receive_data(\
    ap_filter_t *f,\
    apr_bucket_brigade *bb,\
    void *v_ctx,\
    apr_bucket *e,\
    const char *data,\
    apr_size_t len);\
apr_status_t small_light_filter_##name##_output_data(\
    ap_filter_t *f,\
    apr_bucket_brigade *bb,\
    void *v_ctx,\
    apr_bucket *e);

#define small_light_filter_dummy_template(name)\
small_light_filter_prototype(dummy)\
apr_status_t small_light_filter_##name##_init(\
    ap_filter_t *f,\
    apr_bucket_brigade *bb,\
    void *v_ctx)\
{\
    return small_light_filter_dummy_init(f, bb, v_ctx);\
}\
\
apr_status_t small_light_filter_##name##_receive_data(\
    ap_filter_t *f,\
    apr_bucket_brigade *bb,\
    void *v_ctx,\
    apr_bucket *e,\
    const char *data,\
    apr_size_t len)\
{\
    return small_light_filter_dummy_receive_data(f, bb, v_ctx, e, data, len);\
}\
\
apr_status_t small_light_filter_##name##_output_data(\
    ap_filter_t *f,\
    apr_bucket_brigade *bb,\
    void *v_ctx,\
    apr_bucket *e)\
{\
    return small_light_filter_dummy_output_data(f, bb, v_ctx, e);\
}

