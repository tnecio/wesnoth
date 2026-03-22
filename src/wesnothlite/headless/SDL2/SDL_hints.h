#pragma once
/* Headless mock: SDL_hints.h */
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_RENDER_DRIVER        "SDL_RENDER_DRIVER"
#define SDL_HINT_FRAMEBUFFER_ACCELERATION "SDL_FRAMEBUFFER_ACCELERATION"
static inline int SDL_SetHint(const char*, const char*) { return 1; }
