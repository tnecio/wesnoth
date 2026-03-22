#pragma once
/* Headless stub: units/udisplay.hpp — all animation no-ops */
#include "fake_unit_ptr.hpp"
#include "map/location.hpp"
#include <vector>
#include <string>

class attack_type;
class game_board;
class display;
class unit;
using const_attack_ptr = std::shared_ptr<const attack_type>;
using unit_ptr = std::shared_ptr<unit>;

namespace unit_display {

class unit_movement_animator {
public:
    unit_movement_animator(const unit_movement_animator&) = delete;
    unit_movement_animator& operator=(const unit_movement_animator&) = delete;

    explicit unit_movement_animator(const std::vector<map_location>&, bool = true, bool = false) {}
    ~unit_movement_animator() {}

    void start(const unit_ptr&) {}
    void proceed_to(const unit_ptr&, std::size_t, bool = false, bool = true) {}
    void wait_for_anims() {}
    void finish(const unit_ptr&, map_location::direction = map_location::direction::indeterminate) {}
};

inline void move_unit(const std::vector<map_location>&, unit_ptr,
    bool = true, map_location::direction = map_location::direction::indeterminate, bool = false) {}

inline void unit_draw_weapon(const map_location&, unit&,
    const_attack_ptr = nullptr, const_attack_ptr = nullptr,
    const map_location& = map_location::null_location(), unit_ptr = nullptr) {}

inline void unit_sheath_weapon(const map_location&, unit_ptr = nullptr,
    const_attack_ptr = nullptr, const_attack_ptr = nullptr,
    const map_location& = map_location::null_location(), unit_ptr = nullptr) {}

inline void unit_die(const map_location&, unit&,
    const_attack_ptr = nullptr, const_attack_ptr = nullptr,
    const map_location& = map_location::null_location(), unit_ptr = nullptr) {}

inline void unit_attack(display*, game_board&,
    const map_location&, const map_location&, int,
    const attack_type&, const_attack_ptr,
    int, const std::string&, int, const std::string&,
    const std::vector<std::string>* = nullptr, bool = true) {}

inline void unit_recruited(const map_location&,
    const map_location& = map_location::null_location()) {}

inline void unit_healing(unit&, const std::vector<unit*>&, int,
    const std::string& = "") {}

} // namespace unit_display
