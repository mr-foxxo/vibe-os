/*
noise.h and noise.c are derived from:
https://github.com/caseman/noise
*/

#ifndef VIBE_CRAFT_NOISE_H
#define VIBE_CRAFT_NOISE_H

void seed(unsigned int x);

float simplex2(
    float x, float y,
    int octaves, float persistence, float lacunarity);

float simplex3(
    float x, float y, float z,
    int octaves, float persistence, float lacunarity);

#endif
