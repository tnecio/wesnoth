#pragma once
/* Headless mock: SDL_rwops.h */
struct SDL_RWops { int type; };
static inline SDL_RWops* SDL_RWFromFile(const char*, const char*) { return nullptr; }
static inline SDL_RWops* SDL_RWFromMem(void*, int) { return nullptr; }
static inline int SDL_RWclose(SDL_RWops*) { return 0; }
static inline long SDL_RWseek(SDL_RWops*, long, int) { return 0; }
static inline long SDL_RWtell(SDL_RWops*) { return 0; }
static inline size_t SDL_RWread(SDL_RWops*, void*, size_t, size_t) { return 0; }
static inline size_t SDL_RWwrite(SDL_RWops*, const void*, size_t, size_t) { return 0; }
