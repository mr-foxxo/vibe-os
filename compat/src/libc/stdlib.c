#include "../include/compat/libc/stdlib.h"
#include "../include/compat/libc/string.h"
#include <lang/include/vibe_app_runtime.h>
#include <stdint.h>

void *malloc(size_t size) {
    return vibe_app_malloc(size);
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    return vibe_app_realloc(ptr, size);
}

void free(void *ptr) { vibe_app_free(ptr); }

void exit(int status) { 
    // TODO: This should terminate the process
    (void)status; 
    while(1); 
}

void abort(void) { 
    // TODO: This should terminate the process
    while(1); 
}

/* Number conversion */
int atoi(const char *str) {
    return (int)strtol(str, NULL, 10);
}

long atol(const char *str) {
    return strtol(str, NULL, 10);
}

long long atoll(const char *str) {
    return strtoll(str, NULL, 10);
}

/* String to long conversion */
long strtol(const char *nptr, char **endptr, int base) {
    long result = 0;
    int sign = 1;
    const char *s = nptr;
    
    // Skip whitespace
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    
    // Handle sign
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    // Auto-detect base if needed
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
            s++;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    
    // Parse digits
    while (*s) {
        int digit = -1;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) *endptr = (char *)s;
    return result * sign;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    long long result = 0;
    int sign = 1;
    const char *s = nptr;
    
    // Skip whitespace
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    
    // Handle sign
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    // Auto-detect base if needed
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
            s++;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    
    // Parse digits
    while (*s) {
        int digit = -1;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }
    
    if (endptr) *endptr = (char *)s;
    return result * sign;
}
