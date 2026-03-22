/* Headless stub: theme.cpp — theme is display-layout only; stubs all functionality */
#include "theme.hpp"

theme::theme(const config&, const SDL_Rect&) {}
theme::theme() {}
bool theme::set_resolution(const SDL_Rect&) { return false; }
void theme::set_known_themes(const config*) {}
std::vector<std::string> theme::get_known_themes() { return {}; }
const std::string& theme::get_id() const { static std::string s; return s; }
