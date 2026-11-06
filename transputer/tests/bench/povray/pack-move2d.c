#define POV_RGB_USE_MOVE2D 1
#include "rgbrow.h"
void pack_move2d(unsigned char *d, const unsigned char *s, unsigned n)
{
    pov_rgb_unpack(d, s, n);
}
