#include <compat_defs.h>
#include "../include/compat/libc/ctype.h"

int isalpha(int c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int isalnum(int c) {
    return (isalpha(c) || isdigit(c));
}

int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

int toupper(int c) {
    if (islower(c)) {
        return c - 'a' + 'A';
    }
    return c;
}

int tolower(int c) {
    if (isupper(c)) {
        return c - 'A' + 'a';
    }
    return c;
}
