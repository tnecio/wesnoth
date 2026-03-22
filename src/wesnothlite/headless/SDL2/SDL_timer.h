#pragma once
/* Headless mock: SDL_timer.h */
#include <chrono>
#include <cstdint>
inline uint32_t SDL_GetTicks() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
inline void SDL_Delay(uint32_t) {}
