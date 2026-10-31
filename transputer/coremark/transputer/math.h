/* Minimal math.h for bare-metal CoreMark (only modf needed) */
#ifndef _MATH_H
#define _MATH_H

double modf(double value, double *iptr);

/* Use a large double value — TCC may not support __builtin_huge_val */
static const union { unsigned int i[2]; double d; } __huge_val_u = {{0x00000000, 0x7FF00000}};
#define HUGE_VAL (__huge_val_u.d)

#endif
