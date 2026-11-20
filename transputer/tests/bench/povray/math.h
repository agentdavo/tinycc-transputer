/* Minimal declarations for this old source and the fdlibm files used by the
 * fixture.  Definitions are the actual uClibc fdlibm sources, not numerical
 * substitutes.  IEEE entry wrappers live in runtime.c. */
#ifndef POV_PORT_MATH_H
#define POV_PORT_MATH_H
double sqrt(double),sin(double),cos(double),tan(double),asin(double),acos(double),atan(double),atan2(double,double);
double exp(double),log(double),log10(double),pow(double,double),fmod(double,double),floor(double),ceil(double),fabs(double);
double frexp(double,int*),ldexp(double,int),scalbn(double,int),copysign(double,double),modf(double,double*);
#define HUGE_VAL (1.0/0.0)
#define M_PI 3.14159265358979323846264338327950288
#define attribute_hidden
#define libm_hidden_def(x)
#define libm_hidden_proto(x)
#define strong_alias(x,y)
#define weak_alias(x,y)
#define strong_alias_untyped(x,y)
#define isfinite(x) pov_isfinite(x)
int pov_isfinite(double);
#define __MATH_DECLARE_LDOUBLE
#endif
