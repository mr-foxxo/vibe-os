#include <lua_port.h>
#include <errno.h>
#include <userland/modules/include/console.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>

// This allocator is currently shared by the built-in userland runtime,
// not only by Lua. Keep it large enough for heavier apps such as DOOM.
#define VIBE_LUA_HEAP_SIZE (16u * 1024u * 1024u)

struct heap_block {
    size_t size;
    int free;
    struct heap_block *next;
};

static unsigned char g_lua_heap[VIBE_LUA_HEAP_SIZE];
static struct heap_block *g_heap_head = 0;
static int g_heap_init = 0;
static FILE g_stdin = {-1, 0, 0, 0, 0};
static struct lconv g_lconv = {"."};
static sighandler_t g_signal_handlers[8];

int errno = 0;
FILE *stdin = &g_stdin;

static void heap_init(void) {
    g_heap_head = (struct heap_block *)g_lua_heap;
    g_heap_head->size = VIBE_LUA_HEAP_SIZE - sizeof(struct heap_block);
    g_heap_head->free = 1;
    g_heap_head->next = 0;
    g_heap_init = 1;
}

static void *block_payload(struct heap_block *block) {
    return (void *)(block + 1);
}

static struct heap_block *payload_block(void *ptr) {
    return ((struct heap_block *)ptr) - 1;
}

static void split_block(struct heap_block *block, size_t size) {
    if (block->size <= size + sizeof(struct heap_block) + 8u) {
        return;
    }

    {
        struct heap_block *next = (struct heap_block *)((unsigned char *)block_payload(block) + size);
        next->size = block->size - size - sizeof(struct heap_block);
        next->free = 1;
        next->next = block->next;
        block->size = size;
        block->next = next;
    }
}

static void coalesce_blocks(void) {
    struct heap_block *block = g_heap_head;

    while (block && block->next) {
        if (block->free && block->next->free) {
            block->size += sizeof(struct heap_block) + block->next->size;
            block->next = block->next->next;
        } else {
            block = block->next;
        }
    }
}

static int is_digit_char(char c) {
    return c >= '0' && c <= '9';
}

static double vibe_lua_exp(double x) {
    const double ln2 = 0.6931471805599453;
    double result = 1.0;
    double term = 1.0;
    int k = 0;

    if (x == 0.0) {
        return 1.0;
    }
    while (x > ln2) {
        x -= ln2;
        ++k;
    }
    while (x < -ln2) {
        x += ln2;
        --k;
    }
    for (int i = 1; i < 18; ++i) {
        term = term * x / (double)i;
        result += term;
    }
    return ldexp(result, k);
}

static double vibe_lua_log(double x) {
    const double ln2 = 0.6931471805599453;
    double result = 0.0;
    double y;
    double term;

    if (x <= 0.0) {
        return 0.0;
    }

    while (x > 1.5) {
        x *= 0.5;
        result += ln2;
    }
    while (x < 0.75) {
        x *= 2.0;
        result -= ln2;
    }

    y = (x - 1.0) / (x + 1.0);
    term = y;
    for (int i = 1; i < 18; i += 2) {
        result += 2.0 * term / (double)i;
        term *= y * y;
    }
    return result;
}

void vibe_lua_console_write_len(const char *s, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        console_putc(s[i]);
    }
}

void vibe_lua_console_writef(const char *fmt, const char *arg) {
    while (*fmt != '\0') {
        if (fmt[0] == '%' && fmt[1] == 's') {
            if (arg) {
                console_write(arg);
            }
            fmt += 2;
            continue;
        }
        if (fmt[0] == '%' && fmt[1] == '%') {
            console_putc('%');
            fmt += 2;
            continue;
        }
        console_putc(*fmt++);
    }
}

int vibe_lua_integer_to_string(char *buf, size_t size, long long n) {
    unsigned long long value;
    char tmp[32];
    int pos = 0;
    int out = 0;

    if (size == 0u) {
        return 0;
    }

    if (n < 0) {
        if (out + 1 < (int)size) {
            buf[out++] = '-';
        }
        value = (unsigned long long)(-(n + 1)) + 1u;
    } else {
        value = (unsigned long long)n;
    }

    if (value == 0u) {
        tmp[pos++] = '0';
    } else {
        while (value > 0u && pos < (int)sizeof(tmp)) {
            tmp[pos++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }

    while (pos > 0 && out + 1 < (int)size) {
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
    return out;
}

int vibe_lua_pointer_to_string(char *buf, size_t size, const void *p) {
    uintptr_t value = (uintptr_t)p;
    char tmp[32];
    int pos = 0;
    int out = 0;

    if (size < 3u) {
        return 0;
    }

    buf[out++] = '0';
    buf[out++] = 'x';
    if (value == 0u) {
        buf[out++] = '0';
        buf[out] = '\0';
        return out;
    }

    while (value > 0u && pos < (int)sizeof(tmp)) {
        unsigned digit = (unsigned)(value & 0xFu);
        tmp[pos++] = (char)(digit < 10u ? '0' + digit : 'a' + (digit - 10u));
        value >>= 4u;
    }

    while (pos > 0 && out + 1 < (int)size) {
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
    return out;
}

int vibe_lua_number_to_string(char *buf, size_t size, double n) {
    long long integer = (long long)n;
    int out;
    int frac_digits = 6;
    double frac;

    if ((double)integer == n) {
        return vibe_lua_integer_to_string(buf, size, integer);
    }

    out = vibe_lua_integer_to_string(buf, size, integer);
    if (out + 2 >= (int)size) {
        return out;
    }

    if (n < 0.0 && integer == 0) {
        buf[out++] = '-';
    }
    buf[out++] = '.';

    frac = n - (double)integer;
    if (frac < 0.0) {
        frac = -frac;
    }

    while (frac_digits-- > 0 && out + 1 < (int)size) {
        int digit;
        frac *= 10.0;
        digit = (int)frac;
        buf[out++] = (char)('0' + digit);
        frac -= (double)digit;
    }

    while (out > 0 && buf[out - 1] == '0') {
        --out;
    }
    if (out > 0 && buf[out - 1] == '.') {
        --out;
    }
    buf[out] = '\0';
    return out;
}

double vibe_lua_str2number(const char *s, char **endptr) {
    double value = 0.0;
    double frac_scale = 0.1;
    int sign = 1;
    int exp_sign = 1;
    int exponent = 0;
    int seen_digit = 0;
    const char *p = s;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        ++p;
    }

    if (*p == '-') {
        sign = -1;
        ++p;
    } else if (*p == '+') {
        ++p;
    }

    while (is_digit_char(*p)) {
        seen_digit = 1;
        value = (value * 10.0) + (double)(*p - '0');
        ++p;
    }

    if (*p == '.') {
        ++p;
        while (is_digit_char(*p)) {
            seen_digit = 1;
            value += (double)(*p - '0') * frac_scale;
            frac_scale *= 0.1;
            ++p;
        }
    }

    if ((*p == 'e' || *p == 'E') && seen_digit) {
        ++p;
        if (*p == '-') {
            exp_sign = -1;
            ++p;
        } else if (*p == '+') {
            ++p;
        }
        while (is_digit_char(*p)) {
            exponent = exponent * 10 + (*p - '0');
            ++p;
        }
    }

    if (!seen_digit) {
        if (endptr) {
            *endptr = (char *)s;
        }
        return 0.0;
    }

    while (exponent-- > 0) {
        value = exp_sign > 0 ? value * 10.0 : value * 0.1;
    }

    if (endptr) {
        *endptr = (char *)p;
    }
    return value * (double)sign;
}

void *malloc(size_t size) {
    struct heap_block *block;

    if (!g_heap_init) {
        heap_init();
    }
    if (size == 0u) {
        return 0;
    }
    if (size & 7u) {
        size += 8u - (size & 7u);
    }

    block = g_heap_head;
    while (block) {
        if (block->free && block->size >= size) {
            split_block(block, size);
            block->free = 0;
            return block_payload(block);
        }
        block = block->next;
    }

    errno = ENOMEM;
    return 0;
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }
    payload_block(ptr)->free = 1;
    coalesce_blocks();
}

int abs(int value) {
    return value < 0 ? -value : value;
}

void *realloc(void *ptr, size_t size) {
    void *new_ptr;
    struct heap_block *block;

    if (ptr == 0) {
        return malloc(size);
    }
    if (size == 0u) {
        free(ptr);
        return 0;
    }

    block = payload_block(ptr);
    if (block->size >= size) {
        return ptr;
    }

    new_ptr = malloc(size);
    if (!new_ptr) {
        return 0;
    }
    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}

__attribute__((noreturn)) void abort(void) {
    console_write("lua abort\n");
    for (;;) {
        sys_yield();
    }
}

char *getenv(const char *name) {
    (void)name;
    return 0;
}

double strtod(const char *s, char **endptr) {
    return vibe_lua_str2number(s, endptr);
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s) {
        for (size_t i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i) {
        d[i] = (unsigned char)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *aa = (const unsigned char *)a;
    const unsigned char *bb = (const unsigned char *)b;
    for (size_t i = 0; i < n; ++i) {
        if (aa[i] != bb[i]) {
            return (int)aa[i] - (int)bb[i];
        }
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == (unsigned char)c) {
            return (void *)(p + i);
        }
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    return len;
}

char *strcpy(char *dst, const char *src) {
    size_t i = 0;
    do {
        dst[i] = src[i];
    } while (src[i++] != '\0');
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    while (i < n) {
        dst[i++] = '\0';
    }
    return dst;
}

char *strchr(const char *s, int c) {
    while (*s != '\0') {
        if (*s == (char)c) {
            return (char *)s;
        }
        ++s;
    }
    return (c == 0) ? (char *)s : 0;
}

int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return (unsigned char)*a - (unsigned char)*b;
        }
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
            return (unsigned char)a[i] - (unsigned char)b[i];
        }
    }
    return 0;
}

size_t strspn(const char *s, const char *accept) {
    size_t len = 0;
    while (s[len] != '\0') {
        if (!strchr(accept, s[len])) {
            break;
        }
        ++len;
    }
    return len;
}

size_t strcspn(const char *s, const char *reject) {
    size_t len = 0;
    while (s[len] != '\0') {
        if (strchr(reject, s[len])) {
            break;
        }
        ++len;
    }
    return len;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s != '\0') {
        if (strchr(accept, *s)) {
            return (char *)s;
        }
        ++s;
    }
    return 0;
}

char *strstr(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);

    if (needle_len == 0u) {
        return (char *)haystack;
    }

    while (*haystack != '\0') {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        ++haystack;
    }
    return 0;
}

int strcoll(const char *a, const char *b) {
    return strcmp(a, b);
}

char *strerror(int errnum) {
    switch (errnum) {
    case ENOENT: return "not found";
    case ENOMEM: return "out of memory";
    case EINVAL: return "invalid";
    default: return "error";
    }
}

int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isdigit(int c) {
    return c >= '0' && c <= '9';
}

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isspace(int c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v';
}

int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int toupper(int c) {
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

int tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

double floor(double x) {
    long long i = (long long)x;
    if ((double)i > x) {
        --i;
    }
    return (double)i;
}

double ceil(double x) {
    long long i = (long long)x;
    if ((double)i < x) {
        ++i;
    }
    return (double)i;
}

double fabs(double x) {
    return x < 0.0 ? -x : x;
}

double fmod(double x, double y) {
    long long q;

    if (y == 0.0) {
        return 0.0;
    }
    q = (long long)(x / y);
    return x - ((double)q * y);
}

double frexp(double x, int *exp) {
    double v = x;
    int e = 0;

    if (v == 0.0) {
        *exp = 0;
        return 0.0;
    }

    while (fabs(v) >= 1.0) {
        v *= 0.5;
        ++e;
    }
    while (fabs(v) < 0.5) {
        v *= 2.0;
        --e;
    }
    *exp = e;
    return v;
}

double ldexp(double x, int exp) {
    double result = x;
    while (exp > 0) {
        result *= 2.0;
        --exp;
    }
    while (exp < 0) {
        result *= 0.5;
        ++exp;
    }
    return result;
}

double pow(double x, double y) {
    long long yi = (long long)y;

    if ((double)yi == y) {
        double result = 1.0;
        int neg = 0;

        if (yi < 0) {
            neg = 1;
            yi = -yi;
        }
        while (yi > 0) {
            if (yi & 1LL) {
                result *= x;
            }
            x *= x;
            yi >>= 1;
        }
        return neg ? 1.0 / result : result;
    }

    if (x <= 0.0) {
        return 0.0;
    }
    return vibe_lua_exp(y * vibe_lua_log(x));
}

struct lconv *localeconv(void) {
    return &g_lconv;
}

char *setlocale(int category, const char *locale) {
    (void)category;
    (void)locale;
    return "C";
}

time_t time(time_t *out) {
    time_t now = (time_t)(sys_ticks() / 100u);
    if (out) {
        *out = now;
    }
    return now;
}

sighandler_t signal(int signum, sighandler_t handler) {
    sighandler_t old = SIG_DFL;

    if (signum >= 0 && signum < (int)(sizeof(g_signal_handlers) / sizeof(g_signal_handlers[0]))) {
        old = g_signal_handlers[signum];
        g_signal_handlers[signum] = handler;
    }
    return old;
}

FILE *fopen(const char *path, const char *mode) {
    int node;
    FILE *stream;

    (void)mode;
    node = fs_resolve(path);
    if (node < 0 || g_fs_nodes[node].is_dir) {
        errno = ENOENT;
        return 0;
    }

    stream = (FILE *)malloc(sizeof(FILE));
    if (!stream) {
        errno = ENOMEM;
        return 0;
    }
    stream->node = node;
    stream->pos = 0;
    stream->error = 0;
    stream->eof = 0;
    stream->owned = 1;
    return stream;
}

FILE *freopen(const char *path, const char *mode, FILE *stream) {
    int node;

    (void)mode;
    if (!stream) {
        return fopen(path, mode);
    }
    node = fs_resolve(path);
    if (node < 0 || g_fs_nodes[node].is_dir) {
        errno = ENOENT;
        return 0;
    }
    stream->node = node;
    stream->pos = 0;
    stream->error = 0;
    stream->eof = 0;
    return stream;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *stream) {
    size_t total = size * count;
    int read_count;

    if (!stream || stream->node < 0 || !ptr) {
        return 0u;
    }
    if (stream->pos >= g_fs_nodes[stream->node].size) {
        stream->eof = 1;
        return 0u;
    }

    read_count = fs_read_node_bytes(stream->node, stream->pos, ptr, (int)total);
    if (read_count <= 0) {
        stream->eof = 1;
        return 0u;
    }
    stream->pos += read_count;
    if ((size_t)read_count < total) {
        stream->eof = 1;
    }
    if (size == 0u) {
        return 0u;
    }
    return (size_t)read_count / size;
}

int getc(FILE *stream) {
    if (!stream) {
        return EOF;
    }
    if (stream == stdin) {
        return console_getchar();
    }
    if (stream->node < 0 || stream->pos >= g_fs_nodes[stream->node].size) {
        stream->eof = 1;
        return EOF;
    }
    {
        unsigned char value = 0;
        if (fs_read_node_bytes(stream->node, stream->pos, &value, 1) != 1) {
            stream->eof = 1;
            return EOF;
        }
        stream->pos += 1;
        return value;
    }
}

int fclose(FILE *stream) {
    if (stream && stream->owned) {
        free(stream);
    }
    return 0;
}

int feof(FILE *stream) {
    return stream ? stream->eof : 1;
}

int ferror(FILE *stream) {
    return stream ? stream->error : 1;
}

void clearerr(FILE *stream) {
    if (stream) {
        stream->error = 0;
        stream->eof = 0;
    }
}
