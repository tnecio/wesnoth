/*
 * Headless stubs for sdl/surface.hpp.
 *
 * Split out of misc_stubs.cpp for the same reason as picture.cpp: a build that
 * wants the real SDL surface implementation (wl-image-oracle links
 * sdl/surface.cpp) can drop this file instead of colliding with it. See
 * source_lists/wesnoth_oracle_stubs.
 */
// ---- surface methods ----
// sdl/surface.cpp is in libwesnoth_sdl, not compiled into headless.
// Provide minimal stubs for all non-inline surface methods.
#include "sdl/surface.hpp"
#include "sdl/point.hpp"

surface::surface(SDL_Surface* surf) : surface_(surf) {}
surface::surface(int /*w*/, int /*h*/) : surface_(nullptr) {}
surface::surface(const surface& s) : surface_(s.surface_) { if(surface_) ++surface_->refcount; }
surface::surface(surface&& s) noexcept : surface_(s.surface_) { s.surface_ = nullptr; }
surface::~surface() { if(surface_) SDL_FreeSurface(surface_); }

surface& surface::operator=(const surface& s)
{
    if(surface_) SDL_FreeSurface(surface_);
    surface_ = s.surface_;
    if(surface_) ++surface_->refcount;
    return *this;
}

surface& surface::operator=(surface&& s) noexcept
{
    if(surface_) SDL_FreeSurface(surface_);
    surface_ = s.surface_;
    s.surface_ = nullptr;
    return *this;
}

surface surface::clone() const { return surface{}; }
point surface::size() const { return {0, 0}; }
std::size_t surface::area() const { return 0; }

std::ostream& operator<<(std::ostream& s, const surface& /*surf*/) { return s; }

