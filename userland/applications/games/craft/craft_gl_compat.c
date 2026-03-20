#include <GL/glew.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <userland/modules/include/syscalls.h>

#define CRAFT_MAX_BUFFERS 4096
#define CRAFT_MAX_TEXTURES 64
#define CRAFT_MAX_PROGRAMS 16
#define CRAFT_MAX_VERTICES 1572864

enum {
    CRAFT_ATTRIB_POSITION = 0,
    CRAFT_ATTRIB_NORMAL = 1,
    CRAFT_ATTRIB_UV = 2
};

enum {
    CRAFT_UNIFORM_MATRIX = 0,
    CRAFT_UNIFORM_SAMPLER = 1,
    CRAFT_UNIFORM_CAMERA = 2,
    CRAFT_UNIFORM_TIMER = 3,
    CRAFT_UNIFORM_EXTRA1 = 4,
    CRAFT_UNIFORM_EXTRA2 = 5,
    CRAFT_UNIFORM_EXTRA3 = 6,
    CRAFT_UNIFORM_EXTRA4 = 7
};

enum {
    CRAFT_PROGRAM_BLOCK = 1,
    CRAFT_PROGRAM_LINE = 2,
    CRAFT_PROGRAM_TEXT = 3,
    CRAFT_PROGRAM_SKY = 4
};

struct craft_buffer {
    uint8_t *data;
    int size;
};

struct craft_texture {
    uint32_t *pixels;
    int width;
    int height;
    int min_filter;
    int mag_filter;
    int wrap_s;
    int wrap_t;
};

struct craft_program {
    int kind;
    float matrix[16];
    float camera[3];
    float timer;
    float extra_f[4];
    int sampler;
    int extra_i[4];
};

struct craft_attrib_state {
    int enabled;
    int size;
    int stride;
    const uint8_t *pointer;
    GLuint buffer;
};

struct craft_vertex {
    float sx;
    float sy;
    float sz;
    float inv_w;
    float u;
    float v;
    float ao;
    float light;
    float nx;
    float ny;
    float nz;
};

static struct craft_buffer g_buffers[CRAFT_MAX_BUFFERS];
static struct craft_texture g_textures[CRAFT_MAX_TEXTURES];
static struct craft_program g_programs[CRAFT_MAX_PROGRAMS];
static struct craft_attrib_state g_attribs[8];
static uint8_t *g_framebuffer = NULL;
static float *g_depthbuffer = NULL;
static int g_fb_width = 0;
static int g_fb_height = 0;
static uint8_t g_saved_palette[256 * 3];
static int g_saved_palette_valid = 0;
static GLuint g_next_shader_id = 1u;
static GLuint g_next_buffer_id = 1u;
static GLuint g_next_texture_id = 1u;
static GLuint g_next_program_id = 1u;
static GLuint g_bound_buffer = 0u;
static GLuint g_current_program = 0u;
static GLuint g_bound_textures[4] = {0u, 0u, 0u, 0u};
static GLuint g_active_texture = 0u;
static int g_viewport_x = 0;
static int g_viewport_y = 0;
static int g_viewport_w = 0;
static int g_viewport_h = 0;
static int g_scissor_x = 0;
static int g_scissor_y = 0;
static int g_scissor_w = 0;
static int g_scissor_h = 0;
static int g_enable_depth = 0;
static int g_enable_blend = 0;
static int g_enable_cull = 0;
static int g_enable_scissor = 0;
static int g_enable_logic_op = 0;
static float g_clear_r = 0.0f;
static float g_clear_g = 0.0f;
static float g_clear_b = 0.0f;

static float craft_absf(float v) {
    return v < 0.0f ? -v : v;
}

static float craft_minf(float a, float b) {
    return a < b ? a : b;
}

static float craft_maxf(float a, float b) {
    return a > b ? a : b;
}

static float craft_clampf(float value, float lo, float hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static int craft_clampi(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static uint8_t craft_rgb_to_index(float r, float g, float b) {
    int ri = craft_clampi((int)(craft_clampf(r, 0.0f, 1.0f) * 7.0f + 0.5f), 0, 7);
    int gi = craft_clampi((int)(craft_clampf(g, 0.0f, 1.0f) * 7.0f + 0.5f), 0, 7);
    int bi = craft_clampi((int)(craft_clampf(b, 0.0f, 1.0f) * 3.0f + 0.5f), 0, 3);
    return (uint8_t)((ri << 5) | (gi << 2) | bi);
}

static void craft_index_to_rgb(uint8_t idx, float *r, float *g, float *b) {
    *r = ((float)((idx >> 5) & 0x07)) / 7.0f;
    *g = ((float)((idx >> 2) & 0x07)) / 7.0f;
    *b = ((float)(idx & 0x03)) / 3.0f;
}

static int craft_inside_scissor(int x, int y) {
    if (!g_enable_scissor) {
        return 1;
    }
    return x >= g_scissor_x &&
           y >= g_scissor_y &&
           x < g_scissor_x + g_scissor_w &&
           y < g_scissor_y + g_scissor_h;
}

static void craft_plot(int x, int y, float depth, float r, float g, float b, float a) {
    int index;
    float dr;
    float dg;
    float db;

    if (!g_framebuffer || !g_depthbuffer) {
        return;
    }
    if (x < 0 || y < 0 || x >= g_fb_width || y >= g_fb_height) {
        return;
    }
    if (!craft_inside_scissor(x, y)) {
        return;
    }
    index = y * g_fb_width + x;
    if (g_enable_depth) {
        if (depth > g_depthbuffer[index]) {
            return;
        }
        g_depthbuffer[index] = depth;
    }

    if (g_enable_logic_op) {
        g_framebuffer[index] ^= 0xFFu;
        return;
    }

    if (g_enable_blend && a < 0.999f) {
        craft_index_to_rgb(g_framebuffer[index], &dr, &dg, &db);
        r = r * a + dr * (1.0f - a);
        g = g * a + dg * (1.0f - a);
        b = b * a + db * (1.0f - a);
    }
    g_framebuffer[index] = craft_rgb_to_index(r, g, b);
}

static void craft_make_rgb332_palette(void) {
    uint8_t palette[256 * 3];
    for (int i = 0; i < 256; ++i) {
        palette[i * 3 + 0] = (uint8_t)((((i >> 5) & 0x07) * 255) / 7);
        palette[i * 3 + 1] = (uint8_t)((((i >> 2) & 0x07) * 255) / 7);
        palette[i * 3 + 2] = (uint8_t)(((i & 0x03) * 255) / 3);
    }
    (void)sys_gfx_set_palette(palette);
}

static void craft_gl_reset_state(void) {
    g_bound_buffer = 0u;
    g_current_program = 0u;
    g_active_texture = 0u;
    g_viewport_x = 0;
    g_viewport_y = 0;
    g_viewport_w = g_fb_width;
    g_viewport_h = g_fb_height;
    g_scissor_x = 0;
    g_scissor_y = 0;
    g_scissor_w = g_fb_width;
    g_scissor_h = g_fb_height;
    g_enable_depth = 0;
    g_enable_blend = 0;
    g_enable_cull = 0;
    g_enable_scissor = 0;
    g_enable_logic_op = 0;
    for (int i = 0; i < 8; ++i) {
        memset(&g_attribs[i], 0, sizeof(g_attribs[i]));
    }
}

void craft_gl_init_window(int width, int height) {
    sys_text(10, 50, 0, "craft_gl_init_window");
    size_t pixel_count;

    if (width <= 0 || height <= 0) {
        width = 800;
        height = 600;
    }

    if (width == g_fb_width && height == g_fb_height && g_framebuffer && g_depthbuffer) {
        if (!g_saved_palette_valid) {
            g_saved_palette_valid = (sys_gfx_get_palette(g_saved_palette) == 0);
        }
        craft_gl_reset_state();
        return;
    }

    free(g_framebuffer);
    free(g_depthbuffer);
    g_fb_width = width;
    g_fb_height = height;
    pixel_count = (size_t)width * (size_t)height;
    g_framebuffer = (uint8_t *)malloc(pixel_count);
    g_depthbuffer = (float *)malloc(sizeof(float) * pixel_count);
    if (g_framebuffer) {
        sys_text(10, 60, 0, "craft_gl_init_window: framebuffer allocated");
        memset(g_framebuffer, 0, pixel_count);
    }
    if (g_depthbuffer) {
        for (size_t i = 0; i < pixel_count; ++i) {
            g_depthbuffer[i] = 1.0e30f;
        }
    }
    if (!g_saved_palette_valid) {
        g_saved_palette_valid = (sys_gfx_get_palette(g_saved_palette) == 0);
    }
    craft_gl_reset_state();
}

void craft_gl_present(void) {
    if (!g_framebuffer || g_fb_width <= 0 || g_fb_height <= 0) {
        return;
    }
}

void craft_gl_blit_to(int x, int y) {
    sys_text(10, 70, 0, "craft_gl_blit_to");
    if (!g_framebuffer || g_fb_width <= 0 || g_fb_height <= 0) {
        return;
    }
    craft_make_rgb332_palette();
    sys_gfx_blit8(g_framebuffer, g_fb_width, g_fb_height, x, y, 1);
}

void craft_gl_shutdown_window(void) {
    if (g_saved_palette_valid) {
        (void)sys_gfx_set_palette(g_saved_palette);
    }
}

static struct craft_program *craft_program(GLuint id) {
    if (id == 0u || id >= CRAFT_MAX_PROGRAMS) {
        return NULL;
    }
    return &g_programs[id];
}

static struct craft_texture *craft_bound_texture(int unit) {
    GLuint id;
    if (unit < 0 || unit >= 4) {
        return NULL;
    }
    id = g_bound_textures[unit];
    if (id == 0u || id >= CRAFT_MAX_TEXTURES) {
        return NULL;
    }
    return &g_textures[id];
}

static uint32_t craft_texture_sample(struct craft_texture *tex, float u, float v) {
    int tx;
    int ty;

    if (!tex || !tex->pixels || tex->width <= 0 || tex->height <= 0) {
        return 0xFFFFFFFFu;
    }

    if (tex->wrap_s == GL_CLAMP_TO_EDGE) {
        u = craft_clampf(u, 0.0f, 0.9999f);
    } else {
        u = u - floorf(u);
    }
    if (tex->wrap_t == GL_CLAMP_TO_EDGE) {
        v = craft_clampf(v, 0.0f, 0.9999f);
    } else {
        v = v - floorf(v);
    }

    tx = craft_clampi((int)(u * (float)tex->width), 0, tex->width - 1);
    ty = craft_clampi((int)(v * (float)tex->height), 0, tex->height - 1);
    return tex->pixels[(ty * tex->width) + tx];
}

static int craft_transform_vertex(struct craft_program *program,
                                  const float *position,
                                  struct craft_vertex *out) {
    float x = position[0];
    float y = position[1];
    float z = position[2];
    float cx = program->matrix[0] * x + program->matrix[4] * y + program->matrix[8] * z + program->matrix[12];
    float cy = program->matrix[1] * x + program->matrix[5] * y + program->matrix[9] * z + program->matrix[13];
    float cz = program->matrix[2] * x + program->matrix[6] * y + program->matrix[10] * z + program->matrix[14];
    float cw = program->matrix[3] * x + program->matrix[7] * y + program->matrix[11] * z + program->matrix[15];
    float inv_w;
    float ndc_x;
    float ndc_y;
    float ndc_z;

    if (craft_absf(cw) < 0.00001f) {
        return 0;
    }
    inv_w = 1.0f / cw;
    ndc_x = cx * inv_w;
    ndc_y = cy * inv_w;
    ndc_z = cz * inv_w;
    out->sx = g_viewport_x + (ndc_x * 0.5f + 0.5f) * (float)g_viewport_w;
    out->sy = g_viewport_y + (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)g_viewport_h;
    out->sz = ndc_z * 0.5f + 0.5f;
    out->inv_w = inv_w;
    return 1;
}

static void craft_decode_rgba(uint32_t rgba, float *r, float *g, float *b, float *a) {
    *r = ((float)((rgba >> 24) & 0xFFu)) / 255.0f;
    *g = ((float)((rgba >> 16) & 0xFFu)) / 255.0f;
    *b = ((float)((rgba >> 8) & 0xFFu)) / 255.0f;
    *a = ((float)(rgba & 0xFFu)) / 255.0f;
}

static float craft_edge(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void craft_fill_gradient(float timer) {
    int width = g_fb_width;
    int height = g_fb_height;
    if (!g_framebuffer) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        float t = ((float)y) / (float)(height > 1 ? height - 1 : 1);
        float shift = 0.08f * sinf(timer * 6.28318f);
        float r = craft_clampf(0.18f + 0.25f * (1.0f - t) + shift * 0.2f, 0.0f, 1.0f);
        float g = craft_clampf(0.35f + 0.35f * (1.0f - t), 0.0f, 1.0f);
        float b = craft_clampf(0.65f + 0.30f * (1.0f - t), 0.0f, 1.0f);
        uint8_t color = craft_rgb_to_index(r, g, b);
        memset(g_framebuffer + (size_t)y * (size_t)width, color, (size_t)width);
    }
}

static void craft_draw_triangle(struct craft_program *program,
                                const struct craft_vertex *a,
                                const struct craft_vertex *b,
                                const struct craft_vertex *c,
                                int mode_kind) {
    sys_text(10, 80, 0, "craft_draw_triangle");
    float area = craft_edge(a->sx, a->sy, b->sx, b->sy, c->sx, c->sy);
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    struct craft_texture *tex = NULL;
    int sampler = 0;

    if (craft_absf(area) < 0.0001f) {
        return;
    }
    if (g_enable_cull && area > 0.0f && mode_kind != CRAFT_PROGRAM_TEXT) {
        return;
    }

    min_x = (int)floorf(craft_minf(a->sx, craft_minf(b->sx, c->sx)));
    min_y = (int)floorf(craft_minf(a->sy, craft_minf(b->sy, c->sy)));
    max_x = (int)ceilf(craft_maxf(a->sx, craft_maxf(b->sx, c->sx)));
    max_y = (int)ceilf(craft_maxf(a->sy, craft_maxf(b->sy, c->sy)));
    min_x = craft_clampi(min_x, 0, g_fb_width - 1);
    min_y = craft_clampi(min_y, 0, g_fb_height - 1);
    max_x = craft_clampi(max_x, 0, g_fb_width - 1);
    max_y = craft_clampi(max_y, 0, g_fb_height - 1);

    sampler = program ? program->sampler : 0;
    tex = craft_bound_texture(sampler);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = craft_edge(b->sx, b->sy, c->sx, c->sy, px, py) / area;
            float w1 = craft_edge(c->sx, c->sy, a->sx, a->sy, px, py) / area;
            float w2 = craft_edge(a->sx, a->sy, b->sx, b->sy, px, py) / area;
            float inv_w;
            float u;
            float v;
            float depth;
            uint32_t rgba;
            float r;
            float g;
            float bcol;
            float acol;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                continue;
            }

            inv_w = w0 * a->inv_w + w1 * b->inv_w + w2 * c->inv_w;
            if (craft_absf(inv_w) < 0.00001f) {
                continue;
            }
            u = (w0 * a->u * a->inv_w + w1 * b->u * b->inv_w + w2 * c->u * c->inv_w) / inv_w;
            v = (w0 * a->v * a->inv_w + w1 * b->v * b->inv_w + w2 * c->v * c->inv_w) / inv_w;
            depth = w0 * a->sz + w1 * b->sz + w2 * c->sz;

            if (mode_kind == CRAFT_PROGRAM_SKY) {
                continue;
            }

            rgba = craft_texture_sample(tex, u, v);
            craft_decode_rgba(rgba, &r, &g, &bcol, &acol);

            if (mode_kind == CRAFT_PROGRAM_TEXT) {
                if (program->extra_i[0]) {
                    if (r > 0.95f && g > 0.95f && bcol > 0.95f) {
                        continue;
                    }
                } else {
                    acol = craft_maxf(acol, 0.4f);
                }
            } else {
                float ao = (w0 * a->ao * a->inv_w + w1 * b->ao * b->inv_w + w2 * c->ao * c->inv_w) / inv_w;
                float light = (w0 * a->light * a->inv_w + w1 * b->light * b->inv_w + w2 * c->light * c->inv_w) / inv_w;
                float nx = (w0 * a->nx * a->inv_w + w1 * b->nx * b->inv_w + w2 * c->nx * c->inv_w) / inv_w;
                float ny = (w0 * a->ny * a->inv_w + w1 * b->ny * b->inv_w + w2 * c->ny * c->inv_w) / inv_w;
                float nz = (w0 * a->nz * a->inv_w + w1 * b->nz * b->inv_w + w2 * c->nz * c->inv_w) / inv_w;
                float diffuse = craft_maxf(0.0f, (-nx + ny - nz) * 0.57735f);
                float daylight = program->extra_f[1];
                float brightness = craft_clampf((0.20f + daylight * 0.45f) + diffuse * 0.30f + light * 0.35f + (1.0f - ao) * 0.15f, 0.15f, 1.0f);

                if (r > 0.99f && g < 0.05f && bcol > 0.99f) {
                    continue;
                }
                r *= brightness;
                g *= brightness;
                bcol *= brightness;
            }
            craft_plot(x, y, depth, r, g, bcol, acol);
        }
    }
}

static void craft_draw_line(const struct craft_vertex *a, const struct craft_vertex *b) {
    int x0 = (int)a->sx;
    int y0 = (int)a->sy;
    int x1 = (int)b->sx;
    int y1 = (int)b->sy;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y0 < y1 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    float depth = a->sz < b->sz ? a->sz : b->sz;

    for (;;) {
        craft_plot(x0, y0, depth, 1.0f, 1.0f, 1.0f, 1.0f);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        {
            int e2 = err * 2;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

static int craft_fetch_vertex(GLint first,
                              GLsizei index,
                              struct craft_program *program,
                              struct craft_vertex *out) {
    const struct craft_attrib_state *pos_attr = &g_attribs[CRAFT_ATTRIB_POSITION];
    const struct craft_attrib_state *normal_attr = &g_attribs[CRAFT_ATTRIB_NORMAL];
    const struct craft_attrib_state *uv_attr = &g_attribs[CRAFT_ATTRIB_UV];
    const uint8_t *pos_ptr;
    const uint8_t *normal_ptr = NULL;
    const uint8_t *uv_ptr = NULL;
    const float *pf;

    if (!pos_attr->enabled || pos_attr->buffer == 0u) {
        return 0;
    }
    pos_ptr = g_buffers[pos_attr->buffer].data + pos_attr->stride * (first + index) + (uintptr_t)pos_attr->pointer;
    pf = (const float *)pos_ptr;
    if (pos_attr->size >= 3) {
        if (!craft_transform_vertex(program, pf, out)) {
            return 0;
        }
    } else {
        float p3[3] = {pf[0], pf[1], 0.0f};
        if (!craft_transform_vertex(program, p3, out)) {
            return 0;
        }
    }

    out->u = 0.0f;
    out->v = 0.0f;
    out->ao = 0.0f;
    out->light = 0.0f;
    out->nx = 0.0f;
    out->ny = 0.0f;
    out->nz = 1.0f;

    if (normal_attr->enabled && normal_attr->buffer != 0u) {
        normal_ptr = g_buffers[normal_attr->buffer].data + normal_attr->stride * (first + index) + (uintptr_t)normal_attr->pointer;
        pf = (const float *)normal_ptr;
        out->nx = pf[0];
        out->ny = pf[1];
        out->nz = pf[2];
    }

    if (uv_attr->enabled && uv_attr->buffer != 0u) {
        uv_ptr = g_buffers[uv_attr->buffer].data + uv_attr->stride * (first + index) + (uintptr_t)uv_attr->pointer;
        pf = (const float *)uv_ptr;
        out->u = pf[0];
        out->v = pf[1];
        if (uv_attr->size >= 4) {
            out->ao = pf[2];
            out->light = pf[3];
        }
    }

    return 1;
}

int glewInit(void) { return GLEW_OK; }

void glActiveTexture(GLenum texture) {
    if (texture >= GL_TEXTURE0 && texture <= GL_TEXTURE3) {
        g_active_texture = (GLuint)(texture - GL_TEXTURE0);
    }
}

void glAttachShader(GLuint program, GLuint shader) {
    (void)shader;
    if (program > 0u && program < CRAFT_MAX_PROGRAMS && g_programs[program].kind == 0) {
        g_programs[program].kind = (int)program;
    }
}

void glBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) {
        g_bound_buffer = buffer;
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    (void)target;
    if (g_active_texture < 4u) {
        g_bound_textures[g_active_texture] = texture;
    }
}

void glBlendFunc(GLenum sfactor, GLenum dfactor) {
    (void)sfactor;
    (void)dfactor;
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    struct craft_buffer *buffer;
    (void)target;
    (void)usage;
    if (g_bound_buffer == 0u || g_bound_buffer >= CRAFT_MAX_BUFFERS || size <= 0) {
        return;
    }
    buffer = &g_buffers[g_bound_buffer];
    free(buffer->data);
    buffer->data = (uint8_t *)malloc((size_t)size);
    if (!buffer->data) {
        buffer->size = 0;
        return;
    }
    memcpy(buffer->data, data, (size_t)size);
    buffer->size = (int)size;
}

void glClear(GLbitfield mask) {
    uint8_t color = craft_rgb_to_index(g_clear_r, g_clear_g, g_clear_b);
    if (!g_framebuffer || !g_depthbuffer) {
        return;
    }
    if (mask & GL_COLOR_BUFFER_BIT) {
        if (g_enable_scissor) {
            int x0 = craft_clampi(g_scissor_x, 0, g_fb_width);
            int y0 = craft_clampi(g_scissor_y, 0, g_fb_height);
            int x1 = craft_clampi(g_scissor_x + g_scissor_w, 0, g_fb_width);
            int y1 = craft_clampi(g_scissor_y + g_scissor_h, 0, g_fb_height);
            for (int y = y0; y < y1; ++y) {
                memset(g_framebuffer + (size_t)y * (size_t)g_fb_width + (size_t)x0,
                       color,
                       (size_t)(x1 - x0));
            }
        } else {
            memset(g_framebuffer, color, (size_t)g_fb_width * (size_t)g_fb_height);
        }
    }
    if (mask & GL_DEPTH_BUFFER_BIT) {
        if (g_enable_scissor) {
            int x0 = craft_clampi(g_scissor_x, 0, g_fb_width);
            int y0 = craft_clampi(g_scissor_y, 0, g_fb_height);
            int x1 = craft_clampi(g_scissor_x + g_scissor_w, 0, g_fb_width);
            int y1 = craft_clampi(g_scissor_y + g_scissor_h, 0, g_fb_height);
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    g_depthbuffer[y * g_fb_width + x] = 1.0e30f;
                }
            }
        } else {
            for (int i = 0; i < g_fb_width * g_fb_height; ++i) {
                g_depthbuffer[i] = 1.0e30f;
            }
        }
    }
}

void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    (void)alpha;
    g_clear_r = red;
    g_clear_g = green;
    g_clear_b = blue;
}

void glCompileShader(GLuint shader) { (void)shader; }

GLuint glCreateProgram(void) {
    return g_next_program_id < CRAFT_MAX_PROGRAMS ? g_next_program_id++ : 0u;
}

GLuint glCreateShader(GLenum type) {
    (void)type;
    return g_next_shader_id++;
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    for (GLsizei i = 0; i < n; ++i) {
        GLuint id = buffers[i];
        if (id > 0u && id < CRAFT_MAX_BUFFERS) {
            free(g_buffers[id].data);
            g_buffers[id].data = NULL;
            g_buffers[id].size = 0;
        }
    }
}

void glDeleteShader(GLuint shader) { (void)shader; }
void glDetachShader(GLuint program, GLuint shader) { (void)program; (void)shader; }

void glDisable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) g_enable_depth = 0;
    else if (cap == GL_BLEND) g_enable_blend = 0;
    else if (cap == GL_CULL_FACE) g_enable_cull = 0;
    else if (cap == GL_SCISSOR_TEST) g_enable_scissor = 0;
    else if (cap == GL_COLOR_LOGIC_OP) g_enable_logic_op = 0;
}

void glDisableVertexAttribArray(GLuint index) {
    if (index < 8u) {
        g_attribs[index].enabled = 0;
    }
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    struct craft_program *program = craft_program(g_current_program);

    if (!program || count <= 0) {
        return;
    }

    if (program->kind == CRAFT_PROGRAM_SKY) {
        craft_fill_gradient(program->timer);
        return;
    }

    if (mode == GL_TRIANGLES) {
        struct craft_vertex verts[3];
        for (GLsizei i = 0; i + 2 < count; i += 3) {
            if (!craft_fetch_vertex(first, i + 0, program, &verts[0])) continue;
            if (!craft_fetch_vertex(first, i + 1, program, &verts[1])) continue;
            if (!craft_fetch_vertex(first, i + 2, program, &verts[2])) continue;
            craft_draw_triangle(program, &verts[0], &verts[1], &verts[2], program->kind);
        }
    } else if (mode == GL_LINES) {
        for (GLsizei i = 0; i + 1 < count; i += 2) {
            struct craft_vertex a;
            struct craft_vertex b;
            if (!craft_fetch_vertex(first, i + 0, program, &a)) continue;
            if (!craft_fetch_vertex(first, i + 1, program, &b)) continue;
            craft_draw_line(&a, &b);
        }
    }
}

void glEnable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) g_enable_depth = 1;
    else if (cap == GL_BLEND) g_enable_blend = 1;
    else if (cap == GL_CULL_FACE) g_enable_cull = 1;
    else if (cap == GL_SCISSOR_TEST) g_enable_scissor = 1;
    else if (cap == GL_COLOR_LOGIC_OP) g_enable_logic_op = 1;
}

void glEnableVertexAttribArray(GLuint index) {
    if (index < 8u) {
        g_attribs[index].enabled = 1;
    }
}

void glGenBuffers(GLsizei n, GLuint *buffers) {
    for (GLsizei i = 0; i < n; ++i) {
        buffers[i] = g_next_buffer_id < CRAFT_MAX_BUFFERS ? g_next_buffer_id++ : 0u;
    }
}

void glGenTextures(GLsizei n, GLuint *textures) {
    for (GLsizei i = 0; i < n; ++i) {
        textures[i] = g_next_texture_id < CRAFT_MAX_TEXTURES ? g_next_texture_id++ : 0u;
    }
}

GLint glGetAttribLocation(GLuint program, const GLchar *name) {
    (void)program;
    if (!strcmp(name, "position")) return CRAFT_ATTRIB_POSITION;
    if (!strcmp(name, "normal")) return CRAFT_ATTRIB_NORMAL;
    if (!strcmp(name, "uv")) return CRAFT_ATTRIB_UV;
    return 0;
}

void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {
    (void)program;
    if (length) *length = 0;
    if (maxLength > 0 && infoLog) infoLog[0] = '\0';
}

void glGetProgramiv(GLuint program, GLenum pname, GLint *params) {
    (void)program;
    (void)pname;
    if (params) *params = 1;
}

void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog) {
    (void)shader;
    if (length) *length = 0;
    if (maxLength > 0 && infoLog) infoLog[0] = '\0';
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint *params) {
    (void)shader;
    (void)pname;
    if (params) *params = 1;
}

GLint glGetUniformLocation(GLuint program, const GLchar *name) {
    (void)program;
    if (!strcmp(name, "matrix")) return CRAFT_UNIFORM_MATRIX;
    if (!strcmp(name, "sampler")) return CRAFT_UNIFORM_SAMPLER;
    if (!strcmp(name, "camera")) return CRAFT_UNIFORM_CAMERA;
    if (!strcmp(name, "timer")) return CRAFT_UNIFORM_TIMER;
    if (!strcmp(name, "sky_sampler")) return CRAFT_UNIFORM_EXTRA1;
    if (!strcmp(name, "daylight")) return CRAFT_UNIFORM_EXTRA2;
    if (!strcmp(name, "fog_distance")) return CRAFT_UNIFORM_EXTRA3;
    if (!strcmp(name, "ortho")) return CRAFT_UNIFORM_EXTRA4;
    if (!strcmp(name, "is_sign")) return CRAFT_UNIFORM_EXTRA1;
    return 0;
}

void glLineWidth(GLfloat width) { (void)width; }
void glLinkProgram(GLuint program) { (void)program; }
void glLogicOp(GLenum opcode) { (void)opcode; }
void glPolygonOffset(GLfloat factor, GLfloat units) { (void)factor; (void)units; }

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    g_scissor_x = x;
    g_scissor_y = y;
    g_scissor_w = width;
    g_scissor_h = height;
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) {
    (void)shader;
    (void)count;
    (void)string;
    (void)length;
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const void *pixels) {
    struct craft_texture *tex;
    size_t count;
    const uint8_t *src = (const uint8_t *)pixels;
    (void)target;
    (void)level;
    (void)internalformat;
    (void)border;
    (void)format;
    (void)type;
    if (g_active_texture >= 4u) {
        return;
    }
    if (g_bound_textures[g_active_texture] == 0u || g_bound_textures[g_active_texture] >= CRAFT_MAX_TEXTURES) {
        return;
    }
    tex = &g_textures[g_bound_textures[g_active_texture]];
    free(tex->pixels);
    tex->pixels = NULL;
    tex->width = width;
    tex->height = height;
    count = (size_t)width * (size_t)height;
    tex->pixels = (uint32_t *)malloc(sizeof(uint32_t) * count);
    if (!tex->pixels) {
        tex->width = 0;
        tex->height = 0;
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t r = src ? src[i * 4 + 0] : 255u;
        uint32_t g = src ? src[i * 4 + 1] : 255u;
        uint32_t b = src ? src[i * 4 + 2] : 255u;
        uint32_t a = src ? src[i * 4 + 3] : 255u;
        tex->pixels[i] = (r << 24) | (g << 16) | (b << 8) | a;
    }
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    struct craft_texture *tex;
    (void)target;
    if (g_active_texture >= 4u) {
        return;
    }
    if (g_bound_textures[g_active_texture] == 0u || g_bound_textures[g_active_texture] >= CRAFT_MAX_TEXTURES) {
        return;
    }
    tex = &g_textures[g_bound_textures[g_active_texture]];
    if (pname == GL_TEXTURE_MIN_FILTER) tex->min_filter = param;
    else if (pname == GL_TEXTURE_MAG_FILTER) tex->mag_filter = param;
    else if (pname == GL_TEXTURE_WRAP_S) tex->wrap_s = param;
    else if (pname == GL_TEXTURE_WRAP_T) tex->wrap_t = param;
}

void glUniform1f(GLint location, GLfloat v0) {
    struct craft_program *program = craft_program(g_current_program);
    if (!program) return;
    if (location == CRAFT_UNIFORM_TIMER) program->timer = v0;
    else if (location >= CRAFT_UNIFORM_EXTRA1 && location <= CRAFT_UNIFORM_EXTRA4) {
        program->extra_f[location - CRAFT_UNIFORM_EXTRA1] = v0;
    }
}

void glUniform1i(GLint location, GLint v0) {
    struct craft_program *program = craft_program(g_current_program);
    if (!program) return;
    if (location == CRAFT_UNIFORM_SAMPLER) program->sampler = v0;
    else if (location >= CRAFT_UNIFORM_EXTRA1 && location <= CRAFT_UNIFORM_EXTRA4) {
        program->extra_i[location - CRAFT_UNIFORM_EXTRA1] = v0;
    }
}

void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    struct craft_program *program = craft_program(g_current_program);
    if (!program) return;
    if (location == CRAFT_UNIFORM_CAMERA) {
        program->camera[0] = v0;
        program->camera[1] = v1;
        program->camera[2] = v2;
    }
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    struct craft_program *program = craft_program(g_current_program);
    (void)count;
    (void)transpose;
    if (!program || location != CRAFT_UNIFORM_MATRIX || !value) {
        return;
    }
    memcpy(program->matrix, value, sizeof(program->matrix));
}

void glUseProgram(GLuint program) {
    g_current_program = program;
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer) {
    (void)type;
    (void)normalized;
    if (index >= 8u) {
        return;
    }
    g_attribs[index].size = size;
    g_attribs[index].stride = stride ? stride : (int)(sizeof(float) * size);
    g_attribs[index].pointer = (const uint8_t *)pointer;
    g_attribs[index].buffer = g_bound_buffer;
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    g_viewport_x = x;
    g_viewport_y = y;
    g_viewport_w = width;
    g_viewport_h = height;
}
