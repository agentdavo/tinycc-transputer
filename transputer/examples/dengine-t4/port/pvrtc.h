#ifndef DE_PVRTC_H
#define DE_PVRTC_H
/* Decodes a PVRTC1 level (power-of-two width and height) to RGBA8 rows in
 * data order. force_opaque sets alpha to 255 for the RGB formats. Returns 0
 * for unsupported dimensions or out of memory. */
int de_pvrtc_decode(const unsigned char *data, unsigned int width, unsigned int height, int two_bpp, int force_opaque, unsigned char *rgba);
/* Bytes in one level, with the format's minimum of 2x2 blocks. */
unsigned int de_pvrtc_size(unsigned int width, unsigned int height, int two_bpp);
#endif
