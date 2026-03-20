#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define LODEPNG_NO_COMPILE_DISK
#include <userland/applications/games/craft/upstream/deps/lodepng/lodepng.h>
#include <userland/applications/games/craft/upstream/src/util.h>
#include <kernel/fs.h>

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
    unsigned int error;
    unsigned char *data;
    unsigned int width, height;

    int fd = open(file_name, 0);
    if (fd < 0) {
        return;
    }

    size_t buffer_size = 1024;
    unsigned char* buffer = malloc(buffer_size);
    size_t bytes_read = 0;
    int n;

    while ((n = read(fd, buffer + bytes_read, 1024)) > 0) {
        bytes_read += n;
        if (bytes_read == buffer_size) {
            buffer_size *= 2;
            buffer = realloc(buffer, buffer_size);
        }
    }
    close(fd);

    error = lodepng_decode32(&data, &width, &height, buffer, bytes_read);
    free(buffer);

    if (error) {
        // FIXME: don't exit here
        return;
    }
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
