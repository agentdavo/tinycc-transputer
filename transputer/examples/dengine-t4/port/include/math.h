/* Software libm shared by the host and T800 builds, so both evaluate the
 * same IEEE double operations and agree bit for bit. Not correctly rounded;
 * errors are a few ulp over the ranges dEngine uses. */
#ifndef DE_MATH_H
#define DE_MATH_H
#define M_PI 3.14159265358979323846
#define HUGE_VAL (__builtin_inf())
#define sqrt de_sqrt
#define sqrtf de_sqrtf
#define sin de_sin
#define cos de_cos
#define tan de_tan
#define sinf de_sinf
#define cosf de_cosf
#define tanf de_tanf
#define atan de_atan
#define atan2 de_atan2
#define atan2f de_atan2f
#define asin de_asin
#define acos de_acos
#define acosf de_acosf
#define exp de_exp
#define log de_log
#define pow de_pow
#define powf de_powf
#define fabs de_fabs
#define fabsf de_fabsf
#define floor de_floor
#define floorf de_floorf
#define ceil de_ceil
#define ceilf de_ceilf
#define fmod de_fmod
double sqrt(double x);
float sqrtf(float x);
double sin(double x);
double cos(double x);
double tan(double x);
float sinf(float x);
float cosf(float x);
float tanf(float x);
double atan(double x);
double atan2(double y, double x);
float atan2f(float y, float x);
double asin(double x);
double acos(double x);
float acosf(float x);
double exp(double x);
double log(double x);
double pow(double x, double y);
float powf(float x, float y);
double fabs(double x);
float fabsf(float x);
double floor(double x);
float floorf(float x);
double ceil(double x);
float ceilf(float x);
double fmod(double x, double y);
#endif
