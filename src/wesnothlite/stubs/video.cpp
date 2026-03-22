/* Headless stub: video.cpp — all rendering is a no-op; headless() always true */
#include "video.hpp"
#include "sdl/surface.hpp"
#include "sdl/texture.hpp"
#include "sdl/point.hpp"
#include "sdl/rect.hpp"

namespace video {

namespace {
    bool headless_ = false;
    point canvas_size_{800, 600};
}

/* Init / deinit */
void init(fake fake_type) { headless_ = (fake_type != fake::none); }
void deinit() {}
void update_buffers(bool) {}

/* Headless/testing state */
bool headless() { return headless_; }
bool testing() { return false; }

/* Window state — always report "no window" */
bool has_window() { return false; }
bool is_fullscreen() { return false; }
void set_fullscreen(bool) {}
void toggle_fullscreen() {}
bool set_resolution(const point&) { return false; }
point current_resolution() { return canvas_size_; }
std::vector<point> get_available_resolutions(bool) { return {canvas_size_}; }
std::string current_driver() { return "headless"; }
std::vector<std::string> enumerate_drivers() { return {"headless"}; }
int current_refresh_rate() { return 60; }
int native_refresh_rate() { return 60; }

bool window_is_visible() { return false; }
bool window_has_focus() { return false; }
bool window_has_mouse_focus() { return false; }

void set_window_title(const std::string&) {}
void set_window_icon(surface&) {}

/* Coordinate system — fixed 800x600 virtual canvas */
rect game_canvas() { return {0, 0, canvas_size_.x, canvas_size_.y}; }
point game_canvas_size() { return canvas_size_; }
point draw_size() { return canvas_size_; }
rect draw_area() { return {0, 0, canvas_size_.x, canvas_size_.y}; }
point output_size() { return canvas_size_; }
rect output_area() { return {0, 0, canvas_size_.x, canvas_size_.y}; }
point window_size() { return canvas_size_; }
rect input_area() { return {0, 0, canvas_size_.x, canvas_size_.y}; }
rect to_output(const rect& r) { return r; }

int get_pixel_scale() { return 1; }
int get_max_pixel_scale() { return 1; }

/* Rendering — all no-op */
surface read_pixels(rect*) { return surface(); }
surface read_pixels_low_res(rect*) { return surface(); }
texture get_render_target() { return texture(); }
void force_render_target(const texture&) {}
void clear_render_target() {}
void reset_render_target() {}

std::vector<std::pair<std::string, std::string>> renderer_report() { return {}; }

} // namespace video
