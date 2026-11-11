#include <wchar.h>
#include <compat/libc/string.h>
#include <compat/posix/errno.h>
#include <stdlib.h>

wint_t btowc(int c) {
    return (wint_t)(unsigned char)c;
}

int wctob(wint_t c) {
    return (int)c;
}

size_t mbrlen(const char *s, size_t n, mbstate_t *ps) {
    static mbstate_t internal;
    if (!ps) ps = &internal;
    (void)ps;
    if (!s) return 0;
    if (n == 0) return (size_t)-2;
    if ((unsigned char)*s == 0) return 0;
    return 1;
}

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps) {
    static mbstate_t internal;
    (void)ps;
    if (!ps) ps = &internal;
    if (!s) return 0;
    if (n == 0) return (size_t)-2;
    if (pwc) *pwc = (wchar_t)(unsigned char)*s;
    return (*s != '\0') ? 1 : 0;
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps) {
    static mbstate_t internal;
    (void)ps;
    if (!ps) ps = &internal;
    if (!s) return 1;
    *s = (char)wc;
    return 1;
}

size_t wcslen(const wchar_t *s) {
    size_t n = 0;
    if (s) while (s[n]) n++;
    return n;
}

wchar_t *wcscpy(wchar_t *dest, const wchar_t *src) {
    wchar_t *d = dest;
    while ((*d++ = *src++));
    return dest;
}

int wcscmp(const wchar_t *s1, const wchar_t *s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (int)(*s1 - *s2);
}

wchar_t *wcschr(const wchar_t *s, wchar_t c) {
    if (!s) return 0;
    while (*s) {
        if (*s == c) return (wchar_t *)s;
        s++;
    }
    if (c == 0) return (wchar_t *)s;
    return 0;
}

long wcstol(const wchar_t *nptr, wchar_t **endptr, int base) {
    long result = 0;
    int sign = 1;
    const wchar_t *s = nptr;
    while (*s == L' ' || *s == L'\t' || *s == L'\n' || *s == L'\r') s++;
    if (*s == L'-') { sign = -1; s++; }
    else if (*s == L'+') s++;
    if (base == 0) {
        if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) { base = 16; s += 2; }
        else if (s[0] == L'0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) s += 2;
    while (*s) {
        int digit = -1;
        if (*s >= L'0' && *s <= L'9') digit = *s - L'0';
        else if (*s >= L'a' && *s <= L'z') digit = *s - L'a' + 10;
        else if (*s >= L'A' && *s <= L'Z') digit = *s - L'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }
    if (endptr) *endptr = (wchar_t *)s;
    return result * sign;
}
