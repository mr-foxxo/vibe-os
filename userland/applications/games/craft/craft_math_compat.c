#include <math.h>

#define VIBE_PI 3.14159265358979323846

static double vibe_abs(double x) {
    return x < 0.0 ? -x : x;
}

double fabs(double x) { return vibe_abs(x); }
float fabsf(float x) { return (float)vibe_abs((double)x); }

double floor(double x) {
    int i = (int)x;
    if ((double)i > x) {
        i -= 1;
    }
    return (double)i;
}

float floorf(float x) { return (float)floor((double)x); }

double ceil(double x) {
    int i = (int)x;
    if ((double)i < x) {
        i += 1;
    }
    return (double)i;
}

float ceilf(float x) { return (float)ceil((double)x); }

double round(double x) {
    if (x >= 0.0) {
        return floor(x + 0.5);
    }
    return ceil(x - 0.5);
}

float roundf(float x) { return (float)round((double)x); }

double fmod(double x, double y) {
    if (y == 0.0) {
        return 0.0;
    }
    {
        double q = x / y;
        q = q >= 0.0 ? floor(q) : ceil(q);
        return x - (q * y);
    }
}

float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }

static double vibe_wrap_pi(double x) {
    while (x > VIBE_PI) x -= 2.0 * VIBE_PI;
    while (x < -VIBE_PI) x += 2.0 * VIBE_PI;
    return x;
}

double sin(double x) {
    double term;
    double sum;
    x = vibe_wrap_pi(x);
    term = x;
    sum = x;
    for (int i = 1; i < 7; ++i) {
        double a = (2.0 * i) * (2.0 * i + 1.0);
        term *= -(x * x) / a;
        sum += term;
    }
    return sum;
}

float sinf(float x) { return (float)sin((double)x); }

double cos(double x) {
    double term;
    double sum;
    x = vibe_wrap_pi(x);
    term = 1.0;
    sum = 1.0;
    for (int i = 1; i < 7; ++i) {
        double a = (2.0 * i - 1.0) * (2.0 * i);
        term *= -(x * x) / a;
        sum += term;
    }
    return sum;
}

float cosf(float x) { return (float)cos((double)x); }

double tan(double x) {
    double c = cos(x);
    if (c == 0.0) {
        return 0.0;
    }
    return sin(x) / c;
}

float tanf(float x) { return (float)tan((double)x); }

double acos(double x) {
    if (x <= -1.0) {
        return VIBE_PI;
    }
    if (x >= 1.0) {
        return 0.0;
    }
    return atan2(sqrt(1.0 - x * x), x);
}

float acosf(float x) { return (float)acos((double)x); }

static double vibe_atan_series(double x) {
    double xx = x * x;
    double term = x;
    double sum = x;
    for (int i = 1; i < 12; ++i) {
        term *= -xx;
        sum += term / (2.0 * i + 1.0);
    }
    return sum;
}

double atan(double x) {
    if (x > 1.0) {
        return (VIBE_PI / 2.0) - vibe_atan_series(1.0 / x);
    }
    if (x < -1.0) {
        return -(VIBE_PI / 2.0) - vibe_atan_series(1.0 / x);
    }
    return vibe_atan_series(x);
}

double atan2(double y, double x) {
    if (x > 0.0) return atan(y / x);
    if (x < 0.0 && y >= 0.0) return atan(y / x) + VIBE_PI;
    if (x < 0.0 && y < 0.0) return atan(y / x) - VIBE_PI;
    if (x == 0.0 && y > 0.0) return VIBE_PI / 2.0;
    if (x == 0.0 && y < 0.0) return -VIBE_PI / 2.0;
    return 0.0;
}

float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }

double sqrt(double x) {
    double guess;
    if (x <= 0.0) {
        return 0.0;
    }
    guess = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 12; ++i) {
        guess = 0.5 * (guess + x / guess);
    }
    return guess;
}

float sqrtf(float x) { return (float)sqrt((double)x); }

double pow(double x, double y) {
    int iy = (int)y;
    double result = 1.0;
    if ((double)iy != y) {
        return 1.0;
    }
    if (iy < 0) {
        if (x == 0.0) {
            return 0.0;
        }
        x = 1.0 / x;
        iy = -iy;
    }
    while (iy > 0) {
        if (iy & 1) {
            result *= x;
        }
        x *= x;
        iy >>= 1;
    }
    return result;
}

float powf(float x, float y) { return (float)pow((double)x, (double)y); }
