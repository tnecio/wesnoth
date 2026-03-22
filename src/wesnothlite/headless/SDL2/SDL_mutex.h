#pragma once
/* Headless mock: SDL_mutex.h */
#include "SDL_stdinc.h"

/* Mutex */
struct SDL_mutex { int dummy; };
static inline SDL_mutex* SDL_CreateMutex() { return nullptr; }
static inline void SDL_DestroyMutex(SDL_mutex*) {}
static inline int SDL_LockMutex(SDL_mutex*) { return 0; }
static inline int SDL_UnlockMutex(SDL_mutex*) { return 0; }
static inline int SDL_TryLockMutex(SDL_mutex*) { return 0; }

/* Semaphore */
struct SDL_sem { int value; };
static inline SDL_sem* SDL_CreateSemaphore(Uint32 initial_value) {
    SDL_sem* s = new SDL_sem(); if(s) s->value = (int)initial_value; return s;
}
static inline void SDL_DestroySemaphore(SDL_sem* sem) { delete sem; }
static inline int SDL_SemWait(SDL_sem* sem) { if(sem && sem->value > 0) { sem->value--; } return 0; }
static inline int SDL_SemTryWait(SDL_sem* sem) {
    if(sem && sem->value > 0) { sem->value--; return 0; } return 1;
}
static inline int SDL_SemPost(SDL_sem* sem) { if(sem) { sem->value++; } return 0; }
static inline Uint32 SDL_SemValue(SDL_sem* sem) { return sem ? (Uint32)sem->value : 0; }
static inline int SDL_SemWaitTimeout(SDL_sem*, Uint32) { return 0; }

/* Condition variable */
struct SDL_cond { int dummy; };
static inline SDL_cond* SDL_CreateCond() { return nullptr; }
static inline void SDL_DestroyCond(SDL_cond*) {}
static inline int SDL_CondSignal(SDL_cond*) { return 0; }
static inline int SDL_CondBroadcast(SDL_cond*) { return 0; }
static inline int SDL_CondWait(SDL_cond*, SDL_mutex*) { return 0; }
