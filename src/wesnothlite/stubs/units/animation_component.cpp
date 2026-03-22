/* Headless stub: units/animation_component.cpp */
#include <chrono>
#include "units/animation_component.hpp"

const unit_animation* unit_animation_component::choose_animation(
    const map_location&, const std::string&, const map_location&,
    int, const strike_result::type, const const_attack_ptr&, const const_attack_ptr&, int)
{ return nullptr; }

void unit_animation_component::set_standing(bool) {}
void unit_animation_component::set_ghosted(bool) {}
void unit_animation_component::set_disabled_ghosted(bool) {}
void unit_animation_component::set_idling() {}
void unit_animation_component::set_selecting() {}
void unit_animation_component::start_animation(const std::chrono::milliseconds&, const unit_animation*, bool, const std::string&, color_t, STATE) {}
bool unit_animation_component::invalidate(const display&) { return false; }
void unit_animation_component::refresh() {}
void unit_animation_component::clear_haloes() {}
void unit_animation_component::reset_after_advance(const unit_type*) {}
void unit_animation_component::apply_new_animation_effect(const config&) {}
std::vector<std::string> unit_animation_component::get_flags() { return {}; }
