/*
 * Standard Library Implementation  
 * compat/src/libc/stdlib.c
 */

#include <compat/libc/stdlib.h>
#include <compat/libc/string.h>
#include <compat/posix/unistd.h>
#include <stdbool.h>

/* String to integer conversion */

int atoi(const char *nptr) {
    return (int)strtol(nptr, NULL, 10);
}

long atol(const char *nptr) {
    return strtol(nptr, NULL, 10);
}

long long atoll(const char *nptr) {
    return strtoll(nptr, NULL, 10);
}

double atof(const char *nptr) {
    return strtod(nptr, NULL);
}

long strtol(const char *nptr, char **endptr, int base) {
    long result = 0;
    bool negative = false;
    
    if (!nptr) { if (endptr) *endptr = (char *)nptr; return 0; }
    
    /* Skip whitespace */
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n') nptr++;
    
    /* Handle sign */
    if (*nptr == '-') { negative = true; nptr++; }
    else if (*nptr == '+') { nptr++; }
    
    /* Infer or validate base */
    if (base == 0) {
        if (nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
            base = 16;
            nptr += 2;
        } else if (nptr[0] == '0') {
            base = 8;
            nptr++;
        } else {
            base = 10;
        }
    } else if (base == 16 && nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
        nptr += 2;
    }
    
    if (base < 2 || base > 36) { if (endptr) *endptr = (char *)nptr; return 0; }
    
    /* Parse digits */
    while (*nptr) {
        int digit = -1;
        if (*nptr >= '0' && *nptr <= '9') {
            digit = *nptr - '0';
        } else if (*nptr >= 'a' && *nptr <= 'f') {
            digit = *nptr - 'a' + 10;
        } else if (*nptr >= 'A' && *nptr <= 'F') {
            digit = *nptr - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) break;
        result = result * base + digit;
        nptr++;
    }
    
    if (endptr) *endptr = (char *)nptr;
    return negative ? -result : result;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    long long result = 0;
    bool negative = false;
    
    if (!nptr) { if (endptr) *endptr = (char *)nptr; return 0; }
    
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n') nptr++;
    if (*nptr == '-') { negative = true; nptr++; }
    else if (*nptr == '+') { nptr++; }
    
    if (base == 0) {
        if (nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
            base = 16;
            nptr += 2;
        } else if (nptr[0] == '0') {
            base = 8;
            nptr++;
        } else {
            base = 10;
        }
    } else if (base == 16 && nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
        nptr += 2;
    }
    
    if (base < 2 || base > 36) { if (endptr) *endptr = (char *)nptr; return 0; }
    
    while (*nptr) {
        int digit = -1;
        if (*nptr >= '0' && *nptr <= '9') {
            digit = *nptr - '0';
        } else if (*nptr >= 'a' && *nptr <= 'f') {
            digit = *nptr - 'a' + 10;
        } else if (*nptr >= 'A' && *nptr <= 'F') {
            digit = *nptr - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) break;
        result = result * base + digit;
        nptr++;
    }
    
    if (endptr) *endptr = (char *)nptr;
    return negative ? -result : result;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtol(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)strtoll(nptr, endptr, base);
}

double strtod(const char *nptr, char **endptr) {
    /* Minimal implementation - just handle integer part for now */
    double result = 0.0;
    bool negative = false;
    
    if (!nptr) { if (endptr) *endptr = (char *)nptr; return 0.0; }
    
    while (*nptr == ' ' || *nptr == '\t') nptr++;
    if (*nptr == '-') { negative = true; nptr++; }
    else if (*nptr == '+') { nptr++; }
    
    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10.0 + (*nptr - '0');
        nptr++;
    }
    
    if (*nptr == '.') {
        nptr++;
        double frac = 0.1;
        while (*nptr >= '0' && *nptr <= '9') {
            result += frac * (*nptr - '0');
            frac *= 0.1;
            nptr++;
        }
    }
    
    if (endptr) *endptr = (char *)nptr;
    return negative ? -result : result;
}

/* Math utilities */

int abs(int j) {
    return j < 0 ? -j : j;
}

long labs(long j) {
    return j < 0 ? -j : j;
}

long long llabs(long long j) {
    return j < 0 ? -j : j;
}

/* Sorting */

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    if (nmemb <= 1) return;
    
    /* Simple bubble sort for now - OK for small arrays */
    unsigned char *arr = (unsigned char *)base;
    for (size_t i = 0; i < nmemb - 1; i++) {
        for (size_t j = 0; j < nmemb - i - 1; j++) {
            if (compar(&arr[j * size], &arr[(j + 1) * size]) > 0) {
                /* Swap */
                for (size_t k = 0; k < size; k++) {
                    unsigned char tmp = arr[j * size + k];
                    arr[j * size + k] = arr[(j + 1) * size + k];
                    arr[(j + 1) * size + k] = tmp;
                }
            }
        }
    }
}

void *bsearch(const void *key, const void *base,
              size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    if (!base || nmemb == 0) return NULL;
    
    size_t left = 0, right = nmemb;
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const void *mid_elem = (const unsigned char *)base + mid * size;
        int cmp = compar(key, mid_elem);
        
        if (cmp == 0) return (void *)mid_elem;
        if (cmp < 0) right = mid;
        else left = mid + 1;
    }
    return NULL;
}

/* Random - basic */

static unsigned int rand_seed = 1;

int rand(void) {
    rand_seed = (rand_seed * 1103515245 + 12345) & 0x7fffffff;
    return (int)rand_seed;
}

void srand(unsigned int seed) {
    rand_seed = seed;
}

/* Environment */

char *getenv(const char *name) {
    /* Fake/stub - no real environment on VibeOS yet */
    (void)name;
    return NULL;
}
