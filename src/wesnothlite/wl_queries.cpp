/**
 * wl_queries.cpp  —  Read-only game-state query functions
 *
 * All wl_query_* functions plus wl_list_campaigns.  These functions inspect
 * engine / gameboard state and return heap-allocated snapshots that the caller
 * frees with wl_free().
 *
 * Helper functions (fill_wl_unit, fill_wl_attack_pub, fill_resistance_type,
 * terrain_category, resolve_img) are defined in wl_snapshots.cpp and declared
 * via wl_impl.hpp.
 */

#include "wl_impl.hpp"

#include "actions/attack.hpp"
#include "filesystem.hpp"
#include "game_board.hpp"
#include "game_config_manager.hpp"
#include "game_data.hpp"
#include "game_state.hpp"
#include "play_controller.hpp"
#include "saved_game.hpp"
#include "map/location.hpp"
#include "map/map.hpp"
#include "pathfind/pathfind.hpp"
#include "recall_list_manager.hpp"
#include "resources.hpp"
#include "serialization/string_utils.hpp"
#include "team.hpp"
#include "terrain/terrain.hpp"
#include "terrain/translation.hpp"
#include "tod_manager.hpp"
#include "units/map.hpp"
#include "units/types.hpp"
#include "units/unit.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

/* =========================================================================
 * Relocation helper — used throughout this file
 * Given an arena and a final buffer, patches a raw arena-relative pointer
 * to point into the final allocation.
 * ========================================================================= */

namespace {

inline const char* rel_ptr(const char* p, const WLArena& arena,
                             const char* str_base, size_t str_sz)
{
    if(!p || arena.buf.empty()) return nullptr;
    ptrdiff_t off = p - arena.buf.data();
    if(off < 0 || static_cast<size_t>(off) >= str_sz) return nullptr;
    return str_base + off;
}

/* Relocate all string pointers inside a WL_Unit that was filled against
 * arena into the final contiguous buffer at str_base. */
void relocate_unit(WL_Unit& u, const WLArena& arena,
                   const char* str_base, size_t str_sz)
{
    auto rel = [&](const char* p){ return rel_ptr(p, arena, str_base, str_sz); };

    u.id       = rel(u.id);
    u.type_id  = rel(u.type_id);
    u.name     = rel(u.name);
    u.portrait = rel(u.portrait);
    u.sprite   = rel(u.sprite);
    for(int a = 0; a < u.n_attacks; ++a) {
        u.attacks[a].id            = rel(u.attacks[a].id);
        u.attacks[a].name          = rel(u.attacks[a].name);
        u.attacks[a].icon          = rel(u.attacks[a].icon);
        u.attacks[a].specials_desc = rel(u.attacks[a].specials_desc);
    }
    for(int i = 0; i < u.n_traits;    ++i) u.traits[i]      = rel(u.traits[i]);
    for(int i = 0; i < u.n_abilities;  ++i) u.abilities[i]   = rel(u.abilities[i]);
    for(int i = 0; i < u.n_advances;   ++i) u.advances_to[i] = rel(u.advances_to[i]);
}

/* Build a WL_UnitList from a pre-filled vector of WL_Unit and the arena
 * they were filled into.  Allocates a single contiguous block. */
WL_UnitList* build_unit_list(std::vector<WL_Unit>& tmp, int n, WLArena& arena)
{
    size_t list_sz  = sizeof(WL_UnitList);
    size_t items_sz = static_cast<size_t>(n) * sizeof(WL_Unit);
    size_t str_sz   = arena.buf.size();
    char*  buf      = static_cast<char*>(std::malloc(list_sz + items_sz + str_sz));
    if(!buf) return nullptr;

    char* str_base = buf + list_sz + items_sz;
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    WL_UnitList* list  = reinterpret_cast<WL_UnitList*>(buf);
    WL_Unit*     items = reinterpret_cast<WL_Unit*>(buf + list_sz);
    list->units = items;
    list->count = n;

    for(int i = 0; i < n; ++i) {
        items[i] = tmp[static_cast<size_t>(i)];
        relocate_unit(items[i], arena, str_base, str_sz);
    }
    return list;
}

} // anonymous namespace

/* =========================================================================
 * wl_list_campaigns
 * ========================================================================= */

WL_CampaignList* wl_list_campaigns(WL_Engine* engine)
{
    if(!engine || !engine->impl->initialized) return nullptr;
    WLEngineImpl& e = *engine->impl;

    std::vector<const config*> campaigns;
    for(const config& c :
            e.config_manager->game_config().child_range("campaign"))
        campaigns.push_back(&c);

    WLArena arena(campaigns.size() * 512);

    struct CInfo {
        const char *id, *name, *desc, *image, *icon, *first;
        const char* diffs[8];
        int n_diffs;
    };
    std::vector<CInfo> infos;
    infos.reserve(campaigns.size());

    for(const config* c : campaigns) {
        CInfo ci{};
        ci.id    = arena.store((*c)["id"].str());
        ci.name  = arena.store((*c)["name"].str());
        ci.desc  = arena.store((*c)["description"].str());
        ci.image = arena.store((*c)["image"].str());
        ci.icon  = arena.store((*c)["icon"].str());
        ci.first = arena.store((*c)["first_scenario"].str());

        std::string dstr = (*c)["difficulties"].str();
        if(dstr.empty()) {
            ci.diffs[0] = arena.store("NORMAL");
            ci.n_diffs  = 1;
        } else {
            for(const std::string& d : utils::split(dstr)) {
                if(ci.n_diffs >= 8) break;
                ci.diffs[ci.n_diffs++] = arena.store(d);
            }
        }
        infos.push_back(ci);
    }

    int    n        = static_cast<int>(infos.size());
    size_t list_sz  = sizeof(WL_CampaignList);
    size_t items_sz = static_cast<size_t>(n) * sizeof(WL_CampaignInfo);
    size_t str_sz   = arena.buf.size();
    char*  buf      = static_cast<char*>(std::malloc(list_sz + items_sz + str_sz));
    if(!buf) return nullptr;

    char* str_base = buf + list_sz + items_sz;
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        return rel_ptr(p, arena, str_base, str_sz);
    };

    WL_CampaignList* list  = reinterpret_cast<WL_CampaignList*>(buf);
    WL_CampaignInfo* items = reinterpret_cast<WL_CampaignInfo*>(buf + list_sz);
    list->campaigns = items;
    list->count     = n;

    for(int i = 0; i < n; ++i) {
        const CInfo& ci = infos[static_cast<size_t>(i)];
        WL_CampaignInfo& item = items[i];
        item.id             = rel(ci.id);
        item.name           = rel(ci.name);
        item.description    = rel(ci.desc);
        item.image          = rel(ci.image);
        item.icon           = rel(ci.icon);
        item.first_scenario = rel(ci.first);
        item.n_difficulties = ci.n_diffs;
        for(int j = 0; j < ci.n_diffs; ++j)
            item.difficulties[j] = rel(ci.diffs[j]);
    }
    return list;
}

/* =========================================================================
 * wl_query_game
 * ========================================================================= */

WL_GameInfo* wl_query_game(WL_Engine* engine)
{
    if(!engine || !resources::gameboard) return nullptr;
    WLEngineImpl& e = *engine->impl;

    WLArena arena(512);
    WL_GameInfo gi{};

    gi.phase        = WL_PHASE_PLAYING;
    gi.turn         = resources::tod_manager
                          ? resources::tod_manager->turn() : 0;
    gi.max_turns    = resources::tod_manager
                          ? resources::tod_manager->number_of_turns() : 0;
    gi.current_side = resources::controller
                          ? resources::controller->current_side() : 0;
    gi.n_sides      = static_cast<int>(resources::gameboard->teams().size());
    gi.outcome      = WL_OUTCOME_NONE;

    if(resources::tod_manager) {
        const time_of_day& tod = resources::tod_manager->get_time_of_day();
        gi.tod.id           = arena.store(tod.id);
        gi.tod.name         = arena.store(tod.name.str());
        gi.tod.lawful_bonus = tod.lawful_bonus;
        gi.tod.image        = arena.store(tod.image);
        gi.tod.mask_image   = arena.store(tod.image_mask);
    }

    gi.scenario_id   = arena.store(e.state ? e.state->get_scenario_id() : "");
    gi.scenario_name = arena.store(
        resources::gamedata
            ? resources::gamedata->get_variable("scenario_name").str() : "");
    gi.campaign_id   = arena.store(
        e.state ? e.state->classification().campaign : "");
    gi.campaign_name = arena.store("");
    gi.difficulty    = arena.store(
        e.state ? e.state->classification().difficulty : "");

    size_t str_sz = arena.buf.size();
    char* buf = static_cast<char*>(std::malloc(sizeof(WL_GameInfo) + str_sz));
    if(!buf) return nullptr;
    char* str_base = buf + sizeof(WL_GameInfo);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        return rel_ptr(p, arena, str_base, str_sz);
    };

    gi.tod.id         = rel(gi.tod.id);
    gi.tod.name       = rel(gi.tod.name);
    gi.tod.image      = rel(gi.tod.image);
    gi.tod.mask_image = rel(gi.tod.mask_image);
    gi.scenario_id    = rel(gi.scenario_id);
    gi.scenario_name  = rel(gi.scenario_name);
    gi.campaign_id    = rel(gi.campaign_id);
    gi.campaign_name  = rel(gi.campaign_name);
    gi.difficulty     = rel(gi.difficulty);

    std::memcpy(buf, &gi, sizeof(WL_GameInfo));
    return reinterpret_cast<WL_GameInfo*>(buf);
}

/* =========================================================================
 * wl_query_map
 * ========================================================================= */

WL_MapData* wl_query_map(WL_Engine* engine)
{
    if(!engine || !resources::gameboard) return nullptr;

    const gamemap& m = resources::gameboard->map();
    int W = m.w(), H = m.h();

    /* Pre-scan string lengths so the arena doesn't reallocate mid-loop
     * (a reallocation would invalidate raw const char* pointers). */
    size_t arena_reserve = 0;
    for(int y = 0; y < H; ++y) {
        for(int x = 0; x < W; ++x) {
            auto tc = m.get_terrain(map_location(x, y));
            const terrain_type& tt =
                resources::gameboard->map().get_terrain_info(tc);
            arena_reserve += tt.id().size() + 1;
            arena_reserve += tt.name().str().size() + 1;
            arena_reserve += resolve_img(tt.editor_image()).size() + 1;
            t_translation::terrain_code ov_tc(t_translation::NO_LAYER, tc.overlay);
            const std::string ov_img =
                resources::gameboard->map().get_terrain_info(ov_tc).editor_image();
            arena_reserve += (ov_img.empty() ? 0 : resolve_img(ov_img).size()) + 1;
        }
    }
    WLArena arena(arena_reserve);

    std::vector<WL_Terrain> terrains(static_cast<size_t>(W * H));
    for(int y = 0; y < H; ++y) {
        for(int x = 0; x < W; ++x) {
            map_location loc(x, y);
            WL_Terrain& t = terrains[static_cast<size_t>(y * W + x)];
            t.loc = { x + 1, y + 1 };

            auto tc = m.get_terrain(loc);
            const terrain_type& tt =
                resources::gameboard->map().get_terrain_info(tc);

            t.category     = terrain_category(tt);
            t.id           = arena.store(tt.id());
            t.name         = arena.store(tt.name().str());
            t.icon         = arena.store(resolve_img(tt.editor_image()));
            {
                t_translation::terrain_code ov_tc(t_translation::NO_LAYER, tc.overlay);
                const terrain_type& ov_tt =
                    resources::gameboard->map().get_terrain_info(ov_tc);
                const std::string ov_img = ov_tt.editor_image();
                t.overlay_icon = arena.store(
                    ov_img.empty() ? std::string{} : resolve_img(ov_img));
            }
            t.village_side = m.is_village(loc)
                                 ? resources::gameboard->village_owner(loc) + 1
                                 : 0;
            t.starting_side = 0;
            for(int s = 1; s <= static_cast<int>(resources::gameboard->teams().size()); ++s) {
                if(m.starting_position(s) == loc) { t.starting_side = s; break; }
            }
            t.is_keep = tt.is_keep() ? 1 : 0;
        }
    }

    size_t hdr_sz  = sizeof(WL_MapData);
    size_t arr_sz  = static_cast<size_t>(W * H) * sizeof(WL_Terrain);
    size_t str_sz  = arena.buf.size();
    char* buf = static_cast<char*>(std::malloc(hdr_sz + arr_sz + str_sz));
    if(!buf) return nullptr;

    char* str_base = buf + hdr_sz + arr_sz;
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        return rel_ptr(p, arena, str_base, str_sz);
    };

    WL_MapData* out = reinterpret_cast<WL_MapData*>(buf);
    out->width  = W;
    out->height = H;

    WL_Terrain* hex_arr = reinterpret_cast<WL_Terrain*>(buf + hdr_sz);
    for(int i = 0; i < W * H; ++i) {
        hex_arr[i] = terrains[static_cast<size_t>(i)];
        hex_arr[i].id           = rel(hex_arr[i].id);
        hex_arr[i].name         = rel(hex_arr[i].name);
        hex_arr[i].icon         = rel(hex_arr[i].icon);
        hex_arr[i].overlay_icon = rel(hex_arr[i].overlay_icon);
    }
    return out;
}

/* =========================================================================
 * wl_query_visibility
 * ========================================================================= */

WL_VisibilityMap* wl_query_visibility(WL_Engine* engine, int side)
{
    if(!engine || !resources::gameboard) return nullptr;
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > static_cast<int>(teams.size())) return nullptr;

    const gamemap& m = resources::gameboard->map();
    int W = m.w(), H = m.h();
    int n = W * H;

    size_t hdr_sz = sizeof(WL_VisibilityMap);
    size_t val_sz = static_cast<size_t>(n) * sizeof(WL_Visibility);
    char* buf = static_cast<char*>(std::malloc(hdr_sz + val_sz));
    if(!buf) return nullptr;

    WL_VisibilityMap* out = reinterpret_cast<WL_VisibilityMap*>(buf);
    out->side   = side;
    out->width  = W;
    out->height = H;

    const team& t = teams[static_cast<size_t>(side - 1)];
    WL_Visibility* vals = reinterpret_cast<WL_Visibility*>(buf + hdr_sz);
    for(int y = 0; y < H; ++y) {
        for(int x = 0; x < W; ++x) {
            map_location loc(x, y);
            WL_Visibility v = WL_VIS_VISIBLE;
            if(t.shrouded(loc))     v = WL_VIS_SHROUDED;
            else if(t.fogged(loc))  v = WL_VIS_FOGGED;
            vals[y * W + x] = v;
        }
    }
    return out;
}

/* =========================================================================
 * wl_query_units
 * ========================================================================= */

WL_UnitList* wl_query_units(WL_Engine* engine)
{
    if(!engine || !resources::gameboard) return nullptr;
    const unit_map& units = resources::gameboard->units();

    WLArena arena(units.size() * 256);
    std::vector<WL_Unit> tmp(units.size());
    int n = 0;
    for(const unit& u : units)
        fill_wl_unit(tmp[static_cast<size_t>(n++)], u, arena);

    return build_unit_list(tmp, n, arena);
}

/* =========================================================================
 * wl_query_unit_at
 * ========================================================================= */

WL_Unit* wl_query_unit_at(WL_Engine* engine, WL_Loc loc)
{
    if(!engine || !resources::gameboard) return nullptr;
    map_location ml(loc.x - 1, loc.y - 1);
    auto it = resources::gameboard->units().find(ml);
    if(it == resources::gameboard->units().end()) return nullptr;

    WLArena arena(256);
    WL_Unit tmp{};
    fill_wl_unit(tmp, *it, arena);

    size_t str_sz = arena.buf.size();
    char* buf = static_cast<char*>(std::malloc(sizeof(WL_Unit) + str_sz));
    if(!buf) return nullptr;

    char* str_base = buf + sizeof(WL_Unit);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);
    relocate_unit(tmp, arena, str_base, str_sz);

    std::memcpy(buf, &tmp, sizeof(WL_Unit));
    return reinterpret_cast<WL_Unit*>(buf);
}

/* =========================================================================
 * wl_query_unit_type
 * ========================================================================= */

WL_UnitType* wl_query_unit_type(WL_Engine* engine, const char* type_id)
{
    if(!engine || !type_id) return nullptr;

    const unit_type* ut = unit_types.find(type_id);
    if(!ut) return nullptr;

    WLArena arena(512);
    WL_UnitType tmp{};

    tmp.type_id     = arena.store(ut->id());
    tmp.name        = arena.store(ut->type_name().str());
    tmp.description = arena.store(ut->unit_description().str());
    tmp.portrait    = arena.store(ut->big_profile());
    tmp.sprite      = arena.store(ut->image());
    tmp.race        = arena.store(ut->race_id());
    tmp.max_hp      = ut->hitpoints();
    tmp.max_moves   = ut->movement();
    tmp.max_xp      = ut->experience_needed();
    tmp.level       = ut->level();
    tmp.alignment   = static_cast<WL_Alignment>(ut->alignment());
    tmp.cost        = ut->cost();
    tmp.recall_cost = ut->recall_cost();

    fill_resistance_type(tmp.resistance, *ut);

    int na = 0;
    for(const attack_type& atk : ut->attacks()) {
        if(na >= 8) break;
        fill_wl_attack_pub(tmp.attacks[na++], atk, arena);
    }
    tmp.n_attacks = na;

    int nab = 0;
    for(const auto& ab : ut->abilities_cfg().all_children_range()) {
        if(nab >= 8) break;
        tmp.abilities[nab++] = arena.store(ab.cfg["id"].str());
    }
    tmp.n_abilities = nab;

    int nav = 0;
    for(const std::string& adv : ut->advances_to()) {
        if(nav >= 8) break;
        tmp.advances_to[nav++] = arena.store(adv);
    }
    tmp.n_advances = nav;

    size_t str_sz = arena.buf.size();
    char* buf = static_cast<char*>(std::malloc(sizeof(WL_UnitType) + str_sz));
    if(!buf) return nullptr;

    char* str_base = buf + sizeof(WL_UnitType);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        return rel_ptr(p, arena, str_base, str_sz);
    };

    tmp.type_id     = rel(tmp.type_id);
    tmp.name        = rel(tmp.name);
    tmp.description = rel(tmp.description);
    tmp.portrait    = rel(tmp.portrait);
    tmp.sprite      = rel(tmp.sprite);
    tmp.race        = rel(tmp.race);
    for(int i = 0; i < tmp.n_attacks; ++i) {
        tmp.attacks[i].id            = rel(tmp.attacks[i].id);
        tmp.attacks[i].name          = rel(tmp.attacks[i].name);
        tmp.attacks[i].icon          = rel(tmp.attacks[i].icon);
        tmp.attacks[i].specials_desc = rel(tmp.attacks[i].specials_desc);
    }
    for(int i = 0; i < tmp.n_abilities; ++i) tmp.abilities[i]   = rel(tmp.abilities[i]);
    for(int i = 0; i < tmp.n_advances;  ++i) tmp.advances_to[i] = rel(tmp.advances_to[i]);

    std::memcpy(buf, &tmp, sizeof(WL_UnitType));
    return reinterpret_cast<WL_UnitType*>(buf);
}

/* =========================================================================
 * wl_query_recall_list
 * ========================================================================= */

WL_UnitList* wl_query_recall_list(WL_Engine* engine, int side)
{
    if(!engine || !resources::gameboard) return nullptr;
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > static_cast<int>(teams.size())) return nullptr;

    const recall_list_manager& rl = teams[static_cast<size_t>(side - 1)].recall_list();

    WLArena arena(rl.size() * 128);
    std::vector<WL_Unit> tmp(rl.size());
    int n = 0;
    for(const unit_ptr& u : rl)
        fill_wl_unit(tmp[static_cast<size_t>(n++)], *u, arena);

    return build_unit_list(tmp, n, arena);
}

/* =========================================================================
 * wl_query_recruit_list
 * ========================================================================= */

WL_RecruitList* wl_query_recruit_list(WL_Engine* engine, int side)
{
    if(!engine || !resources::gameboard) return nullptr;
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > static_cast<int>(teams.size())) return nullptr;

    const team& t = teams[static_cast<size_t>(side - 1)];
    const std::set<std::string>& recruits = t.recruits();

    WLArena arena(recruits.size() * 32);

    WL_RecruitList tmp{};
    int n = 0;
    for(const auto& r : recruits) {
        if(n >= 64) break;
        tmp.types[n++] = arena.store(r);
    }
    tmp.count = n;

    size_t str_sz = arena.buf.size();
    char* buf = static_cast<char*>(std::malloc(sizeof(WL_RecruitList) + str_sz));
    if(!buf) return nullptr;
    char* str_base = buf + sizeof(WL_RecruitList);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        return rel_ptr(p, arena, str_base, str_sz);
    };

    for(int i = 0; i < n; ++i) tmp.types[i] = rel(tmp.types[i]);
    std::memcpy(buf, &tmp, sizeof(WL_RecruitList));
    return reinterpret_cast<WL_RecruitList*>(buf);
}

/* =========================================================================
 * wl_query_team
 * ========================================================================= */

WL_Team* wl_query_team(WL_Engine* engine, int side)
{
    if(!engine || !resources::gameboard) return nullptr;
    const auto& teams = resources::gameboard->teams();
    if(side < 1 || side > static_cast<int>(teams.size())) return nullptr;

    const team& t = teams[static_cast<size_t>(side - 1)];
    WLArena arena(512);
    WL_Team tmp{};

    tmp.side         = side;
    tmp.name         = arena.store(t.user_team_name().str());
    tmp.faction      = arena.store(t.faction());
    tmp.color        = arena.store(t.color());
    tmp.controller   = static_cast<WL_SideController>(t.controller());
    tmp.gold         = t.gold();
    tmp.income       = t.total_income();
    tmp.base_income  = t.base_income();
    tmp.village_gold = t.village_gold();
    tmp.support      = t.support();
    tmp.recall_cost  = t.recall_cost();

    const auto& vils = t.villages();
    int nv = 0;
    for(const map_location& v : vils) {
        if(nv >= 256) break;
        tmp.villages[nv++] = { v.wml_x(), v.wml_y() };
    }
    tmp.n_villages = nv;

    tmp.objectives         = arena.store(t.objectives().str());
    tmp.objectives_changed = t.objectives_changed() ? 1 : 0;

    int ne = 0;
    for(int s = 1; s <= static_cast<int>(teams.size()); ++s) {
        if(t.is_enemy(s)) tmp.enemy_sides[ne++] = s;
    }
    tmp.n_enemy_sides = ne;
    tmp.lost = t.lost() ? 1 : 0;

    size_t str_sz = arena.buf.size();
    char* buf = static_cast<char*>(std::malloc(sizeof(WL_Team) + str_sz));
    if(!buf) return nullptr;
    char* str_base = buf + sizeof(WL_Team);
    if(str_sz) std::memcpy(str_base, arena.buf.data(), str_sz);

    auto rel = [&](const char* p) -> const char* {
        return rel_ptr(p, arena, str_base, str_sz);
    };

    tmp.name       = rel(tmp.name);
    tmp.faction    = rel(tmp.faction);
    tmp.color      = rel(tmp.color);
    tmp.objectives = rel(tmp.objectives);

    std::memcpy(buf, &tmp, sizeof(WL_Team));
    return reinterpret_cast<WL_Team*>(buf);
}

/* =========================================================================
 * wl_query_reach
 * ========================================================================= */

WL_ReachList* wl_query_reach(WL_Engine* engine, WL_Loc wloc)
{
    if(!engine || !resources::gameboard) return nullptr;
    map_location loc(wloc.x - 1, wloc.y - 1);

    auto it = resources::gameboard->units().find(loc);
    if(it == resources::gameboard->units().end()) return nullptr;

    const unit& u = *it;
    int side = u.side();
    if(side < 1 || side > static_cast<int>(resources::gameboard->teams().size()))
        return nullptr;

    const team& viewing_team =
        resources::gameboard->teams()[static_cast<size_t>(side - 1)];
    pathfind::paths paths_obj(u, false, true, viewing_team);

    int n = static_cast<int>(paths_obj.destinations.size());

    size_t list_sz  = sizeof(WL_ReachList);
    size_t items_sz = static_cast<size_t>(n) * sizeof(WL_ReachHex);
    char* buf = static_cast<char*>(std::malloc(list_sz + items_sz));
    if(!buf) return nullptr;

    WL_ReachList* list = reinterpret_cast<WL_ReachList*>(buf);
    WL_ReachHex*  items = reinterpret_cast<WL_ReachHex*>(buf + list_sz);
    list->hexes = items;
    list->count = n;

    const gamemap& m = resources::gameboard->map();
    int i = 0;
    for(const pathfind::paths::step& s : paths_obj.destinations) {
        items[i].loc        = { s.curr.wml_x(), s.curr.wml_y() };
        items[i].moves_left = s.move_left;
        items[i].defense    = u.defense_modifier(m.get_terrain(s.curr));
        items[i].can_attack = 0;
        for(const map_location& adj : get_adjacent_tiles(s.curr)) {
            auto aj = resources::gameboard->units().find(adj);
            if(aj != resources::gameboard->units().end()
               && viewing_team.is_enemy(aj->side())) {
                items[i].can_attack = 1;
                break;
            }
        }
        ++i;
    }
    return list;
}

/* =========================================================================
 * wl_query_attack_options
 * ========================================================================= */

WL_AttackOptionList* wl_query_attack_options(WL_Engine* engine,
                                               WL_Loc watt, WL_Loc wdef)
{
    if(!engine || !resources::gameboard) return nullptr;

    map_location att(watt.x - 1, watt.y - 1);
    map_location def(wdef.x - 1, wdef.y - 1);

    const unit_map& units = resources::gameboard->units();
    auto ai = units.find(att);
    auto di = units.find(def);
    if(ai == units.end() || di == units.end()) return nullptr;

    const unit& attacker = *ai;

    std::vector<WL_AttackOption> opts;
    int default_opt = 0;
    double best_score = -1.0;

    int n_att_weapons = static_cast<int>(attacker.attacks().size());
    for(int wi = 0; wi < n_att_weapons; ++wi) {
        try {
            battle_context bc(units, att, def, wi, -1, 0.0);

            const battle_context_unit_stats& as = bc.get_attacker_stats();
            const battle_context_unit_stats& ds = bc.get_defender_stats();

            WL_AttackOption opt{};
            opt.attacker_weapon_index = wi;
            opt.defender_weapon_index = ds.attack_num;

            opt.attacker.weapon_index    = wi;
            opt.attacker.damage          = as.damage;
            opt.attacker.num_blows       = static_cast<int>(as.num_blows);
            opt.attacker.chance_to_hit   = static_cast<int>(as.chance_to_hit);
            opt.attacker.expected_damage = as.damage
                * static_cast<int>(as.num_blows)
                * static_cast<int>(as.chance_to_hit) / 100;
            opt.attacker.specials        = WL_ATKSPC_NONE;

            opt.defender.weapon_index    = ds.attack_num;
            opt.defender.damage          = ds.damage;
            opt.defender.num_blows       = static_cast<int>(ds.num_blows);
            opt.defender.chance_to_hit   = static_cast<int>(ds.chance_to_hit);
            opt.defender.expected_damage = ds.damage
                * static_cast<int>(ds.num_blows)
                * static_cast<int>(ds.chance_to_hit) / 100;
            opt.defender.specials        = WL_ATKSPC_NONE;

            double score = opt.attacker.expected_damage
                           - opt.defender.expected_damage * 0.5;
            if(score > best_score) {
                best_score  = score;
                default_opt = static_cast<int>(opts.size());
            }
            opts.push_back(opt);
        } catch(...) {
            /* Weapon not usable in this context; skip. */
        }
    }

    if(opts.empty()) return nullptr;

    int    n        = static_cast<int>(opts.size());
    size_t list_sz  = sizeof(WL_AttackOptionList);
    size_t items_sz = static_cast<size_t>(n) * sizeof(WL_AttackOption);
    char* buf = static_cast<char*>(std::malloc(list_sz + items_sz));
    if(!buf) return nullptr;

    WL_AttackOptionList* list  = reinterpret_cast<WL_AttackOptionList*>(buf);
    WL_AttackOption*     items = reinterpret_cast<WL_AttackOption*>(buf + list_sz);

    list->options        = items;
    list->count          = n;
    list->default_option = default_opt;

    std::memcpy(items, opts.data(), static_cast<size_t>(n) * sizeof(WL_AttackOption));
    return list;
}
