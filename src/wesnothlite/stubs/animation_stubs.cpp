/* Headless stubs: animation subsystem.
 * All implementations are no-ops.
 */

#include "units/animation.hpp"
#include "units/frame.hpp"
#include "config.hpp"
#include "color.hpp"
#include <chrono>

// ---- frame_builder ----

frame_builder::frame_builder()
    : duration_(std::chrono::milliseconds{1})
    , image_()
    , image_diagonal_()
    , image_mod_()
    , halo_()
    , halo_x_()
    , halo_y_()
    , halo_mod_()
    , sound_()
    , text_()
    , text_color_()
    , blend_with_()
    , blend_ratio_()
    , highlight_ratio_()
    , offset_()
    , submerge_()
    , x_()
    , y_()
    , directional_x_()
    , directional_y_()
    , auto_vflip_(boost::logic::indeterminate)
    , auto_hflip_(boost::logic::indeterminate)
    , primary_frame_(boost::logic::indeterminate)
    , drawing_layer_()
{}

// ---- unit_animation static functions ----

void unit_animation::fill_initial_animations(std::vector<unit_animation>& /*animations*/,
                                              const config& /*cfg*/)
{}

// ---- unit_animation::particle destructor ----

unit_animation::particle::~particle() {}

// ---- unit_animator methods ----

void unit_animator::add_animation(unit_const_ptr /*animated_unit*/,
                                  const std::string& /*event*/,
                                  const map_location& /*src*/,
                                  const map_location& /*dst*/,
                                  const int /*value*/,
                                  bool /*with_bars*/,
                                  const std::string& /*text*/,
                                  const color_t /*text_color*/,
                                  const strike_result::type /*hit_type*/,
                                  const const_attack_ptr& /*attack*/,
                                  const const_attack_ptr& /*second_attack*/,
                                  int /*value2*/)
{}

void unit_animator::add_animation(unit_const_ptr /*animated_unit*/,
                                  const unit_animation* /*animation*/,
                                  const map_location& /*src*/,
                                  bool /*with_bars*/,
                                  const std::string& /*text*/,
                                  const color_t /*text_color*/)
{}

void unit_animator::set_all_standing() {}
void unit_animator::start_animations() {}
void unit_animator::wait_for_end() const {}
