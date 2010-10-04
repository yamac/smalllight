/* 
**  mod_small_light_ext_jpeg.c -- extension for jpeg
*/

#include "mod_small_light.h"
#include "mod_small_light_ext_jpeg.h"

/*
** defines.
*/
#define MAX_MARKERS 16

#define M_SOF       0xc0
#define M_SOF_MIN   (M_SOF + 0)
#define M_SOF_MAX   (M_SOF + 15)
#define M_SOI       0xd8
#define M_EOI       0xd9
#define M_SOS       0xdA
#define M_APP       0xe0
#define M_APP0      (M_APP + 0)
#define M_APP1      (M_APP + 1)
#define M_COM       0xfe // comment

static char jpeg_header[] = { 0xff, M_SOI };

/*
** functions.
*/
int load_exif_from_memory(
    unsigned char **exif_data,
    unsigned int *exif_size,
    request_rec *r,
    const unsigned char *data,
    unsigned int data_len)
{
    // scan SOI marker.
    if (data_len <= 2) return 0;
    data_len -= 2;
    unsigned char c1 = *data++;
    unsigned char c2 = *data++;
    if (c1 != 0xff || c2 != M_SOI) {
        return 0;
    }

    int num_marker = 0;
    unsigned char *marker_data[MAX_MARKERS];
    unsigned int marker_size[MAX_MARKERS];    

    // scan marker.
    for (;;) {
        unsigned char c;
        for (;;) {
            c = *data++;
            if (data_len == 0) return 0;
            data_len--;
            if (c == 0xff) break;
        }
        for (;;) {
            c = *data++;
            if (data_len == 0) return 0;
            data_len--;
            if (c != 0xff) break;
        }

        // check marker.
        if (c == M_EOI || c == M_SOS || c == 0) {
            break;
        } else if (c == M_APP1 || c == M_COM) {
            // get length of app1.
            unsigned int length = (*data++ << 8) + *(data++ + 1);

            // validate length.
            if (length < 2) return 0;

            // get app1 pointer and length.
            if (num_marker < MAX_MARKERS) {
                marker_data[num_marker] = (unsigned char *)(data - 4);
                marker_size[num_marker] = length + 2;
                num_marker++;
            }
            
            // skip pointer.
            if (data_len <= length) return 0;
            data_len -= length;
            data += length - 2;
        } else {
            // get length of app1.
            unsigned int length = (*data++ << 8) + *(data++ + 1);

            // validate length.
            if (length < 2) return 0;

            // skip pointer.
            if (data_len <= length) return 0;
            data_len -= length;
            data += length - 2;
        }
    }

    // copy app1.
    int i;
    unsigned int exif_size_total = 0;
    for (i = 0; i < num_marker; i++) {
        exif_size_total += marker_size[i];
    }
    *exif_size = exif_size_total;
    *exif_data = apr_palloc(r->pool, exif_size_total);
    unsigned char *exif_data_ptr = *exif_data;
    for (i = 0; i < num_marker; i++) {
        memcpy(exif_data_ptr, marker_data[i], marker_size[i]);
        exif_data_ptr += marker_size[i];
    }

    return 1;
}

void exif_brigade_insert_tail(
    unsigned char *exif_data, unsigned int exif_size,
    unsigned char *image_data, unsigned long image_size,
    request_rec *r, apr_bucket_brigade *bb)
{
    apr_bucket *b;
    b = apr_bucket_pool_create(jpeg_header, sizeof(jpeg_header), r->pool, bb->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, b);
    b = apr_bucket_pool_create(exif_data, exif_size, r->pool, bb->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, b);
}

