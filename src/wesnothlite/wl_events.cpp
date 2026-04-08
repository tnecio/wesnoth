/**
 * wl_events.cpp  —  Event materialisation
 *
 * Converts a WLEventInternal (std::variant over WLEv::* structs) into a
 * WL_Event written into a caller-supplied std::vector<char> buffer
 * (engine-owned).  All const char* fields in the result point into the same
 * contiguous block.  The returned pointer is valid until the next call to
 * wl_materialize_event() on the same buffer.  Do NOT pass it to wl_free().
 */

#include "wl_impl.hpp"

#include <algorithm>
#include <cstring>

/* =========================================================================
 * wl_materialize_event
 * ========================================================================= */

WL_Event* wl_materialize_event(const WLEventInternal& d,
                                std::vector<char>& buf)
{
    /* Visitor: fills a stack-local WL_Event and a WLArena, then relocates
     * all const char* into the final contiguous buffer. */
    WL_Event ev{};
    WLArena  arena;

    auto store = [&](const std::string& s) -> const char* {
        return arena.store(s);
    };

    std::visit([&](const auto& e) {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, WLEv::LoadingConfig>) {
            ev.type = WL_EVENT_LOADING_CONFIG;

        } else if constexpr (std::is_same_v<T, WLEv::ScenarioStart>) {
            ev.type = WL_EVENT_SCENARIO_START;

        } else if constexpr (std::is_same_v<T, WLEv::ScenarioEnd>) {
            ev.type = WL_EVENT_SCENARIO_END;
            ev.scenario_end.outcome       = e.outcome;
            ev.scenario_end.next_scenario = store(e.next_scenario);

        } else if constexpr (std::is_same_v<T, WLEv::TurnStart>) {
            ev.type = WL_EVENT_TURN_START;
            ev.turn_start.turn = e.turn;

        } else if constexpr (std::is_same_v<T, WLEv::SideTurnStart>) {
            ev.type = WL_EVENT_SIDE_TURN_START;
            ev.side_turn_start.side = e.side;
            ev.side_turn_start.turn = e.turn;

        } else if constexpr (std::is_same_v<T, WLEv::SideTurnEnd>) {
            ev.type = WL_EVENT_SIDE_TURN_END;
            ev.side_turn_end.side = e.side;
            ev.side_turn_end.turn = e.turn;

        } else if constexpr (std::is_same_v<T, WLEv::WaitingForInput>) {
            ev.type = WL_EVENT_WAITING_FOR_INPUT;
            ev.waiting_for_input.side = e.side;
            ev.waiting_for_input.turn = e.turn;

        } else if constexpr (std::is_same_v<T, WLEv::UnitMove>) {
            ev.type = WL_EVENT_UNIT_MOVE;
            ev.unit_move.unit_id = store(e.unit_id);
            ev.unit_move.side    = e.side;
            ev.unit_move.from    = e.from;
            ev.unit_move.to      = e.to;
            int n = std::min(static_cast<int>(e.path.size()), WL_MAX_PATH);
            for(int i = 0; i < n; ++i) ev.unit_move.path[i] = e.path[i];
            ev.unit_move.path_len = n;

        } else if constexpr (std::is_same_v<T, WLEv::UnitAttack>) {
            ev.type = WL_EVENT_UNIT_ATTACK;
            ev.unit_attack.attacker_id     = store(e.attacker_id);
            ev.unit_attack.defender_id     = store(e.defender_id);
            ev.unit_attack.attacker_side   = e.attacker_side;
            ev.unit_attack.defender_side   = e.defender_side;
            ev.unit_attack.attacker_loc    = e.attacker_loc;
            ev.unit_attack.defender_loc    = e.defender_loc;
            ev.unit_attack.attacker_result = e.cr_att;
            ev.unit_attack.defender_result = e.cr_def;
            {
                int n = std::min(static_cast<int>(e.blows.size()), WL_MAX_BLOWS);
                for(int i = 0; i < n; ++i) ev.unit_attack.blows[i] = e.blows[i];
                ev.unit_attack.n_blows = n;
            }

        } else if constexpr (std::is_same_v<T, WLEv::UnitSpawn>) {
            ev.type = WL_EVENT_UNIT_SPAWN;
            ev.unit_spawn.unit_type_id = store(e.unit_type_id);
            ev.unit_spawn.unit_id      = store(e.unit_id);
            ev.unit_spawn.side         = e.side;
            ev.unit_spawn.at           = e.at;

        } else if constexpr (std::is_same_v<T, WLEv::UnitDismiss>) {
            ev.type = WL_EVENT_UNIT_DISMISS;
            ev.unit_dismiss.unit_id      = store(e.unit_id);
            ev.unit_dismiss.unit_type_id = store(e.unit_type_id);
            ev.unit_dismiss.side         = e.side;

        } else if constexpr (std::is_same_v<T, WLEv::UnitDie>) {
            ev.type = WL_EVENT_UNIT_DIE;
            ev.unit_die.unit_id      = store(e.unit_id);
            ev.unit_die.unit_type_id = store(e.unit_type_id);
            ev.unit_die.side         = e.side;
            ev.unit_die.loc          = e.loc;
            ev.unit_die.killer_id    = store(e.killer_id);

        } else if constexpr (std::is_same_v<T, WLEv::UnitAdvance>) {
            ev.type = WL_EVENT_UNIT_ADVANCE;
            ev.unit_advance.unit_id      = store(e.unit_id);
            ev.unit_advance.side         = e.side;
            ev.unit_advance.loc          = e.loc;
            ev.unit_advance.from_type_id = store(e.from_type_id);
            ev.unit_advance.to_type_id   = store(e.to_type_id);

        } else if constexpr (std::is_same_v<T, WLEv::UnitXP>) {
            ev.type = WL_EVENT_UNIT_XP;
            ev.unit_xp.unit_id   = store(e.unit_id);
            ev.unit_xp.side      = e.side;
            ev.unit_xp.loc       = e.loc;
            ev.unit_xp.xp_gained = e.xp_gained;
            ev.unit_xp.xp_total  = e.xp_total;
            ev.unit_xp.xp_needed = e.xp_needed;

        } else if constexpr (std::is_same_v<T, WLEv::UnitHeal>) {
            ev.type = WL_EVENT_UNIT_HEAL;
            ev.unit_heal.unit_id = store(e.unit_id);
            ev.unit_heal.side    = e.side;
            ev.unit_heal.loc     = e.loc;
            ev.unit_heal.amount  = e.amount;

        } else if constexpr (std::is_same_v<T, WLEv::UnitStatus>) {
            ev.type = WL_EVENT_UNIT_STATUS;
            ev.unit_status.unit_id = store(e.unit_id);
            ev.unit_status.side    = e.side;
            ev.unit_status.loc     = e.loc;
            ev.unit_status.flags   = e.flags;

        } else if constexpr (std::is_same_v<T, WLEv::VillageCapture>) {
            ev.type = WL_EVENT_VILLAGE_CAPTURE;
            ev.village_capture.loc      = e.loc;
            ev.village_capture.old_side = e.old_side;
            ev.village_capture.new_side = e.new_side;

        } else if constexpr (std::is_same_v<T, WLEv::Message>) {
            ev.type = WL_EVENT_MESSAGE;
            ev.message.speaker  = store(e.speaker);
            ev.message.portrait = store(e.portrait);
            ev.message.text     = store(e.text);

        } else if constexpr (std::is_same_v<T, WLEv::Story>) {
            ev.type = WL_EVENT_STORY;
            ev.story.title      = store(e.title);
            ev.story.text       = store(e.text);
            ev.story.background = store(e.background);

        } else if constexpr (std::is_same_v<T, WLEv::ObjectivesUpdate>) {
            ev.type = WL_EVENT_OBJECTIVES_UPDATE;
            ev.objectives_update.side = e.side;
            ev.objectives_update.text = store(e.text);

        } else if constexpr (std::is_same_v<T, WLEv::ChoiceNeeded>) {
            ev.type = WL_EVENT_CHOICE_NEEDED;
            ev.choice_needed.kind   = e.kind;
            ev.choice_needed.prompt = store(e.prompt);
            int n = std::min(static_cast<int>(e.options.size()), WL_MAX_OPTIONS);
            for(int i = 0; i < n; ++i)
                ev.choice_needed.options[i] = store(e.options[i]);
            ev.choice_needed.n_options = n;
            ev.choice_needed.speaker   = store(e.speaker);

        } else if constexpr (std::is_same_v<T, WLEv::Sound>) {
            ev.type = WL_EVENT_SOUND;
            ev.sound.path = store(e.path);

        } else if constexpr (std::is_same_v<T, WLEv::MusicChange>) {
            ev.type = WL_EVENT_MUSIC_CHANGE;
            ev.music_change.path  = store(e.path);
            ev.music_change.title = store(e.title);
        }
    }, d);

    /* Allocate the final contiguous block:  [ WL_Event ][ string arena ]
     * Then relocate every const char* from an arena-relative offset to a
     * buf-relative offset. */

    const size_t ev_sz  = sizeof(WL_Event);
    const size_t str_sz = arena.buf.size();
    buf.resize(ev_sz + str_sz);

    if(str_sz) std::memcpy(buf.data() + ev_sz, arena.buf.data(), str_sz);

    auto relocate = [&](const char* p) -> const char* {
        if(!p || arena.buf.empty()) return nullptr;
        ptrdiff_t off = p - arena.buf.data();
        if(off < 0 || static_cast<size_t>(off) >= arena.buf.size()) return nullptr;
        return reinterpret_cast<const char*>(buf.data() + ev_sz + off);
    };

    std::visit([&](const auto& e) {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, WLEv::ScenarioEnd>) {
            ev.scenario_end.next_scenario = relocate(ev.scenario_end.next_scenario);
        } else if constexpr (std::is_same_v<T, WLEv::UnitMove>) {
            ev.unit_move.unit_id = relocate(ev.unit_move.unit_id);
        } else if constexpr (std::is_same_v<T, WLEv::UnitAttack>) {
            ev.unit_attack.attacker_id = relocate(ev.unit_attack.attacker_id);
            ev.unit_attack.defender_id = relocate(ev.unit_attack.defender_id);
        } else if constexpr (std::is_same_v<T, WLEv::UnitSpawn>) {
            ev.unit_spawn.unit_type_id = relocate(ev.unit_spawn.unit_type_id);
            ev.unit_spawn.unit_id      = relocate(ev.unit_spawn.unit_id);
        } else if constexpr (std::is_same_v<T, WLEv::UnitDismiss>) {
            ev.unit_dismiss.unit_id      = relocate(ev.unit_dismiss.unit_id);
            ev.unit_dismiss.unit_type_id = relocate(ev.unit_dismiss.unit_type_id);
        } else if constexpr (std::is_same_v<T, WLEv::UnitDie>) {
            ev.unit_die.unit_id      = relocate(ev.unit_die.unit_id);
            ev.unit_die.unit_type_id = relocate(ev.unit_die.unit_type_id);
            ev.unit_die.killer_id    = relocate(ev.unit_die.killer_id);
        } else if constexpr (std::is_same_v<T, WLEv::UnitAdvance>) {
            ev.unit_advance.unit_id      = relocate(ev.unit_advance.unit_id);
            ev.unit_advance.from_type_id = relocate(ev.unit_advance.from_type_id);
            ev.unit_advance.to_type_id   = relocate(ev.unit_advance.to_type_id);
        } else if constexpr (std::is_same_v<T, WLEv::UnitXP>) {
            ev.unit_xp.unit_id = relocate(ev.unit_xp.unit_id);
        } else if constexpr (std::is_same_v<T, WLEv::UnitHeal>) {
            ev.unit_heal.unit_id = relocate(ev.unit_heal.unit_id);
        } else if constexpr (std::is_same_v<T, WLEv::UnitStatus>) {
            ev.unit_status.unit_id = relocate(ev.unit_status.unit_id);
        } else if constexpr (std::is_same_v<T, WLEv::Message>) {
            ev.message.speaker  = relocate(ev.message.speaker);
            ev.message.portrait = relocate(ev.message.portrait);
            ev.message.text     = relocate(ev.message.text);
        } else if constexpr (std::is_same_v<T, WLEv::Story>) {
            ev.story.title      = relocate(ev.story.title);
            ev.story.text       = relocate(ev.story.text);
            ev.story.background = relocate(ev.story.background);
        } else if constexpr (std::is_same_v<T, WLEv::ObjectivesUpdate>) {
            ev.objectives_update.text = relocate(ev.objectives_update.text);
        } else if constexpr (std::is_same_v<T, WLEv::ChoiceNeeded>) {
            ev.choice_needed.prompt  = relocate(ev.choice_needed.prompt);
            for(int i = 0; i < ev.choice_needed.n_options; ++i)
                ev.choice_needed.options[i] = relocate(ev.choice_needed.options[i]);
            ev.choice_needed.speaker = relocate(ev.choice_needed.speaker);
        } else if constexpr (std::is_same_v<T, WLEv::Sound>) {
            ev.sound.path = relocate(ev.sound.path);
        } else if constexpr (std::is_same_v<T, WLEv::MusicChange>) {
            ev.music_change.path  = relocate(ev.music_change.path);
            ev.music_change.title = relocate(ev.music_change.title);
        }
    }, d);

    std::memcpy(buf.data(), &ev, ev_sz);
    return reinterpret_cast<WL_Event*>(buf.data());
}
