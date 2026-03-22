#pragma once
/* Headless mock: SDL_video.h */
#include "SDL_stdinc.h"
struct SDL_Window;

#define SDL_WINDOW_FULLSCREEN    0x00000001
#define SDL_WINDOW_OPENGL        0x00000002
#define SDL_WINDOW_SHOWN         0x00000004
#define SDL_WINDOW_RESIZABLE     0x00000020
#define SDL_WINDOW_MAXIMIZED     0x00000080
#define SDL_WINDOWPOS_UNDEFINED  0x1FFF0000
#define SDL_WINDOWPOS_CENTERED   0x2FFF0000

static inline SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, unsigned) { return nullptr; }
static inline void SDL_DestroyWindow(SDL_Window*) {}
static inline void SDL_SetWindowTitle(SDL_Window*, const char*) {}
static inline void SDL_SetWindowSize(SDL_Window*, int, int) {}
static inline void SDL_GetWindowSize(SDL_Window*, int* w, int* h) { if(w)*w=800; if(h)*h=600; }
static inline void SDL_SetWindowMinimumSize(SDL_Window*, int, int) {}
static inline int  SDL_GetWindowDisplayIndex(SDL_Window*) { return 0; }
static inline unsigned SDL_GetWindowFlags(SDL_Window*) { return 0; }
static inline void SDL_SetWindowIcon(SDL_Window*, struct SDL_Surface*) {}
static inline const char* SDL_GetCurrentVideoDriver() { return "headless"; }

typedef struct SDL_version { uint8_t major, minor, patch; } SDL_version;
static inline void SDL_GetVersion(SDL_version* ver) { if(ver) { ver->major=2; ver->minor=0; ver->patch=0; } }
