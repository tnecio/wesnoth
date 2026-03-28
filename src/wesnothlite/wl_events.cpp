/**
 * wl_events.cpp  —  Event materialisation
 *
 * Converts a WLEventInternal (the internal representation) into a WL_Event
 * written into a caller-supplied std::vector<char> buffer (engine-owned).
 * All const char* fields in the result point into the same contiguous block.
 * The returned pointer is valid until the next call to wl_materialize_event()
 * on the same buffer.  Do NOT pass it to wl_free().
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
    WLArena arena(256);

    /* Build a stack-local WL_Event, storing all strings into the arena.
     * After the switch, we allocate a contiguous buffer and relocate every
     * const char* from arena offsets to final buffer offsets. */
    WL_Event ev{};
    ev.type = d.type;

    auto store = [&](const std::string& s) -> const char* {
        return arena.store(s);
    };

    switch(d.type) {
    case WL_EVENT_SCENARIO_START:
        break;

    case WL_EVENT_SCENARIO_END:
        ev.scenario_end.outcome       = d.outcome;
        ev.scenario_end.next_scenario = store(d.s1);
        break;

    case WL_EVENT_TURN_START:
        ev.turn_start.turn = d.i1;
        break;

    case WL_EVENT_SIDE_TURN_START:
        ev.side_turn_start.side = d.i1;
        ev.side_turn_start.turn = d.i2;
        break;

    case WL_EVENT_SIDE_TURN_END:
        ev.side_turn_end.side = d.i1;
        ev.side_turn_end.turn = d.i2;
        break;

    case WL_EVENT_WAITING_FOR_INPUT:
        ev.waiting_for_input.side = d.i1;
        ev.waiting_for_input.turn = d.i2;
        break;

    case WL_EVENT_UNIT_MOVE: {
        ev.unit_move.unit_id  = store(d.s1);
        ev.unit_move.side     = d.i1;
        ev.unit_move.from     = d.loc1;
        ev.unit_move.to       = d.loc2;
        int n = std::min(static_cast<int>(d.path.size()), WL_MAX_PATH);
        for(int i = 0; i < n; ++i) ev.unit_move.path[i] = d.path[i];
        ev.unit_move.path_len = n;
        break;
    }

    case WL_EVENT_UNIT_ATTACK:
        ev.unit_attack.attacker_id     = store(d.s1);
        ev.unit_attack.defender_id     = store(d.s2);
        ev.unit_attack.attacker_side   = d.i1;
        ev.unit_attack.defender_side   = d.i2;
        ev.unit_attack.attacker_loc    = d.loc1;
        ev.unit_attack.defender_loc    = d.loc2;
        ev.unit_attack.attacker_result = d.cr_att;
        ev.unit_attack.defender_result = d.cr_def;
        {
            int n = std::min(static_cast<int>(d.blows.size()), WL_MAX_BLOWS);
            for(int i = 0; i < n; ++i) ev.unit_attack.blows[i] = d.blows[i];
            ev.unit_attack.n_blows = n;
        }
        break;

    case WL_EVENT_UNIT_SPAWN:
        ev.unit_spawn.unit_type_id = store(d.s1);
        ev.unit_spawn.unit_id      = store(d.s2);
        ev.unit_spawn.side         = d.i1;
        ev.unit_spawn.at           = d.loc1;
        break;

    case WL_EVENT_UNIT_DISMISS:
        ev.unit_dismiss.unit_id      = store(d.s1);
        ev.unit_dismiss.unit_type_id = store(d.s2);
        ev.unit_dismiss.side         = d.i1;
        break;

    case WL_EVENT_UNIT_DIE:
        ev.unit_die.unit_id      = store(d.s1);
        ev.unit_die.unit_type_id = store(d.s2);
        ev.unit_die.side         = d.i1;
        ev.unit_die.loc          = d.loc1;
        ev.unit_die.killer_id    = store(d.s3);
        break;

    case WL_EVENT_UNIT_ADVANCE:
        ev.unit_advance.unit_id      = store(d.s1);
        ev.unit_advance.side         = d.i1;
        ev.unit_advance.loc          = d.loc1;
        ev.unit_advance.from_type_id = store(d.s2);
        ev.unit_advance.to_type_id   = store(d.s3);
        break;

    case WL_EVENT_UNIT_XP:
        ev.unit_xp.unit_id   = store(d.s1);
        ev.unit_xp.side      = d.i1;
        ev.unit_xp.loc       = d.loc1;
        ev.unit_xp.xp_gained = d.i2;
        ev.unit_xp.xp_total  = d.i3;
        ev.unit_xp.xp_needed = d.i4;
        break;

    case WL_EVENT_UNIT_HEAL:
        ev.unit_heal.unit_id = store(d.s1);
        ev.unit_heal.side    = d.i1;
        ev.unit_heal.loc     = d.loc1;
        ev.unit_heal.amount  = d.i2;
        break;

    case WL_EVENT_UNIT_STATUS:
        ev.unit_status.unit_id = store(d.s1);
        ev.unit_status.side    = d.i1;
        ev.unit_status.loc     = d.loc1;
        ev.unit_status.flags   = d.status_flags;
        break;

    case WL_EVENT_VILLAGE_CAPTURE:
        ev.village_capture.loc      = d.loc1;
        ev.village_capture.old_side = d.i1;
        ev.village_capture.new_side = d.i2;
        break;

    case WL_EVENT_MESSAGE:
        ev.message.speaker  = store(d.s1);
        ev.message.portrait = store(d.s2);
        ev.message.text     = store(d.s3);
        break;

    case WL_EVENT_STORY:
        ev.story.title      = store(d.s1);
        ev.story.text       = store(d.s2);
        ev.story.background = store(d.s3);
        break;

    case WL_EVENT_OBJECTIVES_UPDATE:
        ev.objectives_update.side = d.i1;
        ev.objectives_update.text = store(d.s1);
        break;

    case WL_EVENT_CHOICE_NEEDED: {
        ev.choice_needed.kind   = d.choice_kind;
        ev.choice_needed.prompt = store(d.s1);
        int n = std::min(static_cast<int>(d.options.size()), WL_MAX_OPTIONS);
        for(int i = 0; i < n; ++i)
            ev.choice_needed.options[i] = store(d.options[i]);
        ev.choice_needed.n_options = n;
        ev.choice_needed.speaker   = store(d.s2);
        break;
    }

    case WL_EVENT_SOUND:
        ev.sound.path = store(d.s1);
        break;

    case WL_EVENT_MUSIC_CHANGE:
        ev.music_change.path  = store(d.s1);
        ev.music_change.title = store(d.s2);
        break;
    }

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

    switch(d.type) {
    case WL_EVENT_SCENARIO_END:
        ev.scenario_end.next_scenario = relocate(ev.scenario_end.next_scenario);
        break;
    case WL_EVENT_UNIT_MOVE:
        ev.unit_move.unit_id = relocate(ev.unit_move.unit_id);
        break;
    case WL_EVENT_UNIT_ATTACK:
        ev.unit_attack.attacker_id = relocate(ev.unit_attack.attacker_id);
        ev.unit_attack.defender_id = relocate(ev.unit_attack.defender_id);
        break;
    case WL_EVENT_UNIT_SPAWN:
        ev.unit_spawn.unit_type_id = relocate(ev.unit_spawn.unit_type_id);
        ev.unit_spawn.unit_id      = relocate(ev.unit_spawn.unit_id);
        break;
    case WL_EVENT_UNIT_DISMISS:
        ev.unit_dismiss.unit_id      = relocate(ev.unit_dismiss.unit_id);
        ev.unit_dismiss.unit_type_id = relocate(ev.unit_dismiss.unit_type_id);
        break;
    case WL_EVENT_UNIT_DIE:
        ev.unit_die.unit_id      = relocate(ev.unit_die.unit_id);
        ev.unit_die.unit_type_id = relocate(ev.unit_die.unit_type_id);
        ev.unit_die.killer_id    = relocate(ev.unit_die.killer_id);
        break;
    case WL_EVENT_UNIT_ADVANCE:
        ev.unit_advance.unit_id      = relocate(ev.unit_advance.unit_id);
        ev.unit_advance.from_type_id = relocate(ev.unit_advance.from_type_id);
        ev.unit_advance.to_type_id   = relocate(ev.unit_advance.to_type_id);
        break;
    case WL_EVENT_UNIT_XP:
        ev.unit_xp.unit_id = relocate(ev.unit_xp.unit_id);
        break;
    case WL_EVENT_UNIT_HEAL:
        ev.unit_heal.unit_id = relocate(ev.unit_heal.unit_id);
        break;
    case WL_EVENT_UNIT_STATUS:
        ev.unit_status.unit_id = relocate(ev.unit_status.unit_id);
        break;
    case WL_EVENT_MESSAGE:
        ev.message.speaker  = relocate(ev.message.speaker);
        ev.message.portrait = relocate(ev.message.portrait);
        ev.message.text     = relocate(ev.message.text);
        break;
    case WL_EVENT_STORY:
        ev.story.title      = relocate(ev.story.title);
        ev.story.text       = relocate(ev.story.text);
        ev.story.background = relocate(ev.story.background);
        break;
    case WL_EVENT_OBJECTIVES_UPDATE:
        ev.objectives_update.text = relocate(ev.objectives_update.text);
        break;
    case WL_EVENT_CHOICE_NEEDED:
        ev.choice_needed.prompt  = relocate(ev.choice_needed.prompt);
        for(int i = 0; i < ev.choice_needed.n_options; ++i)
            ev.choice_needed.options[i] = relocate(ev.choice_needed.options[i]);
        ev.choice_needed.speaker = relocate(ev.choice_needed.speaker);
        break;
    case WL_EVENT_SOUND:
        ev.sound.path = relocate(ev.sound.path);
        break;
    case WL_EVENT_MUSIC_CHANGE:
        ev.music_change.path  = relocate(ev.music_change.path);
        ev.music_change.title = relocate(ev.music_change.title);
        break;
    default:
        break;
    }

    std::memcpy(buf.data(), &ev, ev_sz);
    return reinterpret_cast<WL_Event*>(buf.data());
}
