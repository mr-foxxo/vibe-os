#include <string.h>
#include <stdlib.h>
#include <userland/applications/games/craft/upstream/src/client.h>

static int g_client_enabled = 0;

void client_enable() { g_client_enabled = 1; }
void client_disable() { g_client_enabled = 0; }
int get_client_enabled() { return g_client_enabled; }
void client_connect(char *hostname, int port) { (void)hostname; (void)port; }
void client_start() {}
void client_stop() {}
void client_send(char *data) { (void)data; }
char *client_recv() { return 0; }
void client_version(int version) { (void)version; }
void client_login(const char *username, const char *identity_token) { (void)username; (void)identity_token; }
void client_position(float x, float y, float z, float rx, float ry) { (void)x; (void)y; (void)z; (void)rx; (void)ry; }
void client_chunk(int p, int q, int key) { (void)p; (void)q; (void)key; }
void client_block(int x, int y, int z, int w) { (void)x; (void)y; (void)z; (void)w; }
void client_light(int x, int y, int z, int w) { (void)x; (void)y; (void)z; (void)w; }
void client_sign(int x, int y, int z, int face, const char *text) { (void)x; (void)y; (void)z; (void)face; (void)text; }
void client_talk(const char *text) { (void)text; }
