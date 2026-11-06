#undef POV_RGB_USE_MOVE2D
#include "rgbrow.h"
void pack_bytes(unsigned char *d, const unsigned char *s, unsigned n)
{
    pov_rgb_unpack(d, s, n);
}
