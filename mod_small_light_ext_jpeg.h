/* 
**  mod_small_light_ext_jpeg.h -- jpeg extension
*/

/*
** prototypes.
*/
int load_exif_from_memory(
    unsigned char **exif_data,
    unsigned int *exif_size,
    request_rec *r,
    const unsigned char *data,
    unsigned int data_len);
void exif_brigade_insert_tail(
    unsigned char *exif_data, unsigned int exif_size,
    unsigned char *image_data, unsigned long image_size,
    request_rec *r, apr_bucket_brigade *bb);

