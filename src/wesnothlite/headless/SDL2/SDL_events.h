#pragma once
/* Headless mock: SDL_events.h */
#include "SDL_rect.h"
#include "SDL_keyboard.h"
#include <cstdint>

/* Event type constants */
#define SDL_QUIT            0x100u
#define SDL_KEYDOWN         0x300u
#define SDL_KEYUP           0x301u
#define SDL_MOUSEMOTION     0x400u
#define SDL_MOUSEBUTTONDOWN 0x401u
#define SDL_MOUSEBUTTONUP   0x402u
#define SDL_MOUSEWHEEL      0x403u
#define SDL_FINGERDOWN      0x700u
#define SDL_FINGERUP        0x701u
#define SDL_FINGERMOTION    0x702u
#define SDL_MULTIGESTURE    0x800u
#define SDL_TEXTINPUT       0x303u
#define SDL_TEXTEDITING     0x302u
#define SDL_JOYBUTTONDOWN   0x604u
#define SDL_JOYBUTTONUP     0x605u
#define SDL_JOYHATMOTION    0x602u
#define SDL_JOYAXISMOTION   0x600u
#define SDL_WINDOWEVENT     0x200u
#define SDL_USEREVENT       0x8000u
#define SDL_TOUCH_MOUSEID   ((uint32_t)-1)

#define SDL_WINDOWEVENT_SHOWN        1
#define SDL_WINDOWEVENT_RESIZED      5
#define SDL_WINDOWEVENT_SIZE_CHANGED 6
#define SDL_WINDOWEVENT_MINIMIZED    7
#define SDL_WINDOWEVENT_MAXIMIZED    8
#define SDL_WINDOWEVENT_RESTORED     9
#define SDL_WINDOWEVENT_FOCUS_GAINED 12
#define SDL_WINDOWEVENT_FOCUS_LOST   13

typedef struct SDL_WindowEvent {
    uint32_t type; uint32_t timestamp; uint32_t windowID;
    uint8_t event; uint8_t padding1; uint8_t padding2; uint8_t padding3;
    int32_t data1; int32_t data2;
} SDL_WindowEvent;

static inline void SDL_StartTextInput() {}
static inline void SDL_StopTextInput() {}
static inline int SDL_IsTextInputActive() { return 0; }
#define SDL_ADDEVENT  0
#define SDL_PEEKEVENT 1
#define SDL_GETEVENT  2

/* Event structs */
typedef struct SDL_CommonEvent   { uint32_t type; uint32_t timestamp; } SDL_CommonEvent;
typedef struct SDL_KeyboardEvent {
    uint32_t type; uint32_t timestamp; uint32_t windowID;
    uint8_t state; uint8_t repeat; uint8_t padding2; uint8_t padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;
typedef struct SDL_MouseMotionEvent {
    uint32_t type; uint32_t timestamp; uint32_t windowID;
    uint32_t which; uint32_t state;
    int32_t x; int32_t y; int32_t xrel; int32_t yrel;
} SDL_MouseMotionEvent;
typedef struct SDL_MouseButtonEvent {
    uint32_t type; uint32_t timestamp; uint32_t windowID;
    uint32_t which; uint8_t button; uint8_t state; uint8_t clicks; uint8_t padding1;
    int32_t x; int32_t y;
} SDL_MouseButtonEvent;
typedef struct SDL_MouseWheelEvent {
    uint32_t type; uint32_t timestamp; uint32_t windowID;
    uint32_t which; int32_t x; int32_t y; uint32_t direction;
} SDL_MouseWheelEvent;
typedef int64_t SDL_TouchID;
typedef int64_t SDL_FingerID;
typedef struct SDL_TouchFingerEvent {
    uint32_t type; uint32_t timestamp;
    SDL_TouchID touchId; SDL_FingerID fingerId;
    float x; float y; float dx; float dy; float pressure;
    uint32_t windowID;
} SDL_TouchFingerEvent;
typedef struct SDL_UserEvent {
    uint32_t type; uint32_t timestamp; uint32_t windowID;
    int32_t code; void* data1; void* data2;
} SDL_UserEvent;
typedef struct SDL_QuitEvent { uint32_t type; uint32_t timestamp; } SDL_QuitEvent;

typedef union SDL_Event {
    uint32_t type;
    SDL_CommonEvent common;
    SDL_WindowEvent window;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_TouchFingerEvent tfinger;
    SDL_UserEvent user;
    SDL_QuitEvent quit;
    uint8_t padding[56];
} SDL_Event;

static inline uint32_t SDL_GetMouseState(int* x, int* y) {
    if(x) *x = 0; if(y) *y = 0; return 0;
}
static inline uint32_t SDL_GetRelativeMouseState(int* x, int* y) {
    if(x) *x = 0; if(y) *y = 0; return 0;
}
static inline SDL_Keymod SDL_GetModState() { return KMOD_NONE; }
static inline int SDL_PeepEvents(SDL_Event*, int, int, uint32_t, uint32_t) { return 0; }
static inline int SDL_PollEvent(SDL_Event*) { return 0; }
static inline void SDL_PumpEvents() {}
static inline int SDL_PushEvent(SDL_Event*) { return 1; }
static inline int SDL_WaitEvent(SDL_Event*) { return 1; }
