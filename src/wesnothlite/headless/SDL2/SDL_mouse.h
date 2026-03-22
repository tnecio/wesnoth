#pragma once
/* Headless mock: SDL_mouse.h */
#include "SDL_events.h"
typedef void* SDL_Cursor;

#define SDL_BUTTON(X)    (1 << ((X)-1))
#define SDL_BUTTON_LEFT    1
#define SDL_BUTTON_MIDDLE  2
#define SDL_BUTTON_RIGHT   3
#define SDL_BUTTON_X1      4
#define SDL_BUTTON_X2      5
#define SDL_BUTTON_LMASK   SDL_BUTTON(SDL_BUTTON_LEFT)
#define SDL_BUTTON_MMASK   SDL_BUTTON(SDL_BUTTON_MIDDLE)
#define SDL_BUTTON_RMASK   SDL_BUTTON(SDL_BUTTON_RIGHT)
#define SDL_BUTTON_X1MASK  SDL_BUTTON(SDL_BUTTON_X1)
#define SDL_BUTTON_X2MASK  SDL_BUTTON(SDL_BUTTON_X2)
/* SDL_GetMouseState is already defined in SDL_events.h */
static inline void SDL_SetCursor(SDL_Cursor*) {}
static inline SDL_Cursor* SDL_CreateSystemCursor(int) { return nullptr; }
static inline void SDL_FreeCursor(SDL_Cursor*) {}
static inline void SDL_ShowCursor(int) {}
static inline int SDL_ShowCursor_query() { return 1; }
