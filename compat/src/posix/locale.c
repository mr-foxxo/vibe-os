#include <locale.h>
#include <compat/libc/string.h>

static char g_locale_buf[64] = "C";
static char g_c_name[] = "C";
static char g_dot[] = ".";
static char g_empty[] = "";
static char g_charmap[] = "UTF-8";

static struct lconv g_c_lconv = {
    .decimal_point = g_dot,
    .thousands_sep = g_empty,
    .grouping = g_empty,
    .int_curr_symbol = g_empty,
    .currency_symbol = g_empty,
    .mon_decimal_point = g_empty,
    .mon_thousands_sep = g_empty,
    .mon_grouping = g_empty,
    .positive_sign = g_empty,
    .negative_sign = g_empty,
    .int_frac_digits = -1,
    .frac_digits = -1,
    .p_cs_precedes = -1,
    .p_sep_by_space = -1,
    .n_cs_precedes = -1,
    .n_sep_by_space = -1,
    .p_sign_posn = -1,
    .n_sign_posn = -1,
    .int_p_cs_precedes = -1,
    .int_p_sep_by_space = -1,
    .int_n_cs_precedes = -1,
    .int_n_sep_by_space = -1,
    .int_p_sign_posn = -1,
    .int_n_sign_posn = -1
};

char *setlocale(int category, const char *locale) {
    if (category < LC_ALL || category > LC_MESSAGES) {
        return 0;
    }
    if (locale == 0) {
        return g_locale_buf;
    }
    if (locale[0] == '\0') {
        return g_locale_buf;
    }
    if (strcmp(locale, "C") == 0 ||
        strcmp(locale, "POSIX") == 0 ||
        strcmp(locale, "en_US.UTF-8") == 0) {
        if (category == LC_ALL) {
            size_t len = strlen(locale);
            if (len >= sizeof(g_locale_buf)) {
                return 0;
            }
            memcpy(g_locale_buf, locale, len + 1);
        }
        return g_locale_buf;
    }
    return 0;
}

struct lconv *localeconv(void) {
    return &g_c_lconv;
}
