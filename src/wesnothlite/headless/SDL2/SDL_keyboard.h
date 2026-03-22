#pragma once
/* Headless mock: SDL_keyboard.h */
#include <cstdint>

typedef int32_t SDL_Keycode;
typedef int     SDL_Scancode;
typedef int     SDL_Keymod;

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode  sym;
    uint16_t     mod;
    uint32_t     unused;
} SDL_Keysym;

/* Keymod flags */
#define KMOD_NONE  0x0000
#define KMOD_LSHIFT 0x0001
#define KMOD_RSHIFT 0x0002
#define KMOD_LCTRL  0x0040
#define KMOD_RCTRL  0x0080
#define KMOD_LALT   0x0100
#define KMOD_RALT   0x0200
#define KMOD_LGUI   0x0400
#define KMOD_RGUI   0x0800
#define KMOD_NUM    0x1000
#define KMOD_CAPS   0x2000
#define KMOD_MODE   0x4000
#define KMOD_SHIFT  (KMOD_LSHIFT|KMOD_RSHIFT)
#define KMOD_CTRL   (KMOD_LCTRL|KMOD_RCTRL)
#define KMOD_ALT    (KMOD_LALT|KMOD_RALT)
#define KMOD_GUI    (KMOD_LGUI|KMOD_RGUI)

/* Common keycodes used in wesnoth */
#define SDLK_UNKNOWN    0
#define SDLK_RETURN     13
#define SDLK_ESCAPE     27
#define SDLK_SPACE      32
#define SDLK_BACKSPACE  8
#define SDLK_TAB        9
#define SDLK_DELETE     127
#define SDLK_F1         (1<<30|58)
#define SDLK_F2         (1<<30|59)
#define SDLK_UP         (1<<30|82)
#define SDLK_DOWN       (1<<30|81)
#define SDLK_LEFT       (1<<30|80)
#define SDLK_RIGHT      (1<<30|79)
#define SDLK_KP_ENTER   (1<<30|88)
#define SDLK_LCTRL      (1<<30|224)
#define SDLK_RCTRL      (1<<30|228)
#define SDLK_LALT       (1<<30|226)
#define SDLK_RALT       (1<<30|230)
#define SDLK_LSHIFT     (1<<30|225)
#define SDLK_RSHIFT     (1<<30|229)
#define SDLK_AC_BACK    (1<<30|270)
