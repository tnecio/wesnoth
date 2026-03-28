/**
 * wl_cli.cpp — command-line client for the WesnothLite library.
 *
 * Uses ONLY the public wesnothlite.h C API; no Wesnoth internals.
 *
 * Usage:
 *   wl-cli --data=PATH [--userdata=PATH]
 *           [--locale=LOCALE] [--translations=PATH]
 *           [--campaign=ID] [--scenario=ID] [--difficulty=D]
 *           [--save=FILE] [--ai-only] [--side=N]
 *
 * In interactive mode (default) the program prints each event and then,
 * when the engine is waiting for input, prompts for a command:
 *
 *   move X1,Y1 X2,Y2
 *   attack X1,Y1 X2,Y2 [weapon_index]
 *   recruit TYPE_ID X,Y
 *   recall UNIT_ID X,Y
 *   dismiss UNIT_ID
 *   end
 *   undo
 *   units           — list all units on map
 *   team [SIDE]     — show team info
 *   reach X,Y       — reachable hexes for unit at X,Y
 *   attacks X1,Y1 X2,Y2 — preview attack options
 *   info            — game info
 *   help
 */

#include "wesnothlite.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

/* =========================================================================
 * Tiny helper: parse "X,Y" into WL_Loc
 * ========================================================================= */
static bool parse_loc(const char* s, WL_Loc& out)
{
    int x = 0, y = 0;
    if(sscanf(s, "%d,%d", &x, &y) == 2) {
        out = { x, y };
        return true;
    }
    return false;
}

/* =========================================================================
 * Event printing
 * ========================================================================= */

static const char* outcome_name(WL_Outcome o)
{
    switch(o) {
    case WL_OUTCOME_VICTORY: return "VICTORY";
    case WL_OUTCOME_DEFEAT:  return "DEFEAT";
    case WL_OUTCOME_QUIT:    return "QUIT";
    default:                 return "NONE";
    }
}

static void print_event(const WL_Event* ev)
{
    switch(ev->type) {

    case WL_EVENT_SCENARIO_START:
        printf("[EVENT] scenario_start\n");
        break;

    case WL_EVENT_SCENARIO_END:
        printf("[EVENT] scenario_end outcome=%s next=%s\n",
               outcome_name(ev->scenario_end.outcome),
               ev->scenario_end.next_scenario ? ev->scenario_end.next_scenario : "(none)");
        break;

    case WL_EVENT_TURN_START:
        printf("[EVENT] turn_start turn=%d\n", ev->turn_start.turn);
        break;

    case WL_EVENT_SIDE_TURN_START:
        printf("[EVENT] side_turn_start side=%d turn=%d\n",
               ev->side_turn_start.side, ev->side_turn_start.turn);
        break;

    case WL_EVENT_SIDE_TURN_END:
        printf("[EVENT] side_turn_end side=%d turn=%d\n",
               ev->side_turn_end.side, ev->side_turn_end.turn);
        break;

    case WL_EVENT_WAITING_FOR_INPUT:
        printf("[EVENT] waiting_for_input side=%d turn=%d\n",
               ev->waiting_for_input.side, ev->waiting_for_input.turn);
        break;

    case WL_EVENT_UNIT_MOVE:
        printf("[EVENT] unit_move unit=%s side=%d from=(%d,%d) to=(%d,%d) path_len=%d\n",
               ev->unit_move.unit_id, ev->unit_move.side,
               ev->unit_move.from.x, ev->unit_move.from.y,
               ev->unit_move.to.x,   ev->unit_move.to.y,
               ev->unit_move.path_len);
        break;

    case WL_EVENT_UNIT_ATTACK: {
        const auto& a = ev->unit_attack;
        printf("[EVENT] unit_attack att=%s(%d)@(%d,%d) def=%s(%d)@(%d,%d) "
               "blows=%d att_hp=%d->%d def_hp=%d->%d\n",
               a.attacker_id, a.attacker_side,
               a.attacker_loc.x, a.attacker_loc.y,
               a.defender_id, a.defender_side,
               a.defender_loc.x, a.defender_loc.y,
               a.n_blows,
               a.attacker_result.hp_start, a.attacker_result.hp_end,
               a.defender_result.hp_start, a.defender_result.hp_end);
        break;
    }

    case WL_EVENT_UNIT_SPAWN:
        printf("[EVENT] unit_spawn type=%s id=%s side=%d at=(%d,%d)\n",
               ev->unit_spawn.unit_type_id, ev->unit_spawn.unit_id,
               ev->unit_spawn.side,
               ev->unit_spawn.at.x, ev->unit_spawn.at.y);
        break;

    case WL_EVENT_UNIT_DISMISS:
        printf("[EVENT] unit_dismiss id=%s type=%s side=%d\n",
               ev->unit_dismiss.unit_id, ev->unit_dismiss.unit_type_id,
               ev->unit_dismiss.side);
        break;

    case WL_EVENT_UNIT_DIE:
        printf("[EVENT] unit_die id=%s type=%s side=%d at=(%d,%d) killer=%s\n",
               ev->unit_die.unit_id, ev->unit_die.unit_type_id,
               ev->unit_die.side,
               ev->unit_die.loc.x, ev->unit_die.loc.y,
               ev->unit_die.killer_id ? ev->unit_die.killer_id : "(none)");
        break;

    case WL_EVENT_UNIT_ADVANCE:
        printf("[EVENT] unit_advance id=%s side=%d at=(%d,%d) %s -> %s\n",
               ev->unit_advance.unit_id, ev->unit_advance.side,
               ev->unit_advance.loc.x, ev->unit_advance.loc.y,
               ev->unit_advance.from_type_id,
               ev->unit_advance.to_type_id ? ev->unit_advance.to_type_id : "(mod)");
        break;

    case WL_EVENT_UNIT_XP:
        printf("[EVENT] unit_xp id=%s side=%d gained=%d xp=%d/%d\n",
               ev->unit_xp.unit_id, ev->unit_xp.side,
               ev->unit_xp.xp_gained, ev->unit_xp.xp_total, ev->unit_xp.xp_needed);
        break;

    case WL_EVENT_UNIT_HEAL:
        printf("[EVENT] unit_heal id=%s side=%d amount=%d\n",
               ev->unit_heal.unit_id, ev->unit_heal.side, ev->unit_heal.amount);
        break;

    case WL_EVENT_UNIT_STATUS:
        printf("[EVENT] unit_status id=%s side=%d flags=0x%x\n",
               ev->unit_status.unit_id, ev->unit_status.side,
               ev->unit_status.flags);
        break;

    case WL_EVENT_VILLAGE_CAPTURE:
        printf("[EVENT] village_capture at=(%d,%d) %d->%d\n",
               ev->village_capture.loc.x, ev->village_capture.loc.y,
               ev->village_capture.old_side, ev->village_capture.new_side);
        break;

    case WL_EVENT_MESSAGE:
        printf("[EVENT] message speaker=%s text=\"%s\"\n",
               ev->message.speaker, ev->message.text);
        break;

    case WL_EVENT_STORY:
        printf("[EVENT] story title=%s text=%s\n", ev->story.title ? ev->story.title : "", ev->story.text ? ev->story.text : "");
        break;

    case WL_EVENT_OBJECTIVES_UPDATE:
        printf("[EVENT] objectives_update side=%d\n", ev->objectives_update.side);
        break;

    case WL_EVENT_CHOICE_NEEDED:
        printf("[EVENT] choice_needed kind=%d prompt=\"%s\" options=%d\n",
               static_cast<int>(ev->choice_needed.kind),
               ev->choice_needed.prompt ? ev->choice_needed.prompt : "",
               ev->choice_needed.n_options);
        for(int i = 0; i < ev->choice_needed.n_options; ++i)
            printf("         [%d] %s\n", i, ev->choice_needed.options[i]);
        break;

    case WL_EVENT_SOUND:
        printf("[EVENT] sound path=%s\n", ev->sound.path);
        break;

	case WL_EVENT_MUSIC_CHANGE:
		printf("[EVENT] music_change title=%s path=%s\n",
			ev->music_change.title ? ev->music_change.title : ev->music_change.path,
			ev->music_change.path ? ev->music_change.path : "");
		break;
	}
    fflush(stdout);
}

/* =========================================================================
 * Status code → string
 * ========================================================================= */
static const char* status_str(WL_Status s)
{
    switch(s) {
    case WL_OK:           return "OK";
    case WL_ERR_GENERIC:  return "ERR_GENERIC";
    case WL_ERR_INVALID:  return "ERR_INVALID";
    case WL_ERR_NO_GAME:  return "ERR_NO_GAME";
    case WL_ERR_NOT_TURN: return "ERR_NOT_TURN";
    case WL_ERR_BLOCKED:  return "ERR_BLOCKED";
    case WL_ERR_NO_PATH:  return "ERR_NO_PATH";
    case WL_ERR_NO_GOLD:  return "ERR_NO_GOLD";
    case WL_ERR_NO_SPACE: return "ERR_NO_SPACE";
    case WL_ERR_UNKNOWN:  return "ERR_UNKNOWN";
    default:              return "?";
    }
}

/* =========================================================================
 * Interactive query commands
 * ========================================================================= */
static void cmd_info(WL_Engine* eng)
{
    WL_GameInfo* g = wl_query_game(eng);
    if(!g) { printf("(no game)\n"); return; }
    printf("scenario: %s (%s)  turn: %d/%d  side: %d/%d  outcome: %s\n",
           g->scenario_name, g->scenario_id,
           g->turn, g->max_turns,
           g->current_side, g->n_sides,
           outcome_name(g->outcome));
    printf("tod: %s (lawful_bonus: %+d)\n", g->tod.name, g->tod.lawful_bonus);
    wl_free(g);
}

static void cmd_units(WL_Engine* eng)
{
    WL_UnitList* ul = wl_query_units(eng);
    if(!ul) { printf("(no units)\n"); return; }
    for(int i = 0; i < ul->count; ++i) {
        const WL_Unit& u = ul->units[i];
        printf("  [%d] %s (%s) side=%d at=(%d,%d) hp=%d/%d xp=%d/%d moves=%d/%d\n",
               i, u.id, u.type_id, u.side,
               u.loc.x, u.loc.y,
               u.hp, u.max_hp,
               u.xp, u.max_xp,
               u.moves, u.max_moves);
    }
    wl_free(ul);
}

static void cmd_team(WL_Engine* eng, int side)
{
    WL_Team* t = wl_query_team(eng, side);
    if(!t) { printf("(no team)\n"); return; }
    printf("  side=%d name=%s gold=%d income=%d villages=%d\n",
           t->side, t->name, t->gold, t->income, t->n_villages);
    wl_free(t);
}

static void cmd_reach(WL_Engine* eng, WL_Loc loc)
{
    WL_ReachList* rl = wl_query_reach(eng, loc);
    if(!rl) { printf("(no reach)\n"); return; }
    printf("  %d reachable hexes from (%d,%d):\n", rl->count, loc.x, loc.y);
    for(int i = 0; i < rl->count; ++i) {
        const WL_ReachHex& h = rl->hexes[i];
        printf("    (%d,%d) moves_left=%d def=%d%% can_attack=%d\n",
               h.loc.x, h.loc.y, h.moves_left, h.defense, h.can_attack);
    }
    wl_free(rl);
}

static void cmd_attacks(WL_Engine* eng, WL_Loc att, WL_Loc def)
{
    WL_AttackOptionList* al = wl_query_attack_options(eng, att, def);
    if(!al) { printf("(no attack options)\n"); return; }
    printf("  %d attack options (default=%d):\n", al->count, al->default_option);
    for(int i = 0; i < al->count; ++i) {
        const WL_AttackOption& o = al->options[i];
        printf("  [%d] att_wpn=%d  %dd%d (%d%% hit, ~%d dmg)  "
               "def_wpn=%d  %dd%d (%d%% hit, ~%d dmg)\n",
               i,
               o.attacker_weapon_index,
               o.attacker.num_blows, o.attacker.damage,
               o.attacker.chance_to_hit, o.attacker.expected_damage,
               o.defender_weapon_index,
               o.defender.num_blows, o.defender.damage,
               o.defender.chance_to_hit, o.defender.expected_damage);
    }
    wl_free(al);
}

static void print_help()
{
    printf(
        "Commands:\n"
        "  move X1,Y1 X2,Y2              Move unit from (X1,Y1) to (X2,Y2)\n"
        "  attack X1,Y1 X2,Y2 [WPN]      Attack with unit at (X1,Y1) against (X2,Y2)\n"
        "  recruit TYPE X,Y              Recruit unit type at hex X,Y\n"
        "  recall ID X,Y                 Recall unit by id at hex X,Y\n"
        "  dismiss ID                    Dismiss unit by id from recall list\n"
        "  end                           End current side's turn\n"
        "  undo                          Undo last action\n"
        "  choose N                      Pick option N for pending choice\n"
        "  units                         List all units on map\n"
        "  team [SIDE]                   Show team info (default: current side)\n"
        "  reach X,Y                     Show reachable hexes for unit at X,Y\n"
        "  attacks X1,Y1 X2,Y2           Preview attack options\n"
        "  info                          Show game info\n"
        "  help                          This message\n"
        "  quit                          Exit\n");
}

/* =========================================================================
 * Interactive command dispatch
 * ========================================================================= */
static bool dispatch_command(WL_Engine* eng, const std::string& line,
                              bool& quit, bool* /*pending_choice*/)
{
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if(cmd.empty()) return true;

    if(cmd == "quit" || cmd == "exit") {
        quit = true;
        return true;
    }

    if(cmd == "help") { print_help(); return true; }
    if(cmd == "info") { cmd_info(eng); return true; }
    if(cmd == "units") { cmd_units(eng); return true; }

    if(cmd == "team") {
        int side = 0;
        ss >> side;
        if(side == 0) {
            WL_GameInfo* g = wl_query_game(eng);
            if(g) { side = g->current_side; wl_free(g); }
        }
        cmd_team(eng, side);
        return true;
    }

    if(cmd == "reach") {
        std::string lstr; ss >> lstr;
        WL_Loc loc{};
        if(!parse_loc(lstr.c_str(), loc)) { printf("usage: reach X,Y\n"); return true; }
        cmd_reach(eng, loc);
        return true;
    }

    if(cmd == "attacks") {
        std::string l1, l2; ss >> l1 >> l2;
        WL_Loc att{}, def{};
        if(!parse_loc(l1.c_str(), att) || !parse_loc(l2.c_str(), def)) {
            printf("usage: attacks X1,Y1 X2,Y2\n"); return true;
        }
        cmd_attacks(eng, att, def);
        return true;
    }

    if(cmd == "move") {
        std::string l1, l2; ss >> l1 >> l2;
        WL_Loc from{}, to{};
        if(!parse_loc(l1.c_str(), from) || !parse_loc(l2.c_str(), to)) {
            printf("usage: move X1,Y1 X2,Y2\n"); return true;
        }
        WL_Command c{}; c.type = WL_CMD_MOVE; c.move.from = from; c.move.to = to;
        WL_Status r = wl_send(eng, &c);
        printf("move -> %s\n", status_str(r));
        return true;
    }

    if(cmd == "attack") {
        std::string l1, l2; ss >> l1 >> l2;
        int wpn = 0; ss >> wpn;
        WL_Loc att{}, def{};
        if(!parse_loc(l1.c_str(), att) || !parse_loc(l2.c_str(), def)) {
            printf("usage: attack X1,Y1 X2,Y2 [weapon_index]\n"); return true;
        }
        WL_Command c{}; c.type = WL_CMD_ATTACK; c.attack.att = att; c.attack.def = def; c.attack.weapon = wpn;
        WL_Status r = wl_send(eng, &c);
        printf("attack -> %s\n", status_str(r));
        return true;
    }

    if(cmd == "recruit") {
        std::string type, lstr; ss >> type >> lstr;
        WL_Loc at{};
        if(type.empty() || !parse_loc(lstr.c_str(), at)) {
            printf("usage: recruit TYPE X,Y\n"); return true;
        }
        WL_Command c{}; c.type = WL_CMD_RECRUIT; c.recruit.type_id = type.c_str(); c.recruit.at = at;
        WL_Status r = wl_send(eng, &c);
        printf("recruit -> %s\n", status_str(r));
        return true;
    }

    if(cmd == "recall") {
        std::string id, lstr; ss >> id >> lstr;
        WL_Loc at{};
        if(id.empty() || !parse_loc(lstr.c_str(), at)) {
            printf("usage: recall ID X,Y\n"); return true;
        }
        WL_Command c{}; c.type = WL_CMD_RECALL; c.recall.unit_id = id.c_str(); c.recall.at = at;
        WL_Status r = wl_send(eng, &c);
        printf("recall -> %s\n", status_str(r));
        return true;
    }

    if(cmd == "dismiss") {
        std::string id; ss >> id;
        if(id.empty()) { printf("usage: dismiss ID\n"); return true; }
        WL_Command c{}; c.type = WL_CMD_DISMISS; c.dismiss.unit_id = id.c_str();
        WL_Status r = wl_send(eng, &c);
        printf("dismiss -> %s\n", status_str(r));
        return true;
    }

    if(cmd == "end") {
        WL_Command c{}; c.type = WL_CMD_END_TURN;
        WL_Status r = wl_send(eng, &c);
        printf("end_turn -> %s\n", status_str(r));
        return true;
    }

    if(cmd == "undo") {
        WL_Command c{}; c.type = WL_CMD_UNDO;
        WL_Status r = wl_send(eng, &c);
        printf("undo -> %s\n", status_str(r));
        return true;
    }

    if(cmd == "choose") {
        int idx = 0; ss >> idx;
        WL_Command c{}; c.type = WL_CMD_CHOOSE; c.choose.option = idx;
        WL_Status r = wl_send(eng, &c);
        printf("choose -> %s\n", status_str(r));
        return true;
    }

    printf("unknown command: %s  (type 'help')\n", cmd.c_str());
    return true;
}

/* =========================================================================
 * Main
 * ========================================================================= */
static void usage_and_exit()
{
    fprintf(stderr,
        "Usage: wl-cli --data=PATH [options]\n"
        "  --data=PATH         Wesnoth data directory (required)\n"
        "  --userdata=PATH     Save/preferences dir (default: /tmp/wesnoth_wl)\n"
        "  --locale=LOCALE     POSIX locale for translated strings, e.g. pl_PL\n"
        "                      (default: system locale)\n"
        "  --translations=PATH Directory containing <locale>/LC_MESSAGES/*.mo files\n"
        "                      (default: <data>/../translations)\n"
        "  --campaign=ID       Campaign id (default: Tutorial)\n"
        "  --scenario=ID       Scenario id (skip to specific scenario)\n"
        "  --difficulty=D      Difficulty (default: NORMAL)\n"
        "  --save=FILE         Load save file instead of starting fresh\n"
        "  --side=N            Human side number (default: 1; 0 = ai-only)\n"
        "  --ai-only           Shorthand for --side=0\n");
    exit(1);
}

int main(int argc, char** argv)
{
    std::string data_path;
    std::string userdata      = "/tmp/wesnoth_wl";
    std::string locale;
    std::string translations;
    std::string campaign      = "Tutorial";
    std::string scenario;
    std::string difficulty    = "NORMAL";
    std::string save_file;
    int human_side = 1;  /* 0 = fully ai-only */

    for(int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto eq = arg.find('=');
        std::string key = (eq != std::string::npos) ? arg.substr(0, eq) : arg;
        std::string val = (eq != std::string::npos) ? arg.substr(eq + 1) : "";

        if     (key == "--data")         data_path    = val;
        else if(key == "--userdata")     userdata     = val;
        else if(key == "--locale")       locale       = val;
        else if(key == "--translations") translations = val;
        else if(key == "--campaign")     campaign     = val;
        else if(key == "--scenario")     scenario     = val;
        else if(key == "--difficulty")   difficulty   = val;
        else if(key == "--save")         save_file    = val;
        else if(key == "--side")         human_side   = std::stoi(val);
        else if(key == "--ai-only")      human_side   = 0;
        else if(key == "--help" || key == "-h") usage_and_exit();
        else { fprintf(stderr, "unknown argument: %s\n", arg.c_str()); usage_and_exit(); }
    }

    if(data_path.empty()) {
        fprintf(stderr, "[ERROR] --data=PATH is required\n");
        usage_and_exit();
    }

    /* ── Init ── */
    WL_Engine* eng = wl_init(data_path.c_str(), userdata.c_str());
    if(!eng) {
        fprintf(stderr, "[FATAL] wl_init failed\n");
        return 1;
    }

    /* ── Locale (optional) ── */
    if(!locale.empty() || !translations.empty()) {
        wl_set_locale(eng,
                      locale.empty()       ? nullptr : locale.c_str(),
                      translations.empty() ? nullptr : translations.c_str());
    }

    /* ── Load scenario ── */
    WL_Status st;
    if(!save_file.empty()) {
        st = wl_load_save(eng, save_file.c_str());
    } else if(!scenario.empty()) {
        st = wl_start_scenario(eng, campaign.c_str(), scenario.c_str(), difficulty.c_str());
    } else {
        st = wl_start_campaign(eng, campaign.c_str(), difficulty.c_str());
    }

    if(st != WL_OK) {
        fprintf(stderr, "[FATAL] load failed: %s — %s\n",
                status_str(st), wl_last_error(eng));
        wl_shutdown(eng);
        return 1;
    }

    /* ── Event / action loop ── */
    bool quit = false;
    bool game_over = false;
    bool waiting = false;     /* true when wl_step returned NULL */
    bool pending_choice = false;

    while(!quit && !game_over) {

        /* Drain all pending events first. */
        while(!waiting) {
            const WL_Event* ev = wl_step(eng);
            if(!ev) {
                /* Engine is waiting for human input. */
                waiting = true;
                break;
            }
            print_event(ev);

            if(ev->type == WL_EVENT_SCENARIO_END) {
                game_over = true;
                break;
            }
            if(ev->type == WL_EVENT_CHOICE_NEEDED) {
                pending_choice = true;
                waiting = true;
                break;
            }
            if(ev->type == WL_EVENT_WAITING_FOR_INPUT) {
                waiting = true;
                break;
            }
        }

        if(game_over || quit) break;

        /* ── Decide what to do while waiting ── */
        WL_GameInfo* gi = wl_query_game(eng);
        int cur_side = gi ? gi->current_side : 0;
        wl_free(gi);

        bool is_human_turn = (human_side != 0 && cur_side == human_side);

        if(!is_human_turn) {
            /* AI side or ai-only mode: just end the turn. */
            if(pending_choice) {
                WL_Command c{}; c.type = WL_CMD_CHOOSE; c.choose.option = 0;
                wl_send(eng, &c);
                pending_choice = false;
            } else {
                WL_Command c{}; c.type = WL_CMD_END_TURN;
                wl_send(eng, &c);
            }
            waiting = false;
        } else {
            /* Interactive: prompt for a command. */
            printf("> "); fflush(stdout);
            std::string line;
            if(!std::getline(std::cin, line)) break;  /* EOF */
            dispatch_command(eng, line, quit, &pending_choice);
            /* Stay in waiting state until user issues an action that
             * unblocks the game thread (move/attack/end_turn/etc.).
             * wl_step() will return events again after an action succeeds.
             */
            /* Check if the game thread is still waiting. */
            const WL_Event* probe = wl_step(eng);
            if(probe) {
                print_event(probe);
                if(probe->type == WL_EVENT_SCENARIO_END) game_over = true;
                waiting = false;  /* more events in flight */
            }
            /* else still waiting — loop back to prompt */
        }
    }

    wl_shutdown(eng);
    return 0;
}
