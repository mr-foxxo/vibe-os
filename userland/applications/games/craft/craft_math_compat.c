#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define VIBE_PI 3.14159265358979323846

static double vibe_abs(double x) {
    return x < 0.0 ? -x : x;
}

static double vibe_floor(double x) {
    int i = (int)x;
    if ((double)i > x) {
        i -= 1;
    }
    return (double)i;
}

static double vibe_ceil(double x) {
    int i = (int)x;
    if ((double)i < x) {
        i += 1;
    }
    return (double)i;
}

static double vibe_round(double x) {
    if (x >= 0.0) {
        return vibe_floor(x + 0.5);
    }
    return vibe_ceil(x - 0.5);
}

static double vibe_wrap_pi(double x) {
    while (x > VIBE_PI) x -= 2.0 * VIBE_PI;
    while (x < -VIBE_PI) x += 2.0 * VIBE_PI;
    return x;
}

static double vibe_sin(double x) {
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

static double vibe_cos(double x) {
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

static double vibe_tan(double x) {
    double c = vibe_cos(x);
    if (c == 0.0) {
        return 0.0;
    }
    return vibe_sin(x) / c;
}

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

static double vibe_atan(double x) {
    if (x > 1.0) {
        return (VIBE_PI / 2.0) - vibe_atan_series(1.0 / x);
    }
    if (x < -1.0) {
        return -(VIBE_PI / 2.0) - vibe_atan_series(1.0 / x);
    }
    return vibe_atan_series(x);
}

static double vibe_atan2(double y, double x) {
    if (x > 0.0) return vibe_atan(y / x);
    if (x < 0.0 && y >= 0.0) return vibe_atan(y / x) + VIBE_PI;
    if (x < 0.0 && y < 0.0) return vibe_atan(y / x) - VIBE_PI;
    if (x == 0.0 && y > 0.0) return VIBE_PI / 2.0;
    if (x == 0.0 && y < 0.0) return -VIBE_PI / 2.0;
    return 0.0;
}

static double vibe_sqrt(double x) {
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

float fabsf(float x) { return (float)vibe_abs((double)x); }
float floorf(float x) { return (float)vibe_floor((double)x); }
float ceilf(float x) { return (float)vibe_ceil((double)x); }
float roundf(float x) { return (float)vibe_round((double)x); }
float fmodf(float x, float y) {
    if (y == 0.0f) {
        return 0.0f;
    }
    {
        float q = x / y;
        q = q >= 0.0f ? floorf(q) : ceilf(q);
        return x - (q * y);
    }
}
float sinf(float x) { return (float)vibe_sin((double)x); }
float cosf(float x) { return (float)vibe_cos((double)x); }
float tanf(float x) { return (float)vibe_tan((double)x); }
float atan2f(float y, float x) { return (float)vibe_atan2((double)y, (double)x); }
float sqrtf(float x) { return (float)vibe_sqrt((double)x); }
float acosf(float x) {
    if (x <= -1.0f) {
        return (float)VIBE_PI;
    }
    if (x >= 1.0f) {
        return 0.0f;
    }
    return atan2f(sqrtf(1.0f - x * x), x);
}

float powf(float x, float y) {
    int iy = (int)y;
    double result = 1.0;
    double base = (double)x;
    if ((float)iy != y) {
        return 1.0f;
    }
    if (iy < 0) {
        if (base == 0.0) {
            return 0.0f;
        }
        base = 1.0 / base;
        iy = -iy;
    }
    while (iy > 0) {
        if (iy & 1) {
            result *= base;
        }
        base *= base;
        iy >>= 1;
    }
    return (float)result;
}

void *calloc(size_t count, size_t size) {
    size_t total = count * size;
    unsigned char *ptr;
    if (count != 0u && total / count != size) {
        return 0;
    }
    ptr = (unsigned char *)malloc(total);
    if (!ptr) {
        return 0;
    }
    memset(ptr, 0, total);
    return ptr;
}

char *strncat(char *dest, const char *src, size_t n) {
    size_t dest_len = strlen(dest);
    size_t i = 0u;
    while (i < n && src[i] != '\0') {
        dest[dest_len + i] = src[i];
        i += 1u;
    }
    dest[dest_len + i] = '\0';
    return dest;
}

int atoi(const char *nptr) {
    int sign = 1;
    int value = 0;
    if (!nptr) {
        return 0;
    }
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') {
        nptr++;
    }
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }
    while (*nptr >= '0' && *nptr <= '9') {
        value = value * 10 + (*nptr - '0');
        nptr++;
    }
    return value * sign;
}
