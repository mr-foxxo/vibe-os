#ifndef VIBE_CRAFT_WORLD_H
#define VIBE_CRAFT_WORLD_H

typedef void (*world_func)(int, int, int, int, void *);

void create_world(int p, int q, world_func func, void *arg);

#endif
