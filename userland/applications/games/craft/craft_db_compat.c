#include <string.h>
#include <userland/applications/games/craft/upstream/src/db.h>

static int g_db_enabled = 0;
static int g_state_valid = 0;
static float g_x;
static float g_y;
static float g_z;
static float g_rx;
static float g_ry;

void db_enable() { g_db_enabled = 1; }
void db_disable() { g_db_enabled = 0; }
int get_db_enabled() { return g_db_enabled; }
int db_init(char *path) { (void)path; return 1; }
void db_close() {}
void db_commit() {}
void db_auth_set(char *username, char *identity_token) { (void)username; (void)identity_token; }
int db_auth_select(char *username) { (void)username; return 0; }
void db_auth_select_none() {}
int db_auth_get(char *username, char *identity_token, int identity_token_length) {
    (void)username; (void)identity_token; (void)identity_token_length;
    return 0;
}
int db_auth_get_selected(char *username, int username_length, char *identity_token, int identity_token_length) {
    (void)username; (void)username_length; (void)identity_token; (void)identity_token_length;
    return 0;
}
void db_save_state(float x, float y, float z, float rx, float ry) {
    g_x = x; g_y = y; g_z = z; g_rx = rx; g_ry = ry; g_state_valid = 1;
}
int db_load_state(float *x, float *y, float *z, float *rx, float *ry) {
    if (!g_state_valid) {
        return 0;
    }
    if (x) *x = g_x;
    if (y) *y = g_y;
    if (z) *z = g_z;
    if (rx) *rx = g_rx;
    if (ry) *ry = g_ry;
    return 1;
}
void db_insert_block(int p, int q, int x, int y, int z, int w) { (void)p; (void)q; (void)x; (void)y; (void)z; (void)w; }
void db_insert_light(int p, int q, int x, int y, int z, int w) { (void)p; (void)q; (void)x; (void)y; (void)z; (void)w; }
void db_insert_sign(int p, int q, int x, int y, int z, int face, const char *text) { (void)p; (void)q; (void)x; (void)y; (void)z; (void)face; (void)text; }
void db_delete_sign(int x, int y, int z, int face) { (void)x; (void)y; (void)z; (void)face; }
void db_delete_signs(int x, int y, int z) { (void)x; (void)y; (void)z; }
void db_delete_all_signs() {}
void db_load_blocks(Map *map, int p, int q) { (void)map; (void)p; (void)q; }
void db_load_lights(Map *map, int p, int q) { (void)map; (void)p; (void)q; }
void db_load_signs(SignList *list, int p, int q) { (void)list; (void)p; (void)q; }
int db_get_key(int p, int q) { (void)p; (void)q; return 0; }
void db_set_key(int p, int q, int key) { (void)p; (void)q; (void)key; }
void db_worker_start() {}
void db_worker_stop() {}
int db_worker_run(void *arg) { (void)arg; return 0; }
