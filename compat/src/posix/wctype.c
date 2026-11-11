#include <wctype.h>
#include <string.h>

static int compat_iswcntrl(wint_t wc) {
    return (wc < 0x20) || (wc == 0x7f) ? 1 : 0;
}

static int compat_iswblank(wint_t wc) {
    return (wc == ' ' || wc == '\t') ? 1 : 0;
}

static int compat_iswspace(wint_t wc) {
    return (wc == ' ' || wc == '\t' || wc == '\n' || wc == '\r' ||
            wc == '\f' || wc == '\v') ? 1 : 0;
}

static int compat_iswdigit(wint_t wc) {
    return (wc >= '0' && wc <= '9') ? 1 : 0;
}

static int compat_iswxdigit(wint_t wc) {
    return compat_iswdigit(wc) || (wc >= 'a' && wc <= 'f') || (wc >= 'A' && wc <= 'F') ? 1 : 0;
}

static int compat_iswlower(wint_t wc) {
    return (wc >= 'a' && wc <= 'z') ? 1 : 0;
}

static int compat_iswupper(wint_t wc) {
    return (wc >= 'A' && wc <= 'Z') ? 1 : 0;
}

static int compat_iswalpha(wint_t wc) {
    return compat_iswlower(wc) || compat_iswupper(wc) ? 1 : 0;
}

static int compat_iswalnum(wint_t wc) {
    return compat_iswalpha(wc) || compat_iswdigit(wc) ? 1 : 0;
}

static int compat_iswprint(wint_t wc) {
    return (wc >= 0x20 && wc <= 0x7e) ? 1 : 0;
}

static int compat_iswgraph(wint_t wc) {
    return (wc >= 0x21 && wc <= 0x7e) ? 1 : 0;
}

static int compat_iswpunct(wint_t wc) {
    return compat_iswgraph(wc) && !compat_iswalnum(wc) ? 1 : 0;
}

int iswalnum(wint_t wc) { return compat_iswalnum(wc); }
int iswalpha(wint_t wc) { return compat_iswalpha(wc); }
int iswblank(wint_t wc) { return compat_iswblank(wc); }
int iswcntrl(wint_t wc) { return compat_iswcntrl(wc); }
int iswdigit(wint_t wc) { return compat_iswdigit(wc); }
int iswgraph(wint_t wc) { return compat_iswgraph(wc); }
int iswlower(wint_t wc) { return compat_iswlower(wc); }
int iswprint(wint_t wc) { return compat_iswprint(wc); }
int iswpunct(wint_t wc) { return compat_iswpunct(wc); }
int iswspace(wint_t wc) { return compat_iswspace(wc); }
int iswupper(wint_t wc) { return compat_iswupper(wc); }
int iswxdigit(wint_t wc) { return compat_iswxdigit(wc); }

wint_t towlower(wint_t wc) {
    if (compat_iswupper(wc)) return (wint_t)(wc + 32);
    return wc;
}

wint_t towupper(wint_t wc) {
    if (compat_iswlower(wc)) return (wint_t)(wc - 32);
    return wc;
}

static const struct {
    const char *name;
    int (*func)(wint_t);
} wctype_map[] = {
    {"alnum", compat_iswalnum},
    {"alpha", compat_iswalpha},
    {"blank", compat_iswblank},
    {"cntrl", compat_iswcntrl},
    {"digit", compat_iswdigit},
    {"graph", compat_iswgraph},
    {"lower", compat_iswlower},
    {"print", compat_iswprint},
    {"punct", compat_iswpunct},
    {"space", compat_iswspace},
    {"upper", compat_iswupper},
    {"xdigit", compat_iswxdigit},
    {0, 0}
};

static const struct {
    const char *name;
    wint_t (*func)(wint_t);
} wctrans_map[] = {
    {"tolower", towlower},
    {"toupper", towupper},
    {0, 0}
};

wctype_t wctype(const char *property) {
    int i;
    if (property == 0) return 0;
    for (i = 0; wctype_map[i].name != 0; ++i) {
        if (strcmp(wctype_map[i].name, property) == 0) return (wctype_t)wctype_map[i].func;
    }
    return 0;
}

int iswctype(wint_t wc, wctype_t desc) {
    if (desc == 0) return 0;
    return ((int (*)(wint_t))desc)(wc);
}

wctrans_t wctrans(const char *property) {
    int i;
    if (property == 0) return 0;
    for (i = 0; wctrans_map[i].name != 0; ++i) {
        if (strcmp(wctrans_map[i].name, property) == 0) return (wctrans_t)wctrans_map[i].func;
    }
    return 0;
}

wint_t towctrans(wint_t wc, wctrans_t desc) {
    if (desc == 0) return wc;
    return ((wint_t (*)(wint_t))desc)(wc);
}
