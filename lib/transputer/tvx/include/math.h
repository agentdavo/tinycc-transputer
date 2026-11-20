/* <math.h> for the vendored TVX headers.  Only tvx_vec.h's SiLU and RMSNorm
 * kernels call these, and the runtime does not use those yet. */
#ifndef TP_TVX_MATH_H
#define TP_TVX_MATH_H
double exp(double);
double sqrt(double);
#endif
