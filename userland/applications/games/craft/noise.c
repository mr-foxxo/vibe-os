#include <string.h>
#include <userland/applications/games/craft/noise.h>

static unsigned int g_vibe_craft_noise_seed = 1u;

static unsigned int vibe_craft_rand(void) {
    g_vibe_craft_noise_seed = (g_vibe_craft_noise_seed * 1103515245u) + 12345u;
    return g_vibe_craft_noise_seed;
}

static void vibe_craft_srand(unsigned int x) {
    g_vibe_craft_noise_seed = x ? x : 1u;
}

static float vibe_craft_floorf(float value) {
    int ivalue = (int)value;
    if ((float)ivalue > value) {
        ivalue -= 1;
    }
    return (float)ivalue;
}

#define srand vibe_craft_srand
#define rand() ((int)(vibe_craft_rand() & 0x7fffffffu))
#define RAND_MAX 0x7fffffff
#define floorf vibe_craft_floorf

#include "upstream/deps/noise/noise.c"
