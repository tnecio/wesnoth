#pragma once
/* Headless mock: SDL.h — umbrella header */
#include "SDL_pixels.h"
#include "SDL_rect.h"
#include "SDL_render.h"
#include "SDL_surface.h"
#include "SDL_events.h"
#include "SDL_timer.h"
#include "SDL_mutex.h"
#include "SDL_video.h"
#include "SDL_keyboard.h"
#include "SDL_mouse.h"
#include "SDL_rwops.h"
#include "SDL_hints.h"
inline int  SDL_Init(uint32_t) { return 0; }
inline void SDL_Quit() {}
inline const char* SDL_GetError() { return ""; }
inline void SDL_ClearError() {}
