/* Headless stubs: whiteboard manager and related classes.
 * All implementations are no-ops.
 */

#include "whiteboard/manager.hpp"
#include "whiteboard/side_actions.hpp"
#include "config.hpp"
#include "key.hpp"
#include "pathfind/pathfind.hpp"
#include "team.hpp"
// Complete types needed for unique_ptr destructors in manager
#include "whiteboard/mapbuilder.hpp"

namespace wb {

// ---- manager ----

manager::manager()
    : active_(false)
    , inverted_behavior_(false)
    , self_activate_once_(false)
    , wait_for_side_init_(true)
    , planned_unit_map_active_(false)
    , executing_actions_(false)
    , executing_all_actions_(false)
    , preparing_to_end_turn_(false)
    , gamestate_mutated_(false)
    , activation_state_lock_(std::make_shared<bool>(false))
    , unit_map_lock_(std::make_shared<bool>(false))
    , mapbuilder_()
    , highlighter_()
    , route_()
    , move_arrows_()
    , fake_units_()
    , temp_move_unit_underlying_id_(0)
    , key_poller_(new CKey())
    , hidden_unit_hexes_()
    , net_buffer_()
    , team_plans_hidden_()
    , units_owning_moves_()
{}

manager::~manager() {}

int manager::get_spent_gold_for(int /*side*/) { return 0; }
void manager::on_change_controller(int /*side*/, const team& /*t*/) {}
void manager::on_finish_side_turn(int /*side*/) {}
void manager::on_gamestate_change() {}
void manager::on_init_side() {}
void manager::on_kill_unit() {}
void manager::process_network_data(const config& /*cfg*/) {}
void manager::send_network_data() {}
void manager::set_invert_behavior(bool /*invert*/) {}
bool manager::should_clear_undo() const { return false; }

// ---- real_map ----

real_map::real_map()
    : initial_planned_unit_map_(false)
    , unit_map_lock_(std::make_shared<bool>(false))
{}

real_map::~real_map() {}

// ---- side_actions ----

side_actions::side_actions()
    : actions_()
    , team_index_(0)
    , team_index_defined_(false)
    , gold_spent_(0)
    , hidden_(false)
{}

void side_actions::set_team_index(std::size_t team_index)
{
    team_index_ = team_index;
    team_index_defined_ = true;
}

} // namespace wb
