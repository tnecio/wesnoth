/**
 * wl_controller.cpp  —  WLController class and game-thread entry point
 *
 * WLController subclasses playsingle_controller, intercepting play_human_turn()
 * to drive the game loop via the WLChannel command queue instead of an SDL
 * event loop.
 *
 * wl_run_game_thread() is the std::thread entry point called by
 * launch_game_thread() in wesnothlite.cpp.
 */

#include "wl_impl.hpp"

#include "actions/attack.hpp"
#include "actions/create.hpp"
#include "actions/move.hpp"
#include "actions/undo.hpp"
#include "game_board.hpp"
#include "game_config_manager.hpp"
#include "game_end_exceptions.hpp"
#include "game_state.hpp"
#include "map/map.hpp"
#include "pathfind/pathfind.hpp"
#include "playsingle_controller.hpp"
#include "recall_list_manager.hpp"
#include "replay_helper.hpp"
#include "resources.hpp"
#include "save_index.hpp"
#include "saved_game.hpp"
#include "savegame.hpp"
#include "scripting/game_lua_kernel.hpp"
#include "synced_context.hpp"
#include "team.hpp"
#include "tod_manager.hpp"
#include "units/map.hpp"
#include "units/types.hpp"
#include "units/unit.hpp"

/* =========================================================================
 * WLController
 * playsingle_controller adapted for channel-based I/O.
 * ========================================================================= */

class WLController : public playsingle_controller
{
public:
    WLController(const config& level, saved_game& state,
                 std::shared_ptr<WLChannel> ch)
        : playsingle_controller(level, state)
        , ch_(std::move(ch))
    {}

protected:
    void play_human_turn() override
    {
        /* Tell the API thread we are ready for commands. */
        {
            WLEventInternal ev;
            ev.type = WL_EVENT_WAITING_FOR_INPUT;
            ev.i1   = current_side();
            ev.i2   = static_cast<int>(turn());
            ch_->post_event(std::move(ev));
        }
        ch_->set_waiting(true);

        while(!should_return_to_play_side()) {
            WLCommand cmd = ch_->wait_for_command();
            WL_Status result = process_command(cmd);
            ch_->post_result(result);

            if(result == WL_OK && cmd.type == WLCmdType::END_TURN)
                break;
            if(result == WL_OK && cmd.type == WLCmdType::UNDO)
                continue;   /* undo stays in loop */
        }

        ch_->set_waiting(false);
    }

private:
    std::shared_ptr<WLChannel> ch_;

    WL_Status process_command(const WLCommand& cmd)
    {
        try {
            switch(cmd.type) {
            case WLCmdType::END_TURN:
                force_end_turn();
                return WL_OK;

            case WLCmdType::UNDO:
                if(undo_stack().can_undo()) {
                    undo_stack().undo();
                    return WL_OK;
                }
                return WL_ERR_INVALID;

            case WLCmdType::MOVE:
                return do_move(cmd.loc1, cmd.loc2);

            case WLCmdType::ATTACK:
                return do_attack(cmd.loc1, cmd.loc2, cmd.int1);

            case WLCmdType::RECRUIT:
                return do_recruit(cmd.str1,
                    map_location(cmd.loc1.x - 1, cmd.loc1.y - 1));

            case WLCmdType::RECALL:
                return do_recall(cmd.str1,
                    map_location(cmd.loc1.x - 1, cmd.loc1.y - 1));

            case WLCmdType::DISMISS:
                return do_dismiss(cmd.str1);

            case WLCmdType::CHOOSE:
                /* Deliver the choice to a pending request_choice() call
                   that originated from inside do_move/do_attack/etc. */
                if(ch_->deliver_choice(cmd.int1))
                    return WL_OK;
                return WL_ERR_INVALID;
            }
        } catch(const std::exception& e) {
            ERR_WL << "command error: " << e.what();
            return WL_ERR_GENERIC;
        }
        return WL_ERR_INVALID;
    }

    /* ── Action helpers ── */

    WL_Status do_move(WL_Loc wfrom, WL_Loc wto)
    {
        map_location from(wfrom.x - 1, wfrom.y - 1);
        map_location to  (wto.x   - 1, wto.y   - 1);

        const gamemap& m = get_map();
        if(!from.valid(m.w(), m.h()) || !to.valid(m.w(), m.h()))
            return WL_ERR_INVALID;

        auto it = get_units().find(from);
        if(it == get_units().end() || it->side() != current_side())
            return WL_ERR_INVALID;

        pathfind::shortest_path_calculator calc(
            *it, current_team(), get_teams(), m);
        pathfind::plain_route route = pathfind::a_star_search(
            from, to, 10000.0, calc, m.w(), m.h());

        if(route.steps.empty())
            return WL_ERR_NO_PATH;

        /* Post the move event with full path before the move executes.
         * The Lua moveto handler will skip its duplicate via tl_skip_next_moveto. */
        {
            WLEventInternal ev;
            ev.type = WL_EVENT_UNIT_MOVE;
            ev.s1   = it->id();
            ev.i1   = it->side();
            ev.loc1 = { from.wml_x(), from.wml_y() };
            ev.loc2 = { to.wml_x(),   to.wml_y() };
            int n = std::min(static_cast<int>(route.steps.size()), WL_MAX_PATH);
            ev.path.resize(n);
            for(int i = 0; i < n; ++i)
                ev.path[i] = { route.steps[i].wml_x(), route.steps[i].wml_y() };
            tl_channel->post_event(std::move(ev));
        }
        tl_skip_next_moveto = true;
        actions::move_unit_and_record(route.steps, &undo_stack());
        return WL_OK;
    }

    WL_Status do_attack(WL_Loc watt, WL_Loc wdef, int weapon)
    {
        map_location att(watt.x - 1, watt.y - 1);
        map_location def(wdef.x - 1, wdef.y - 1);

        const gamemap& m = get_map();
        if(!att.valid(m.w(), m.h()) || !def.valid(m.w(), m.h()))
            return WL_ERR_INVALID;

        auto a = get_units().find(att);
        if(a == get_units().end() || a->side() != current_side())
            return WL_ERR_INVALID;
        auto d = get_units().find(def);
        if(d == get_units().end())
            return WL_ERR_INVALID;
        if(weapon < 0) weapon = 0;

        synced_context::run_and_throw("attack",
            replay_helper::get_attack(
                att, def,
                weapon, -1,
                a->type_id(), d->type_id(),
                a->level(),  d->level(),
                resources::tod_manager->turn(),
                resources::tod_manager->get_time_of_day()));
        return WL_OK;
    }

    WL_Status do_recruit(const std::string& type_id, map_location hex)
    {
        const unit_type* ut = unit_types.find(type_id);
        if(!ut) return WL_ERR_UNKNOWN;
        if(current_team().gold() < ut->cost()) return WL_ERR_NO_GOLD;

        map_location from;
        std::string err = actions::find_recruit_location(
            current_side(), hex, from, type_id);
        if(!err.empty()) return WL_ERR_NO_SPACE;

        synced_context::run_and_throw("recruit",
            replay_helper::get_recruit(type_id, hex, from));
        return WL_OK;
    }

    WL_Status do_recall(const std::string& unit_id, map_location hex)
    {
        const team& t = current_team();
        const recall_list_manager& rl = t.recall_list();
        unit_ptr u_ptr;
        for(const auto& u : rl) {
            if(u->id() == unit_id) { u_ptr = u; break; }
        }
        if(!u_ptr) return WL_ERR_UNKNOWN;
        int cost = u_ptr->recall_cost() >= 0 ? u_ptr->recall_cost() : t.recall_cost();
        if(t.gold() < cost) return WL_ERR_NO_GOLD;

        map_location from;
        std::string err = actions::find_recall_location(
            current_side(), hex, from, *u_ptr);
        if(!err.empty()) return WL_ERR_NO_SPACE;

        synced_context::run_in_synced_context_if_not_already("recall",
            replay_helper::get_recall(unit_id, hex, from));
        return WL_OK;
    }

    WL_Status do_dismiss(const std::string& unit_id)
    {
        const recall_list_manager& rl = current_team().recall_list();
        bool found = std::any_of(rl.begin(), rl.end(),
            [&](const unit_ptr& u){ return u->id() == unit_id; });
        if(!found) return WL_ERR_UNKNOWN;

        synced_context::run_and_throw("disband",
            replay_helper::get_disband(unit_id));
        return WL_OK;
    }
};

/* =========================================================================
 * Game-thread entry point
 * ========================================================================= */

void wl_run_game_thread(WLEngineImpl* e)
{
    tl_channel = e->channel.get();
    WLChannel& ch = *e->channel;

    try {
        saved_game& state = *e->state;

        /* Emit LOADING_CONFIG so the JS side can show a progress message
         * while the slow WML parse runs off the API/JS thread. */
        {
            WLEventInternal ev;
            ev.type = WL_EVENT_LOADING_CONFIG;
            ch.post_event(std::move(ev));
        }

        e->config_manager->load_game_config_for_game(
            state.classification(),
            e->needs_scenario_init ? e->pending_scenario_id
                                   : state.get_scenario_id());

        if(e->needs_scenario_init) {
            const std::string& sid = e->pending_scenario_id;
            config scenario_cfg;
            for(const config& sc :
                    e->config_manager->game_config().child_range("scenario")) {
                if(sc["id"].str() == sid) { scenario_cfg = sc; break; }
            }
            if(scenario_cfg.empty()) {
                e->last_error = std::string("scenario not found: ") + sid;
                WLEventInternal ev;
                ev.type    = WL_EVENT_SCENARIO_END;
                ev.outcome = WL_OUTCOME_QUIT;
                ch.post_event(std::move(ev));
                tl_channel = nullptr;
                ch.set_done();
                return;
            }
            state.set_scenario(scenario_cfg);
            state.set_defaults();
            e->needs_scenario_init = false;
        }

        state.expand_scenario();
        state.expand_random_scenario();
        state.expand_mp_events();
        state.expand_carryover();
        state.expand_mp_options();

        /* Emit scenario-start event. */
        {
            WLEventInternal ev;
            ev.type = WL_EVENT_SCENARIO_START;
            ch.post_event(std::move(ev));
        }

        const config& level = state.get_starting_point();
        WLController controller(level, state, e->channel);

        /* Register wl_post_* functions and inject the Lua hook snippet. */
        wl_lua_setup();

        level_result::type result = controller.play_scenario(level);

        /* Extract next_scenario by converting state to start-save form,
         * mirroring what campaign_controller::play_game() does. */
        std::string next_scenario_id;
        if(result == level_result::type::victory
                && controller.is_regular_game_end()
                && controller.get_end_level_data().proceed_to_next_level) {
            state.set_snapshot(controller.to_config());
            state.convert_to_start_save();
            next_scenario_id = state.get_scenario_id();
        }

        /* Emit scenario-end event. */
        WLEventInternal ev;
        ev.type    = WL_EVENT_SCENARIO_END;
        ev.outcome = (result == level_result::type::victory)
                         ? WL_OUTCOME_VICTORY
                         : WL_OUTCOME_DEFEAT;
        ev.s1 = next_scenario_id;
        ch.post_event(std::move(ev));

    } catch(const savegame::load_game_exception&) {
        WLEventInternal ev;
        ev.type    = WL_EVENT_SCENARIO_END;
        ev.outcome = WL_OUTCOME_QUIT;
        ch.post_event(std::move(ev));
    } catch(...) {
        e->game_exception = std::current_exception();
        WLEventInternal ev;
        ev.type    = WL_EVENT_SCENARIO_END;
        ev.outcome = WL_OUTCOME_QUIT;
        ch.post_event(std::move(ev));
    }

    tl_channel = nullptr;
    ch.set_done();
}
