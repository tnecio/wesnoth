#pragma once
/* Headless mock: SDL_surface.h */
#include "SDL_pixels.h"
#include "SDL_rect.h"
#include <cstdint>

typedef struct SDL_PixelFormat { uint32_t format; int BitsPerPixel; int BytesPerPixel; } SDL_PixelFormat;
typedef struct SDL_Surface {
    uint32_t flags;
    SDL_PixelFormat* format;
    int w, h, pitch;
    void* pixels;
    void* userdata;
    int locked;
    void* lock_data;
    SDL_Rect clip_rect;
    void* map;
    int refcount;
} SDL_Surface;

static inline void SDL_FreeSurface(SDL_Surface*) {}
static inline SDL_Surface* SDL_CreateRGBSurface(uint32_t, int, int, int, uint32_t, uint32_t, uint32_t, uint32_t) { return nullptr; }
static inline SDL_Surface* SDL_CreateRGBSurfaceFrom(void*, int, int, int, int, uint32_t, uint32_t, uint32_t, uint32_t) { return nullptr; }
static inline SDL_Surface* SDL_ConvertSurface(SDL_Surface*, const SDL_PixelFormat*, uint32_t) { return nullptr; }
static inline int SDL_BlitSurface(SDL_Surface*, const SDL_Rect*, SDL_Surface*, SDL_Rect*) { return 0; }
static inline int SDL_FillRect(SDL_Surface*, const SDL_Rect*, uint32_t) { return 0; }
static inline void SDL_GetClipRect(SDL_Surface* s, SDL_Rect* r) { if(s && r) *r = s->clip_rect; }
static inline SDL_bool SDL_SetClipRect(SDL_Surface* s, const SDL_Rect* r) {
    if(s) { s->clip_rect = r ? *r : SDL_Rect{0,0,s->w,s->h}; } return SDL_TRUE;
}
static inline int SDL_SetSurfaceAlphaMod(SDL_Surface*, uint8_t) { return 0; }
static inline int SDL_SetSurfaceBlendMode(SDL_Surface*, int) { return 0; }
static inline int SDL_GetSurfaceAlphaMod(SDL_Surface*, uint8_t* a) { if(a) *a = 255; return 0; }
static inline int SDL_LockSurface(SDL_Surface*) { return 0; }
static inline void SDL_UnlockSurface(SDL_Surface*) {}
static inline uint32_t SDL_MapRGB(const SDL_PixelFormat*, uint8_t, uint8_t, uint8_t) { return 0; }
static inline uint32_t SDL_MapRGBA(const SDL_PixelFormat*, uint8_t, uint8_t, uint8_t, uint8_t) { return 0; }
static inline void SDL_GetRGB(uint32_t, const SDL_PixelFormat*, uint8_t* r, uint8_t* g, uint8_t* b) {
    if(r)*r=0; if(g)*g=0; if(b)*b=0;
}
static inline void SDL_GetRGBA(uint32_t, const SDL_PixelFormat*, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    if(r)*r=0; if(g)*g=0; if(b)*b=0; if(a)*a=255;
}
static inline SDL_Surface* SDL_LoadBMP_RW(struct SDL_RWops*, int) { return nullptr; }
