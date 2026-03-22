#pragma once
/* Headless mock: SDL_render.h — opaque types only; no real renderer exists */
#include "SDL_rect.h"
#include "SDL_pixels.h"
struct SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture  SDL_Texture;
typedef int SDL_BlendMode;

typedef struct SDL_Vertex {
    SDL_FPoint position;
    SDL_Color  color;
    SDL_FPoint tex_coord;
} SDL_Vertex;
#define SDL_BLENDMODE_NONE  0
#define SDL_BLENDMODE_BLEND 1
#define SDL_BLENDMODE_ADD   2
#define SDL_BLENDMODE_MOD   4

typedef enum {
    SDL_TEXTUREACCESS_STATIC    = 0,
    SDL_TEXTUREACCESS_STREAMING = 1,
    SDL_TEXTUREACCESS_TARGET    = 2
} SDL_TextureAccess;

static inline int SDL_SetTextureBlendMode(SDL_Texture*, SDL_BlendMode) { return 0; }
static inline int SDL_SetTextureAlphaMod(SDL_Texture*, unsigned char) { return 0; }
static inline int SDL_SetTextureColorMod(SDL_Texture*, unsigned char, unsigned char, unsigned char) { return 0; }
static inline int SDL_SetRenderDrawBlendMode(SDL_Renderer*, SDL_BlendMode) { return 0; }
static inline int SDL_SetRenderDrawColor(SDL_Renderer*, unsigned char, unsigned char, unsigned char, unsigned char) { return 0; }
static inline int SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*) { return 0; }
static inline int SDL_RenderCopyEx(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*, double, const SDL_Point*, int) { return 0; }
static inline int SDL_RenderFillRect(SDL_Renderer*, const SDL_Rect*) { return 0; }
static inline int SDL_RenderDrawRect(SDL_Renderer*, const SDL_Rect*) { return 0; }
static inline int SDL_RenderDrawLine(SDL_Renderer*, int, int, int, int) { return 0; }
static inline int SDL_RenderSetClipRect(SDL_Renderer*, const SDL_Rect*) { return 0; }
static inline void SDL_RenderGetClipRect(SDL_Renderer*, SDL_Rect* r) { if(r) { r->x=r->y=r->w=r->h=0; } }
static inline SDL_Texture* SDL_CreateTexture(SDL_Renderer*, unsigned int, int, int, int) { return nullptr; }
static inline void SDL_DestroyTexture(SDL_Texture*) {}
static inline int SDL_QueryTexture(SDL_Texture*, unsigned int*, int*, int*, int*) { return -1; }
static inline SDL_Texture* SDL_GetRenderTarget(SDL_Renderer*) { return nullptr; }
static inline int SDL_SetRenderTarget(SDL_Renderer*, SDL_Texture*) { return 0; }
static inline void SDL_RenderPresent(SDL_Renderer*) {}
static inline int SDL_RenderReadPixels(SDL_Renderer*, const SDL_Rect*, unsigned int, void*, int) { return -1; }
