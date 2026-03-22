#pragma once
/* Headless mock: SDL_keycode.h — included by real SDL_keyboard.h to define SDLK_* values */
#include "SDL_scancode.h"
/* SDL_Keycode is defined in SDL_keyboard.h which includes this. Just forward-declare. */
typedef int32_t SDL_Keycode;
