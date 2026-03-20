#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <userland/applications/games/craft/upstream/src/util.h>

static unsigned int g_craft_rng_state = 0x13579bdfu;

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
    glGenBuffers(1, &buffer);
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

static void craft_fill_rgba(uint8_t *pixels, int width, int height,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int count = width * height;
    for (int i = 0; i < count; ++i) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
}

static void craft_generate_block_texture(uint8_t *pixels, int width, int height) {
    static const uint8_t tile_colors[16][3] = {
        {110, 180, 90}, {194, 178, 128}, {128, 128, 128}, {164, 78, 52},
        {119, 84, 58},  {75, 151, 75},   {219, 211, 160}, {58, 110, 196},
        {205, 92, 92},  {210, 180, 140}, {170, 210, 255}, {100, 60, 160},
        {235, 221, 95}, {250, 250, 250}, {70, 70, 70},    {40, 40, 40}
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int tile_x = x / 16;
            int tile_y = y / 16;
            int tile = ((tile_y & 15) * 16) + (tile_x & 15);
            int palette = tile & 15;
            int checker = ((x ^ y) & 4) ? 12 : -12;
            int edge = ((x & 15) == 0 || (y & 15) == 0 || (x & 15) == 15 || (y & 15) == 15) ? -20 : 0;
            int base = (y * width + x) * 4;
            int r = tile_colors[palette][0] + checker + edge;
            int g = tile_colors[palette][1] + checker + edge;
            int b = tile_colors[palette][2] + checker + edge;
            if (palette == 10) {
                r = 190;
                g = 230;
                b = 255;
            }
            if (palette == 13) {
                r = 255;
                g = 0;
                b = 255;
            }
            if (r < 0) r = 0;
            if (g < 0) g = 0;
            if (b < 0) b = 0;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            pixels[base + 0] = (uint8_t)r;
            pixels[base + 1] = (uint8_t)g;
            pixels[base + 2] = (uint8_t)b;
            pixels[base + 3] = 255;
        }
    }
}

static void craft_generate_sky_texture(uint8_t *pixels, int width, int height) {
    for (int y = 0; y < height; ++y) {
        float t = (float)y / (float)(height > 1 ? height - 1 : 1);
        uint8_t r = (uint8_t)(40 + (1.0f - t) * 70.0f);
        uint8_t g = (uint8_t)(85 + (1.0f - t) * 90.0f);
        uint8_t b = (uint8_t)(140 + (1.0f - t) * 110.0f);
        for (int x = 0; x < width; ++x) {
            int base = (y * width + x) * 4;
            pixels[base + 0] = r;
            pixels[base + 1] = g;
            pixels[base + 2] = b;
            pixels[base + 3] = 255;
        }
    }
}

static void craft_generate_font_texture(uint8_t *pixels, int width, int height, int solid_alpha) {
    craft_fill_rgba(pixels, width, height, 255, 255, 255, 0);
    for (int cell_y = 0; cell_y < 16; ++cell_y) {
        for (int cell_x = 0; cell_x < 16; ++cell_x) {
            int origin_x = cell_x * 16;
            int origin_y = cell_y * 16;
            for (int py = 3; py < 13; ++py) {
                for (int px = 2; px < 14; ++px) {
                    int border = (px == 2 || px == 13 || py == 3 || py == 12);
                    int stroke = (((cell_x + cell_y + px + py) & 3) == 0);
                    int draw = solid_alpha ? (!border) : (border || stroke);
                    int base = ((origin_y + py) * width + origin_x + px) * 4;
                    if (!draw) {
                        continue;
                    }
                    pixels[base + 0] = 255;
                    pixels[base + 1] = 255;
                    pixels[base + 2] = 255;
                    pixels[base + 3] = 255;
                }
            }
        }
    }
}

void load_png_texture(const char *file_name) {
    int width = 256;
    int height = 256;
    uint8_t *pixels = (uint8_t *)malloc((size_t)width * (size_t)height * 4u);
    if (!pixels) {
        return;
    }
    if (file_name && strstr(file_name, "texture.png")) {
        craft_generate_block_texture(pixels, width, height);
    } else if (file_name && strstr(file_name, "sky.png")) {
        craft_generate_sky_texture(pixels, width, height);
    } else if (file_name && strstr(file_name, "sign.png")) {
        craft_generate_font_texture(pixels, width, height, 1);
    } else {
        craft_generate_font_texture(pixels, width, height, 0);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, pixels);
    free(pixels);
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
