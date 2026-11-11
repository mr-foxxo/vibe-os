#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define LODEPNG_NO_COMPILE_DISK
#include <userland/applications/games/craft/upstream/deps/lodepng/lodepng.h>
#include <userland/applications/games/craft/upstream/src/util.h>
#include <userland/modules/include/fs.h>
#include <userland/modules/include/syscalls.h>

#define CRAFT_TILE_SIZE 16u
#define CRAFT_ATLAS_TILES 16u
#define CRAFT_ATLAS_SIZE (CRAFT_TILE_SIZE * CRAFT_ATLAS_TILES)

void flip_image_vertical(
    unsigned char *data, unsigned int width, unsigned int height);

static void craft_set_rgba(unsigned char *data, unsigned int width,
                           unsigned int x, unsigned int y,
                           unsigned char r, unsigned char g,
                           unsigned char b, unsigned char a) {
    size_t index;
    if (!data || x >= width) {
        return;
    }
    index = ((size_t)y * (size_t)width + (size_t)x) * 4u;
    data[index + 0] = r;
    data[index + 1] = g;
    data[index + 2] = b;
    data[index + 3] = a;
}

static void craft_fill_tile(unsigned char *data, unsigned int width,
                            unsigned int tile, unsigned char r,
                            unsigned char g, unsigned char b,
                            unsigned char a) {
    unsigned int tx = (tile % CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    unsigned int ty = (tile / CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    for (unsigned int y = 0; y < CRAFT_TILE_SIZE; ++y) {
        for (unsigned int x = 0; x < CRAFT_TILE_SIZE; ++x) {
            craft_set_rgba(data, width, tx + x, ty + y, r, g, b, a);
        }
    }
}

static void craft_fill_checker_tile(unsigned char *data, unsigned int width,
                                    unsigned int tile,
                                    unsigned char r1, unsigned char g1, unsigned char b1,
                                    unsigned char r2, unsigned char g2, unsigned char b2) {
    unsigned int tx = (tile % CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    unsigned int ty = (tile / CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    for (unsigned int y = 0; y < CRAFT_TILE_SIZE; ++y) {
        for (unsigned int x = 0; x < CRAFT_TILE_SIZE; ++x) {
            int alt = ((int)(x / 4u) + (int)(y / 4u)) & 1;
            craft_set_rgba(data, width, tx + x, ty + y,
                           alt ? r1 : r2,
                           alt ? g1 : g2,
                           alt ? b1 : b2,
                           255);
        }
    }
}

static void craft_fill_grass_side_tile(unsigned char *data, unsigned int width, unsigned int tile) {
    unsigned int tx = (tile % CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    unsigned int ty = (tile / CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    for (unsigned int y = 0; y < CRAFT_TILE_SIZE; ++y) {
        for (unsigned int x = 0; x < CRAFT_TILE_SIZE; ++x) {
            if (y < 4u) {
                craft_set_rgba(data, width, tx + x, ty + y, 64, 180, 72, 255);
            } else {
                unsigned char shade = (unsigned char)(104u + ((x + y) & 3u) * 6u);
                craft_set_rgba(data, width, tx + x, ty + y, shade, 84, 40, 255);
            }
        }
    }
}

static void craft_fill_leaves_tile(unsigned char *data, unsigned int width, unsigned int tile) {
    unsigned int tx = (tile % CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    unsigned int ty = (tile / CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    for (unsigned int y = 0; y < CRAFT_TILE_SIZE; ++y) {
        for (unsigned int x = 0; x < CRAFT_TILE_SIZE; ++x) {
            unsigned char r = 34;
            unsigned char g = (unsigned char)(120u + ((x * 3u + y * 5u) & 31u));
            unsigned char b = 32;
            craft_set_rgba(data, width, tx + x, ty + y, r, g, b, 255);
        }
    }
}

static void craft_fill_glass_tile(unsigned char *data, unsigned int width, unsigned int tile) {
    unsigned int tx = (tile % CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    unsigned int ty = (tile / CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    for (unsigned int y = 0; y < CRAFT_TILE_SIZE; ++y) {
        for (unsigned int x = 0; x < CRAFT_TILE_SIZE; ++x) {
            unsigned char a = 180;
            if (x == 0 || y == 0 || x == CRAFT_TILE_SIZE - 1 || y == CRAFT_TILE_SIZE - 1) {
                craft_set_rgba(data, width, tx + x, ty + y, 210, 248, 255, 255);
            } else {
                craft_set_rgba(data, width, tx + x, ty + y, 160, 220, 255, a);
            }
        }
    }
}

static void craft_fill_cross_plant_tile(unsigned char *data, unsigned int width, unsigned int tile,
                                        unsigned char r, unsigned char g, unsigned char b) {
    unsigned int tx = (tile % CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    unsigned int ty = (tile / CRAFT_ATLAS_TILES) * CRAFT_TILE_SIZE;
    for (unsigned int y = 0; y < CRAFT_TILE_SIZE; ++y) {
        for (unsigned int x = 0; x < CRAFT_TILE_SIZE; ++x) {
            unsigned int dx = x > 7u ? x - 7u : 7u - x;
            unsigned int dy = y > 7u ? y - 7u : 7u - y;
            unsigned char a = (dx == dy || dx + dy <= 2u) ? 255 : 0;
            craft_set_rgba(data, width, tx + x, ty + y, r, g, b, a);
        }
    }
}

static void craft_fill_palette_tile(unsigned char *data, unsigned int width,
                                    unsigned int tile, unsigned int palette_index) {
    unsigned char r = (unsigned char)(((palette_index >> 5) & 0x07u) * 255u / 7u);
    unsigned char g = (unsigned char)(((palette_index >> 2) & 0x07u) * 255u / 7u);
    unsigned char b = (unsigned char)((palette_index & 0x03u) * 255u / 3u);
    craft_fill_tile(data, width, tile, r, g, b, 255);
}

static void craft_make_fallback_atlas(void) {
    unsigned char *data;
    const unsigned int width = CRAFT_ATLAS_SIZE;
    const unsigned int height = CRAFT_ATLAS_SIZE;
    data = (unsigned char *)malloc((size_t)width * (size_t)height * 4u);
    if (!data) {
        return;
    }
    memset(data, 0, (size_t)width * (size_t)height * 4u);

    craft_fill_tile(data, width, 0, 110, 78, 42, 255);
    craft_fill_tile(data, width, 1, 232, 214, 136, 255);
    craft_fill_checker_tile(data, width, 2, 142, 142, 148, 112, 112, 120);
    craft_fill_checker_tile(data, width, 3, 170, 52, 44, 132, 34, 28);
    craft_fill_tile(data, width, 4, 158, 158, 164, 255);
    craft_fill_tile(data, width, 5, 190, 190, 196, 255);
    craft_fill_tile(data, width, 6, 124, 86, 52, 255);
    craft_fill_checker_tile(data, width, 7, 182, 146, 92, 154, 120, 72);
    craft_fill_tile(data, width, 8, 246, 246, 250, 255);
    craft_fill_glass_tile(data, width, 9);
    craft_fill_checker_tile(data, width, 10, 120, 120, 126, 84, 84, 90);
    craft_fill_tile(data, width, 11, 220, 214, 170, 255);
    craft_fill_tile(data, width, 12, 76, 80, 90, 255);
    craft_fill_checker_tile(data, width, 13, 130, 84, 30, 96, 60, 22);
    craft_fill_leaves_tile(data, width, 14);
    craft_fill_tile(data, width, 15, 240, 240, 248, 255);
    craft_fill_grass_side_tile(data, width, 16);
    craft_fill_grass_side_tile(data, width, 20);
    craft_fill_grass_side_tile(data, width, 24);
    craft_fill_tile(data, width, 32, 72, 188, 78, 255);
    craft_fill_checker_tile(data, width, 36, 166, 130, 76, 122, 88, 50);
    craft_fill_tile(data, width, 40, 252, 252, 255, 255);
    craft_fill_cross_plant_tile(data, width, 48, 56, 180, 72);
    craft_fill_cross_plant_tile(data, width, 49, 246, 220, 48);
    craft_fill_cross_plant_tile(data, width, 50, 220, 46, 46);
    craft_fill_cross_plant_tile(data, width, 51, 180, 70, 220);
    craft_fill_cross_plant_tile(data, width, 52, 248, 192, 40);
    craft_fill_cross_plant_tile(data, width, 53, 248, 248, 248);
    craft_fill_cross_plant_tile(data, width, 54, 56, 112, 232);
    for (unsigned int tile = 176u; tile <= 207u; ++tile) {
        craft_fill_palette_tile(data, width, tile, tile - 176u);
    }

    flip_image_vertical(data, width, height);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, data);
    free(data);
}

static void craft_make_fallback_sky(void) {
    unsigned char *data;
    const unsigned int width = 128u;
    const unsigned int height = 128u;
    data = (unsigned char *)malloc((size_t)width * (size_t)height * 4u);
    if (!data) {
        return;
    }
    for (unsigned int y = 0; y < height; ++y) {
        unsigned char r = (unsigned char)(96u + (32u * (height - 1u - y)) / (height - 1u));
        unsigned char g = (unsigned char)(168u + (36u * (height - 1u - y)) / (height - 1u));
        unsigned char b = (unsigned char)(220u + (28u * (height - 1u - y)) / (height - 1u));
        for (unsigned int x = 0; x < width; ++x) {
            craft_set_rgba(data, width, x, y, r, g, b, 255);
        }
    }
    flip_image_vertical(data, width, height);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, data);
    free(data);
}

static void craft_make_fallback_font_or_sign(unsigned char value) {
    unsigned char data[16 * 16 * 4];
    for (int i = 0; i < 16 * 16; ++i) {
        data[i * 4 + 0] = value;
        data[i * 4 + 1] = value;
        data[i * 4 + 2] = value;
        data[i * 4 + 3] = 255;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

static void craft_load_png_fallback(const char *file_name) {
    if (file_name) {
        sys_write_debug("craft: texture fallback ");
        sys_write_debug(file_name);
        sys_write_debug("\n");
    }
    if (!file_name) {
        return;
    }
    if (strstr(file_name, "texture.png")) {
        craft_make_fallback_atlas();
    } else if (strstr(file_name, "sky.png")) {
        craft_make_fallback_sky();
    } else if (strstr(file_name, "font.png")) {
        craft_make_fallback_font_or_sign(255);
    } else if (strstr(file_name, "sign.png")) {
        craft_make_fallback_font_or_sign(210);
    }
}

void flip_image_vertical(
    unsigned char *data, unsigned int width, unsigned int height)
{
    unsigned int size = width * height * 4;
    unsigned int stride = sizeof(char) * width * 4;
    unsigned char *new_data = malloc(sizeof(unsigned char) * size);
    for (unsigned int i = 0; i < height; i++) {
        unsigned int j = height - i - 1;
        memcpy(new_data + j * stride, data + i * stride, stride);
    }
    memcpy(data, new_data, size);
    free(new_data);
}

static unsigned int g_craft_rng_state = 0x13579bdfu;
int doom_snprintf(char *str, size_t size, const char *fmt, ...);
void sys_write_debug(const char *s);

static int craft_rand(void) {
    g_craft_rng_state = g_craft_rng_state * 1103515245u + 12345u;
    return (int)((g_craft_rng_state >> 16) & 0x7fffu);
}

int rand_int(int n) {
    if (n <= 0) {
        return 0;
    }
    return craft_rand() % n;
}

double rand_double() {
    return (double)(craft_rand() & 0x7fff) / 32767.0;
}

void update_fps(FPS *fps) {
    if (!fps) {
        return;
    }
    fps->frames++;
    if (glfwGetTime() - fps->since >= 1.0) {
        fps->fps = fps->frames;
        fps->frames = 0;
        fps->since = glfwGetTime();
    }
}

GLuint gen_buffer(GLsizei size, GLfloat *data) {
    GLuint buffer = 0;
    static int craft_buffer_debug_budget = 32;

    glGenBuffers(1, &buffer);
    if ((buffer == 0 || size > (GLsizei)(64 * 1024)) && craft_buffer_debug_budget > 0) {
        char line[128];
        craft_buffer_debug_budget--;
        doom_snprintf(line, sizeof(line),
                      "craft: gen_buffer size=%d buffer=%u data=%u\n",
                      (int)size, (unsigned int)buffer, data != NULL ? 1u : 0u);
        sys_write_debug(line);
    }
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return buffer;
}

void del_buffer(GLuint buffer) {
    glDeleteBuffers(1, &buffer);
}

GLfloat *malloc_faces(int components, int faces) {
    return (GLfloat *)malloc(sizeof(GLfloat) * 6 * components * faces);
}

GLuint gen_faces(int components, int faces, GLfloat *data) {
    GLuint buffer = gen_buffer(sizeof(GLfloat) * 6 * components * faces, data);
    free(data);
    return buffer;
}

GLuint make_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, (const GLchar *const *)&source, 0);
    glCompileShader(shader);
    return shader;
}

GLuint load_shader(GLenum type, const char *path) {
    (void)path;
    return make_shader(type, "");
}

GLuint make_program(GLuint shader1, GLuint shader2) {
    GLuint program = glCreateProgram();
    glAttachShader(program, shader1);
    glAttachShader(program, shader2);
    glLinkProgram(program);
    glDetachShader(program, shader1);
    glDetachShader(program, shader2);
    glDeleteShader(shader1);
    glDeleteShader(shader2);
    return program;
}

GLuint load_program(const char *path1, const char *path2) {
    (void)path1;
    (void)path2;
    return make_program(load_shader(GL_VERTEX_SHADER, 0), load_shader(GL_FRAGMENT_SHADER, 0));
}










void load_png_texture(const char *file_name) {
    unsigned int error;
    unsigned char *data;
    unsigned int width, height;
    int node = -1;
    unsigned char *buffer;
    int bytes_read;
    char absolute_path[80];

    if (file_name) {
        node = fs_resolve(file_name);
        if (node < 0 && file_name[0] != '/') {
            int pos = 0;
            absolute_path[pos++] = '/';
            while (file_name[pos - 1] != '\0' && pos < (int)sizeof(absolute_path) - 1) {
                absolute_path[pos] = file_name[pos - 1];
                ++pos;
            }
            absolute_path[pos] = '\0';
            node = fs_resolve(absolute_path);
        }
    }

    if (node < 0) {
        craft_load_png_fallback(file_name);
        return;
    }
    if (g_fs_nodes[node].size <= 0) {
        craft_load_png_fallback(file_name);
        return;
    }
    buffer = malloc((size_t)g_fs_nodes[node].size);
    if (!buffer) {
        craft_load_png_fallback(file_name);
        return;
    }
    bytes_read = fs_read_node_bytes(node, 0, buffer, g_fs_nodes[node].size);
    if (bytes_read != g_fs_nodes[node].size) {
        free(buffer);
        craft_load_png_fallback(file_name);
        return;
    }

    error = lodepng_decode32(&data, &width, &height, buffer, bytes_read);
    free(buffer);

    if (error) {
        craft_load_png_fallback(file_name);
        return;
    }
    sys_write_debug("craft: texture loaded ");
    sys_write_debug(file_name);
    sys_write_debug("\n");
    flip_image_vertical(data, width, height);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, data);
    free(data);
}


char *tokenize(char *str, const char *delim, char **key) {
    char *result;
    if (str == 0) {
        str = *key;
    }
    while (*str && strchr(delim, *str)) {
        str++;
    }
    if (*str == '\0') {
        return 0;
    }
    result = str;
    while (*str && !strchr(delim, *str)) {
        str++;
    }
    if (*str) {
        *str++ = '\0';
    }
    *key = str;
    return result;
}

int char_width(char input) {
    (void)input;
    return 6;
}

int string_width(const char *input) {
    return (int)strlen(input) * 6;
}

int wrap(const char *input, int max_width, char *output, int max_length) {
    (void)max_width;
    if (max_length <= 0) {
        return 0;
    }
    strncpy(output, input, max_length - 1);
    output[max_length - 1] = '\0';
    return 1;
}

static int craft_scan_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int craft_scan_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static void craft_scan_skip_space(const char **cursor) {
    while (**cursor && craft_scan_is_space(**cursor)) {
        *cursor += 1;
    }
}

static int craft_scan_parse_int(const char **cursor, int *out_value) {
    int sign = 1;
    int value = 0;
    int found = 0;
    const char *s = *cursor;

    if (*s == '-') {
        sign = -1;
        s += 1;
    } else if (*s == '+') {
        s += 1;
    }
    while (craft_scan_is_digit(*s)) {
        value = value * 10 + (*s - '0');
        s += 1;
        found = 1;
    }
    if (!found) {
        return 0;
    }
    *out_value = value * sign;
    *cursor = s;
    return 1;
}

static int craft_scan_parse_unsigned(const char **cursor, unsigned int *out_value) {
    unsigned int value = 0;
    int found = 0;
    const char *s = *cursor;

    while (craft_scan_is_digit(*s)) {
        value = value * 10u + (unsigned int)(*s - '0');
        s += 1;
        found = 1;
    }
    if (!found) {
        return 0;
    }
    *out_value = value;
    *cursor = s;
    return 1;
}

static double craft_scan_pow10(int exponent) {
    double scale = 1.0;
    while (exponent > 0) {
        scale *= 10.0;
        exponent -= 1;
    }
    return scale;
}

static int craft_scan_parse_double(const char **cursor, double *out_value) {
    int sign = 1;
    int exponent_sign = 1;
    int exponent = 0;
    double integer = 0.0;
    double fraction = 0.0;
    double scale = 1.0;
    int found = 0;
    const char *s = *cursor;

    if (*s == '-') {
        sign = -1;
        s += 1;
    } else if (*s == '+') {
        s += 1;
    }
    while (craft_scan_is_digit(*s)) {
        integer = integer * 10.0 + (double)(*s - '0');
        s += 1;
        found = 1;
    }
    if (*s == '.') {
        s += 1;
        while (craft_scan_is_digit(*s)) {
            fraction = fraction * 10.0 + (double)(*s - '0');
            scale *= 10.0;
            s += 1;
            found = 1;
        }
    }
    if (!found) {
        return 0;
    }
    if (*s == 'e' || *s == 'E') {
        const char *exp_start = s + 1;
        if (*exp_start == '-') {
            exponent_sign = -1;
            exp_start += 1;
        } else if (*exp_start == '+') {
            exp_start += 1;
        }
        if (!craft_scan_is_digit(*exp_start)) {
            return 0;
        }
        s = exp_start;
        while (craft_scan_is_digit(*s)) {
            exponent = exponent * 10 + (*s - '0');
            s += 1;
        }
    }

    *out_value = (integer + (fraction / scale)) * (double)sign;
    if (exponent != 0) {
        double exp_scale = craft_scan_pow10(exponent);
        if (exponent_sign < 0) {
            *out_value /= exp_scale;
        } else {
            *out_value *= exp_scale;
        }
    }
    *cursor = s;
    return 1;
}

static int craft_scan_parse_string(const char **cursor, char *out, size_t width) {
    size_t len = 0;
    const char *s = *cursor;

    if (*s == '\0' || craft_scan_is_space(*s)) {
        return 0;
    }
    while (*s && !craft_scan_is_space(*s) && len + 1u < width) {
        out[len++] = *s++;
    }
    if (len == 0u) {
        return 0;
    }
    out[len] = '\0';
    while (*s && !craft_scan_is_space(*s)) {
        s += 1;
    }
    *cursor = s;
    return 1;
}

/* strnlen is now provided by compat/libc */

int __isoc99_vsscanf(const char *str, const char *fmt, va_list ap) {
    const char *s = str;
    const char *f = fmt;
    int assigned = 0;

    while (*f) {
        size_t width = (size_t)-1;
        int long_modifier = 0;

        if (craft_scan_is_space(*f)) {
            craft_scan_skip_space(&s);
            f += 1;
            continue;
        }
        if (*f != '%') {
            if (*s != *f) {
                break;
            }
            s += 1;
            f += 1;
            continue;
        }

        f += 1;
        if (*f == '%') {
            if (*s != '%') {
                break;
            }
            s += 1;
            f += 1;
            continue;
        }
        if (craft_scan_is_digit(*f)) {
            width = 0u;
            while (craft_scan_is_digit(*f)) {
                width = width * 10u + (size_t)(*f - '0');
                f += 1;
            }
        }
        if (*f == 'l') {
            long_modifier = 1;
            f += 1;
        }

        craft_scan_skip_space(&s);
        if (*f == 'd') {
            int value;
            int *out = va_arg(ap, int *);
            if (!craft_scan_parse_int(&s, &value)) {
                break;
            }
            *out = value;
            assigned += 1;
            f += 1;
            continue;
        }
        if (*f == 'u') {
            unsigned int value;
            unsigned int *out = va_arg(ap, unsigned int *);
            if (!craft_scan_parse_unsigned(&s, &value)) {
                break;
            }
            *out = value;
            assigned += 1;
            f += 1;
            continue;
        }
        if (*f == 'f') {
            double value;
            if (!craft_scan_parse_double(&s, &value)) {
                break;
            }
            if (long_modifier) {
                double *out = va_arg(ap, double *);
                *out = value;
            } else {
                float *out = va_arg(ap, float *);
                *out = (float)value;
            }
            assigned += 1;
            f += 1;
            continue;
        }
        if (*f == 's') {
            char *out = va_arg(ap, char *);
            size_t max_width = width == (size_t)-1 ? (size_t)4096u : width + 1u;
            if (!craft_scan_parse_string(&s, out, max_width)) {
                break;
            }
            assigned += 1;
            f += 1;
            continue;
        }

        break;
    }

    return assigned;
}

int __isoc99_sscanf(const char *str, const char *fmt, ...) {
    int result;
    va_list ap;

    va_start(ap, fmt);
    result = __isoc99_vsscanf(str, fmt, ap);
    va_end(ap);
    return result;
}
