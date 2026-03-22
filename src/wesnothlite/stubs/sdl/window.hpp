#pragma once
/* Headless stub: sdl/window.hpp — hollow sdl::window, never instantiated */
#include "sdl/point.hpp"
#include <cstdint>
#include <string>

class surface;
struct SDL_Renderer;
struct SDL_Window;

namespace sdl {

class window {
public:
    window(const window&) = delete;
    window& operator=(const window&) = delete;
    window(const std::string&, int, int, int, int, uint32_t, uint32_t) {}
    ~window() {}

    void set_size(int, int) {}
    SDL_Point get_size() { return {800, 600}; }
    SDL_Point get_output_size() { return {800, 600}; }
    void center() {}
    void maximize() {}
    void restore() {}
    void to_window() {}
    void full_screen() {}
    void fill(uint8_t, uint8_t, uint8_t, uint8_t = 0) {}
    void render() {}
    void set_title(const std::string&) {}
    void set_icon(const surface&) {}
    uint32_t get_flags() { return 0; }
    void set_minimum_size(int, int) {}
    int get_display_index() { return 0; }
    void set_logical_size(int, int) {}
    void set_logical_size(const point&) {}
    point get_logical_size() const { return {800, 600}; }
    void get_logical_size(int& w, int& h) const { w = 800; h = 600; }
    uint32_t pixel_format() { return 0; }

    operator SDL_Window*() { return nullptr; }
    operator SDL_Renderer*() { return nullptr; }
};

} // namespace sdl
