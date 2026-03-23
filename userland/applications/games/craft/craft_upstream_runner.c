#include <stdlib.h>
#include <string.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>

int doom_printf(const char *fmt, ...);
int doom_snprintf(char *str, size_t size, const char *fmt, ...);
int doom_sscanf(const char *str, const char *fmt, ...);

float floorf(float x);
float roundf(float x);
float powf(float x, float y);
float cosf(float x);
float sinf(float x);
float atan2f(float y, float x);
float sqrtf(float x);
void craft_glfw_inject_key(int raw);
void craft_glfw_set_mouse_state(int x, int y, int dx, int dy,
                                uint8_t buttons, int focused, int inside);
void craft_glfw_set_window_size(int width, int height);
void craft_glfw_request_close(void);
void craft_glfw_reset_embedded(void);
void craft_gl_blit_to(int x, int y);

static unsigned int g_craft_runner_rng_state = 1u;

static void craft_debug_stage(const char *message) {
    sys_write_debug(message);
    sys_write_debug("\n");
}

static void *craft_runner_calloc(size_t count, size_t size) {
    size_t total = count * size;
    unsigned char *ptr;
    if (count != 0u && total / count != size) {
        return 0;
    }
    ptr = (unsigned char *)malloc(total);
    if (!ptr) {
        return 0;
    }
    memset(ptr, 0, total);
    return ptr;
}

static char *craft_runner_strncat(char *dest, const char *src, size_t n) {
    size_t dest_len = strlen(dest);
    size_t i = 0u;
    while (i < n && src[i] != '\0') {
        dest[dest_len + i] = src[i];
        i += 1u;
    }
    dest[dest_len + i] = '\0';
    return dest;
}

static int craft_runner_atoi(const char *nptr) {
    int sign = 1;
    int value = 0;
    if (!nptr) {
        return 0;
    }
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') {
        nptr++;
    }
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }
    while (*nptr >= '0' && *nptr <= '9') {
        value = value * 10 + (*nptr - '0');
        nptr++;
    }
    return value * sign;
}

static void craft_runner_srand(unsigned int seed) {
    g_craft_runner_rng_state = seed ? seed : 1u;
}

static int craft_runner_rand(void) {
    g_craft_runner_rng_state = g_craft_runner_rng_state * 1103515245u + 12345u;
    return (int)((g_craft_runner_rng_state >> 16) & 0x7fffu);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#define printf doom_printf
#define snprintf doom_snprintf
#define sscanf doom_sscanf
#define calloc craft_runner_calloc
#define strncat craft_runner_strncat
#define atoi craft_runner_atoi
#define srand craft_runner_srand
#define rand craft_runner_rand
#define main craft_upstream_main
#include "upstream/src/main.c"
#undef main
#undef rand
#undef srand
#undef atoi
#undef strncat
#undef calloc
#undef sscanf
#undef snprintf
#undef printf

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct craft_runtime_state {
    int bootstrapped;
    int session_active;
    GLuint sky_buffer;
    FPS fps;
    double previous;
    double last_commit;
    double last_update;
};

static struct craft_runtime_state g_runtime;
static Attrib g_block_attrib;
static Attrib g_line_attrib;
static Attrib g_text_attrib;
static Attrib g_sky_attrib;
// static Attrib block_attrib;
// static Attrib line_attrib;
// static Attrib text_attrib;
// static Attrib sky_attrib;

void craft_upstream_resize(int width, int height);

static int craft_upstream_bootstrap(void) {
    GLuint texture;
    GLuint font;
    GLuint sky;
    GLuint sign;
    GLuint program;

    if (g_runtime.bootstrapped) {
        return 0;
    }

    craft_debug_stage("craft: bootstrap begin");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    craft_runner_srand(time(NULL));
    craft_runner_rand();

    if (!glfwInit()) {
        return -1;
    }
    craft_debug_stage("craft: after glfwInit");
    create_window();
    if (!g->window) {
        glfwTerminate();
        return -1;
    }
    craft_debug_stage("craft: after create_window");

    glfwMakeContextCurrent(g->window);
    glfwSwapInterval(VSYNC);
    glfwSetInputMode(g->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetKeyCallback(g->window, on_key);
    glfwSetCharCallback(g->window, on_char);
    glfwSetMouseButtonCallback(g->window, on_mouse_button);
    glfwSetScrollCallback(g->window, on_scroll);

    if (glewInit() != GLEW_OK) {
        glfwTerminate();
        return -1;
    }
    craft_debug_stage("craft: after glewInit");

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glLogicOp(GL_INVERT);
    glClearColor(0, 0, 0, 1);

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    load_png_texture("textures/texture.png");

    glGenTextures(1, &font);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, font);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    load_png_texture("textures/font.png");

    glGenTextures(1, &sky);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, sky);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    load_png_texture("textures/sky.png");

    glGenTextures(1, &sign);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, sign);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    load_png_texture("textures/sign.png");
    craft_debug_stage("craft: after textures");

    program = load_program(
        "shaders/block_vertex.glsl", "shaders/block_fragment.glsl");
    g_block_attrib.program = program;
    g_block_attrib.position = glGetAttribLocation(program, "position");
    g_block_attrib.normal = glGetAttribLocation(program, "normal");
    g_block_attrib.uv = glGetAttribLocation(program, "uv");
    g_block_attrib.matrix = glGetUniformLocation(program, "matrix");
    g_block_attrib.sampler = glGetUniformLocation(program, "sampler");
    g_block_attrib.extra1 = glGetUniformLocation(program, "sky_sampler");
    g_block_attrib.extra2 = glGetUniformLocation(program, "daylight");
    g_block_attrib.extra3 = glGetUniformLocation(program, "fog_distance");
    g_block_attrib.extra4 = glGetUniformLocation(program, "ortho");
    g_block_attrib.camera = glGetUniformLocation(program, "camera");
    g_block_attrib.timer = glGetUniformLocation(program, "timer");

    program = load_program(
        "shaders/line_vertex.glsl", "shaders/line_fragment.glsl");
    g_line_attrib.program = program;
    g_line_attrib.position = glGetAttribLocation(program, "position");
    g_line_attrib.matrix = glGetUniformLocation(program, "matrix");

    program = load_program(
        "shaders/text_vertex.glsl", "shaders/text_fragment.glsl");
    g_text_attrib.program = program;
    g_text_attrib.position = glGetAttribLocation(program, "position");
    g_text_attrib.uv = glGetAttribLocation(program, "uv");
    g_text_attrib.matrix = glGetUniformLocation(program, "matrix");
    g_text_attrib.sampler = glGetUniformLocation(program, "sampler");
    g_text_attrib.extra1 = glGetUniformLocation(program, "is_sign");

    program = load_program(
        "shaders/sky_vertex.glsl", "shaders/sky_fragment.glsl");
    g_sky_attrib.program = program;
    g_sky_attrib.position = glGetAttribLocation(program, "position");
    g_sky_attrib.normal = glGetAttribLocation(program, "normal");
    g_sky_attrib.uv = glGetAttribLocation(program, "uv");
    g_sky_attrib.matrix = glGetUniformLocation(program, "matrix");
    g_sky_attrib.sampler = glGetUniformLocation(program, "sampler");
    g_sky_attrib.timer = glGetUniformLocation(program, "timer");
    craft_debug_stage("craft: after shaders");

    g->mode = MODE_OFFLINE;
    doom_snprintf(g->db_path, MAX_PATH_LENGTH, "%s", DB_PATH);
    g->create_radius = 1;
    g->render_radius = 1;
    g->delete_radius = 3;
    g->sign_radius = 1;

    craft_debug_stage("craft: before workers");
    for (int i = 0; i < WORKERS; ++i) {
        Worker *worker = g->workers + i;
        worker->index = i;
        worker->state = WORKER_IDLE;
        mtx_init(&worker->mtx, mtx_plain);
        cnd_init(&worker->cnd);
        worker->cnd.owner = worker;
        thrd_create(&worker->thrd, worker_run, worker);
    }
    craft_debug_stage("craft: after workers");

    g_runtime.bootstrapped = 1;
    return 0;
}

static int craft_upstream_begin_session(void) {
    Player *me;
    State *s;
    int loaded;
    int spawn_y;

    if (g_runtime.session_active) {
        return 0;
    }

    craft_debug_stage("craft: begin session");
    if (g->mode == MODE_OFFLINE || USE_CACHE) {
        db_enable();
        if (db_init(g->db_path)) {
            return -1;
        }
        if (g->mode == MODE_ONLINE) {
            db_delete_all_signs();
        }
    }

    if (g->mode == MODE_ONLINE) {
        client_enable();
        client_connect(g->server_addr, g->server_port);
        client_start();
        client_version(1);
        login();
    }

    reset_model();
    memset(&g_runtime.fps, 0, sizeof(g_runtime.fps));
    g_runtime.last_commit = glfwGetTime();
    g_runtime.last_update = glfwGetTime();
    g_runtime.sky_buffer = gen_sky_buffer();

    me = g->players;
    s = &g->players->state;
    me->id = 0;
    me->name[0] = '\0';
    me->buffer = 0;
    g->player_count = 1;

    s->x = 0.0f;
    s->y = 0.0f;
    s->z = 0.0f;
    s->rx = 0.0f;
    s->ry = 0.0f;
    loaded = db_load_state(&s->x, &s->y, &s->z, &s->rx, &s->ry);
    craft_debug_stage("craft: after db load");
    if (loaded) {
        if (s->x < -256.0f || s->x > 256.0f || s->z < -256.0f || s->z > 256.0f) {
            s->x = 0.0f;
            s->z = 0.0f;
        }
        if (s->rx < -6.28319f || s->rx > 6.28319f) {
            s->rx = 0.0f;
        }
        if (s->ry < -1.4f || s->ry > 1.4f) {
            s->ry = 0.0f;
        }
    }
    force_chunks(me);
    spawn_y = highest_block(s->x, s->z) + 3;
    if (!loaded) {
        s->x = 0.0f;
        s->z = 0.0f;
        s->rx = 0.0f;
        s->ry = -0.25f;
        s->y = (float)spawn_y;
    } else {
        if (s->y < 1.0f || s->y > 255.0f || player_intersects_block(2, s->x, s->y, s->z,
                (int)roundf(s->x), (int)roundf(s->y), (int)roundf(s->z))) {
            s->y = (float)spawn_y;
            s->ry = -0.25f;
        }
    }

    g_runtime.previous = glfwGetTime();
    g_runtime.session_active = 1;
    craft_debug_stage("craft: session ready");
    return 0;
}

static void craft_upstream_end_session(void) {
    State *s;

    if (!g_runtime.session_active) {
        return;
    }

    s = &g->players->state;
    db_save_state(s->x, s->y, s->z, s->rx, s->ry);
    db_close();
    db_disable();
    client_stop();
    client_disable();
    del_buffer(g_runtime.sky_buffer);
    g_runtime.sky_buffer = 0;
    delete_all_chunks();
    delete_all_players();
    g_runtime.session_active = 0;
}

int craft_upstream_start(int width, int height) {
    if (craft_upstream_bootstrap() != 0) {
        return -1;
    }
    craft_glfw_set_window_size(width, height);
    if (craft_upstream_begin_session() != 0) {
        craft_upstream_end_session();
        return -1;
    }
    return 0;
}

int craft_upstream_frame(void) {
    Player *me;
    Player *player;
    State *s;
    char text_buffer[1024];
    float ts;
    float tx;
    float ty;
    int face_count;

    if (!g_runtime.bootstrapped) {
        return -1;
    }
    if (!g_runtime.session_active && craft_upstream_begin_session() != 0) {
        return -1;
    }
    craft_debug_stage("craft: frame begin");

    me = g->players;
    s = &g->players->state;

    g->scale = get_scale_factor();
    glfwGetFramebufferSize(g->window, &g->width, &g->height);
    glViewport(0, 0, g->width, g->height);
    craft_debug_stage("craft: frame viewport");

    if (g->time_changed) {
        g->time_changed = 0;
        g_runtime.last_commit = glfwGetTime();
        g_runtime.last_update = glfwGetTime();
        memset(&g_runtime.fps, 0, sizeof(g_runtime.fps));
    }
    update_fps(&g_runtime.fps);
    {
        double now = glfwGetTime();
        double dt = now - g_runtime.previous;
        dt = MIN(dt, 0.2);
        dt = MAX(dt, 0.0);
        g_runtime.previous = now;

        handle_mouse_input();
        handle_movement(dt);

        {
            char *buffer = client_recv();
            if (buffer) {
                parse_buffer(buffer);
                free(buffer);
            }
        }

        if (now - g_runtime.last_commit > COMMIT_INTERVAL) {
            g_runtime.last_commit = now;
            db_commit();
        }

        if (now - g_runtime.last_update > 0.1) {
            g_runtime.last_update = now;
            client_position(s->x, s->y, s->z, s->rx, s->ry);
        }
    }
    craft_debug_stage("craft: frame simulation");

    g->observe1 = g->observe1 % g->player_count;
    g->observe2 = g->observe2 % g->player_count;
    delete_chunks();
    del_buffer(me->buffer);
    me->buffer = gen_player_buffer(s->x, s->y, s->z, s->rx, s->ry);
    for (int i = 1; i < g->player_count; ++i) {
        interpolate_player(g->players + i);
    }
    player = g->players + g->observe1;
    craft_debug_stage("craft: frame buffers");

    glClear(GL_COLOR_BUFFER_BIT);
    glClear(GL_DEPTH_BUFFER_BIT);
    render_sky(&g_sky_attrib, player, g_runtime.sky_buffer);
    glClear(GL_DEPTH_BUFFER_BIT);
    craft_debug_stage("craft: frame sky");
    face_count = render_chunks(&g_block_attrib, player);
    craft_debug_stage("craft: frame chunks");
    render_signs(&g_text_attrib, player);
    render_sign(&g_text_attrib, player);
    render_players(&g_block_attrib, player);
    if (SHOW_WIREFRAME) {
        render_wireframe(&g_line_attrib, player);
    }

    glClear(GL_DEPTH_BUFFER_BIT);
    if (SHOW_CROSSHAIRS) {
        render_crosshairs(&g_line_attrib);
    }
    if (SHOW_ITEM) {
        render_item(&g_block_attrib);
    }
    craft_debug_stage("craft: frame world done");

    ts = 12 * g->scale;
    tx = ts / 2;
    ty = g->height - ts;
    if (SHOW_INFO_TEXT) {
        int hour = time_of_day() * 24;
        char am_pm = hour < 12 ? 'a' : 'p';
        hour = hour % 12;
        hour = hour ? hour : 12;
        doom_snprintf(
            text_buffer, 1024,
            "(%d, %d) (%.2f, %.2f, %.2f) [%d, %d, %d] %d%cm %dfps",
            chunked(s->x), chunked(s->z), s->x, s->y, s->z,
            g->player_count, g->chunk_count,
            face_count * 2, hour, am_pm, g_runtime.fps.fps);
        render_text(&g_text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);
        ty -= ts * 2;
    }
    if (SHOW_CHAT_TEXT) {
        for (int i = 0; i < MAX_MESSAGES; ++i) {
            int index = (g->message_index + i) % MAX_MESSAGES;
            if (strlen(g->messages[index])) {
                render_text(&g_text_attrib, ALIGN_LEFT, tx, ty, ts,
                    g->messages[index]);
                ty -= ts * 2;
            }
        }
    }
    if (g->typing) {
        doom_snprintf(text_buffer, 1024, "> %s", g->typing_buffer);
        render_text(&g_text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);
        ty -= ts * 2;
    }
    if (SHOW_PLAYER_NAMES) {
        if (player != me) {
            render_text(&g_text_attrib, ALIGN_CENTER,
                g->width / 2, ts, ts, player->name);
        }
        {
            Player *other = player_crosshair(player);
            if (other) {
                render_text(&g_text_attrib, ALIGN_CENTER,
                    g->width / 2, g->height / 2 - ts - 24, ts,
                    other->name);
            }
        }
    }

    if (g->observe2) {
        int pw = 256 * g->scale;
        int ph = 256 * g->scale;
        int offset = 32 * g->scale;
        int pad = 3 * g->scale;
        int sw = pw + pad * 2;
        int sh = ph + pad * 2;

        player = g->players + g->observe2;

        glEnable(GL_SCISSOR_TEST);
        glScissor(g->width - sw - offset + pad, offset - pad, sw, sh);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);
        glViewport(g->width - pw - offset, offset, pw, ph);

        g->width = pw;
        g->height = ph;
        g->ortho = 0;
        g->fov = 65;

        render_sky(&g_sky_attrib, player, g_runtime.sky_buffer);
        glClear(GL_DEPTH_BUFFER_BIT);
        render_chunks(&g_block_attrib, player);
        render_signs(&g_text_attrib, player);
        render_players(&g_block_attrib, player);
        glClear(GL_DEPTH_BUFFER_BIT);
        if (SHOW_PLAYER_NAMES) {
            render_text(&g_text_attrib, ALIGN_CENTER,
                pw / 2, ts, ts, player->name);
        }
    }

    glfwSwapBuffers(g->window);
    craft_debug_stage("craft: frame swap");
    glfwPollEvents();
    craft_debug_stage("craft: frame poll");
    if (glfwWindowShouldClose(g->window)) {
        craft_upstream_end_session();
        return 0;
    }
    if (g->mode_changed) {
        g->mode_changed = 0;
        craft_upstream_end_session();
        if (craft_upstream_begin_session() != 0) {
            return -1;
        }
    }
    return 1;
}

void craft_upstream_stop(void) {
    craft_upstream_end_session();
    if (g_runtime.bootstrapped) {
        craft_glfw_request_close();
        glfwTerminate();
        craft_glfw_reset_embedded();
        curl_global_cleanup();
        memset(&g_runtime, 0, sizeof(g_runtime));
    }
}

void craft_upstream_resize(int width, int height) {
    craft_glfw_set_window_size(width, height);
}

void craft_upstream_queue_key(int key) {
    craft_glfw_inject_key(key);
}

void craft_upstream_set_mouse(int x, int y, int dx, int dy,
                              uint8_t buttons, int focused, int inside) {
    craft_glfw_set_mouse_state(x, y, dx, dy, buttons, focused, inside);
}

void craft_upstream_blit(int x, int y) {
    craft_gl_blit_to(x, y);
}

void craft_upstream_request_close(void) {
    craft_glfw_request_close();
}
