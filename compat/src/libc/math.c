#ifndef __GNUC_PREREQ__
#define __GNUC_PREREQ__(maj, min) ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#endif
#ifndef __ISO_C_VISIBLE
#define __ISO_C_VISIBLE 1999
#endif
#ifndef __double_t
#define __double_t double
#endif
#ifndef __float_t
#define __float_t float
#endif

#include <math.h>
#include <stdint.h>

double sqrt(double x) { return __builtin_sqrt(x); }
float sqrtf(float x) { return __builtin_sqrtf(x); }
double sin(double x) { return __builtin_sin(x); }
float sinf(float x) { return __builtin_sinf(x); }
double cos(double x) { return __builtin_cos(x); }
float cosf(float x) { return __builtin_cosf(x); }
double tan(double x) { return __builtin_tan(x); }
float tanf(float x) { return __builtin_tanf(x); }
double atan(double x) { return __builtin_atan(x); }
float atanf(float x) { return __builtin_atanf(x); }
double atan2(double y, double x) { return __builtin_atan2(y, x); }
float atan2f(float y, float x) { return __builtin_atan2f(y, x); }
double floor(double x) { return __builtin_floor(x); }
float floorf(float x) { return __builtin_floorf(x); }
double ceil(double x) { return __builtin_ceil(x); }
float ceilf(float x) { return __builtin_ceilf(x); }
double fabs(double x) { return __builtin_fabs(x); }
float fabsf(float x) { return __builtin_fabsf(x); }
double fmod(double x, double y) { return __builtin_fmod(x, y); }
float fmodf(float x, float y) { return __builtin_fmodf(x, y); }
double pow(double x, double y) { return __builtin_pow(x, y); }
float powf(float x, float y) { return __builtin_powf(x, y); }
double exp(double x) { return __builtin_exp(x); }
float expf(float x) { return __builtin_expf(x); }
double log(double x) { return __builtin_log(x); }
float logf(float x) { return __builtin_logf(x); }
double log10(double x) { return __builtin_log10(x); }
float log10f(float x) { return __builtin_log10f(x); }
double sinh(double x) { return __builtin_sinh(x); }
float sinhf(float x) { return __builtin_sinhf(x); }
double cosh(double x) { return __builtin_cosh(x); }
float coshf(float x) { return __builtin_coshf(x); }
double tanh(double x) { return __builtin_tanh(x); }
float tanhf(float x) { return __builtin_tanhf(x); }
double asin(double x) { return __builtin_asin(x); }
float asinf(float x) { return __builtin_asinf(x); }
double acos(double x) { return __builtin_acos(x); }
float acosf(float x) { return __builtin_acosf(x); }
double ldexp(double x, int exp) { return __builtin_ldexp(x, exp); }
float ldexpf(float x, int exp) { return __builtin_ldexpf(x, exp); }
double frexp(double x, int *exp) { return __builtin_frexp(x, exp); }
float frexpf(float x, int *exp) { return __builtin_frexpf(x, exp); }
double modf(double x, double *iptr) { return __builtin_modf(x, iptr); }
float modff(float x, float *iptr) { return __builtin_modff(x, iptr); }
