#pragma once
/* Headless mock: SDL_pixels.h — only the types used by color.hpp */
#include <stdint.h>
typedef struct { uint8_t r, g, b, a; } SDL_Color;
#define SDL_ALPHA_OPAQUE      255
#define SDL_ALPHA_TRANSPARENT 0
