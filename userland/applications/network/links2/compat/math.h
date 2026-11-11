#ifndef MATH_H
#define MATH_H
extern double pow(double, double);
extern double sqrt(double);
extern double fabs(double);
extern double floor(double);
extern double ceil(double);
extern double fmod(double, double);
extern double sin(double);
extern double cos(double);
extern double tan(double);
extern double log(double);
extern double log10(double);
extern double exp(double);
extern double atan2(double, double);
extern double powf(float, float);
#define HUGE_VAL (__builtin_huge_val())
#define M_PI 3.14159265358979323846
#endif
