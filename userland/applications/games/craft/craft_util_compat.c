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

void load_png_texture(const char *file_name) {
    (void)file_name;
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
