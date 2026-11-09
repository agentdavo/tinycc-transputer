#ifndef DE_PNG_H
#define DE_PNG_H
typedef struct {
    unsigned int width, height;
    unsigned char *rgba;       /* malloc'd RGBA8, rows top first */
    int has_alpha;             /* colour premultiplied by alpha when set */
    unsigned int bits_per_pixel; /* what CGImageGetBitsPerPixel reported */
} de_png_image;
/* Returns 0 on success, otherwise a short description of the failure. */
const char *de_png_decode(const unsigned char *data, unsigned long size, de_png_image *img);
#endif
