/**
 * wl_snapshots.cpp  —  Unit, attack, and terrain snapshot helpers
 *
 * Defines:
 *   g_data_root   — set by wl_init(); used to relativise image paths
 *   resolve_img() — strips compositor suffixes, resolves to data-relative path
 *   fill_wl_unit()    — populates a WL_Unit from a live unit instance
 *   terrain_category() — maps a terrain_type to WL_TerrainCategory
 */

#include "wl_impl.hpp"

#include "filesystem.hpp"
#include <unordered_map>
#include "map/map.hpp"
#include "terrain/terrain.hpp"
#include "terrain/translation.hpp"
#include "units/orb_status.hpp"
#include "units/types.hpp"
#include "units/unit.hpp"

/* =========================================================================
 * Shared globals (declared extern in wl_impl.hpp)
 * ========================================================================= */

std::string g_data_root;

/* =========================================================================
 * Image path resolver
 * Converts a bare image name (as returned by absolute_image / editor_image)
 * to a path relative to the game data root, searching across all data dirs
 * (core + campaigns).  The result can be served directly as /data/data/<rel>.
 * ========================================================================= */

std::string resolve_img(const std::string& rel)
{
    if(rel.empty()) return rel;
    /* Strip any Wesnoth compositor suffix (~BLIT, ~RC, etc.) */
    std::string base = rel.substr(0, rel.find('~'));
    if(base.empty()) return rel;

    /* Memoize: filesystem::get_binary_file_location() calls file_exists()
     * which crosses the WASM→JS boundary — cache results to avoid repeating
     * the VFS lookup for the same image path (hot path in wl_query_map). */
    static std::unordered_map<std::string, std::string> s_cache;
    auto it = s_cache.find(base);
    if(it != s_cache.end()) return it->second;

    std::string result;
    auto opt = filesystem::get_binary_file_location("images", base);
    if(!opt) {
        result = "core/images/" + base;
    } else {
        std::string full = *opt;
        if(!g_data_root.empty()) {
            const std::string prefix = g_data_root + "/";
            if(full.size() > prefix.size() &&
               full.substr(0, prefix.size()) == prefix)
                full = full.substr(prefix.size());
        }
        result = std::move(full);
    }
    s_cache.emplace(base, result);
    return result;
}

/* =========================================================================
 * Attack snapshot helpers
 * ========================================================================= */

namespace {

WL_DamageType damage_type_from_string(const std::string& s)
{
    if(s == "blade")  return WL_DMG_BLADE;
    if(s == "pierce") return WL_DMG_PIERCE;
    if(s == "impact") return WL_DMG_IMPACT;
    if(s == "fire")   return WL_DMG_FIRE;
    if(s == "cold")   return WL_DMG_COLD;
    if(s == "arcane") return WL_DMG_ARCANE;
    return WL_DMG_BLADE;
}

WL_AttackSpecials specials_from_attack(const attack_type& atk)
{
    int sp = 0;
    if(atk.has_special_or_ability("magical"))     sp |= WL_ATKSPC_MAGICAL;
    if(atk.has_special_or_ability("marksman"))    sp |= WL_ATKSPC_MARKSMAN;
    if(atk.has_special_or_ability("poison"))      sp |= WL_ATKSPC_POISON;
    if(atk.has_special_or_ability("slow"))        sp |= WL_ATKSPC_SLOW;
    if(atk.has_special_or_ability("drain"))       sp |= WL_ATKSPC_DRAIN;
    if(atk.has_special_or_ability("petrifies"))   sp |= WL_ATKSPC_PETRIFY;
    if(atk.has_special_or_ability("plague"))      sp |= WL_ATKSPC_PLAGUE;
    if(atk.has_special_or_ability("backstab"))    sp |= WL_ATKSPC_BACKSTAB;
    if(atk.has_special_or_ability("charge"))      sp |= WL_ATKSPC_CHARGE;
    if(atk.has_special_or_ability("firststrike")) sp |= WL_ATKSPC_FIRSTSTRIKE;
    if(atk.has_special_or_ability("swarm"))       sp |= WL_ATKSPC_SWARM;
    if(atk.has_special_or_ability("berserk"))     sp |= WL_ATKSPC_BERSERK;
    return static_cast<WL_AttackSpecials>(sp);
}

void fill_wl_attack_impl(WL_Attack& out, const attack_type& atk, WLArena& arena)
{
    out.id            = arena.store(atk.id());
    out.name          = arena.store(atk.name());
    out.damage_type   = damage_type_from_string(atk.type());
    out.icon          = arena.store(atk.icon());
    out.damage        = atk.damage();
    out.num_attacks   = atk.num_attacks();
    out.range         = (atk.range() == "ranged") ? 1 : 0;
    out.specials      = specials_from_attack(atk);
    out.specials_desc = arena.store(""); /* TODO: iterate specials */
}

/* Fill resistance from a live unit instance (accounts for abilities). */
void fill_resistance(int (&res)[WL_DMG_COUNT], const unit& u)
{
    res[WL_DMG_BLADE]  = u.resistance_against("blade",  false, map_location());
    res[WL_DMG_PIERCE] = u.resistance_against("pierce", false, map_location());
    res[WL_DMG_IMPACT] = u.resistance_against("impact", false, map_location());
    res[WL_DMG_FIRE]   = u.resistance_against("fire",   false, map_location());
    res[WL_DMG_COLD]   = u.resistance_against("cold",   false, map_location());
    res[WL_DMG_ARCANE] = u.resistance_against("arcane", false, map_location());
}

WL_UnitStatusFlags unit_status_flags(const unit& u)
{
    int f = WL_STATUS_NONE;
    if(u.get_state("poisoned"))  f |= WL_STATUS_POISONED;
    if(u.get_state("slowed"))    f |= WL_STATUS_SLOWED;
    if(u.get_state("petrified")) f |= WL_STATUS_PETRIFIED;
    if(u.invisible(u.get_location(), false)) f |= WL_STATUS_INVISIBLE;
    return static_cast<WL_UnitStatusFlags>(f);
}

WL_UnitCapability unit_capability(const unit& u)
{
    int c = WL_UNIT_DONE;
    if(u.movement_left() > 0)  c |= WL_UNIT_CAN_MOVE;
    if(!u.attacks_left() == 0) c |= WL_UNIT_CAN_ATTACK;
    return static_cast<WL_UnitCapability>(c);
}

} // anonymous namespace

/* =========================================================================
 * fill_wl_unit  (declared in wl_impl.hpp)
 * ========================================================================= */

void fill_wl_unit(WL_Unit& out, const unit& u, WLArena& arena)
{
    out.id        = arena.store(u.id());
    out.type_id   = arena.store(u.type_id());
    out.name      = arena.store(u.name().str());
    out.portrait  = arena.store(resolve_img(u.big_profile()));
    out.sprite    = arena.store(resolve_img(u.absolute_image()));
    out.side      = u.side();
    out.loc       = { u.get_location().wml_x(), u.get_location().wml_y() };

    out.hp        = u.hitpoints();
    out.max_hp    = u.max_hitpoints();
    out.xp        = u.experience();
    out.max_xp    = u.max_experience();
    out.level     = u.level();
    out.moves     = u.movement_left();
    out.max_moves = u.total_movement();

    out.alignment  = static_cast<WL_Alignment>(u.alignment());
    out.status     = unit_status_flags(u);
    out.capability = unit_capability(u);

    fill_resistance(out.resistance, u);

    int na = 0;
    for(const auto& atk : u.attacks()) {
        if(na >= 8) break;
        fill_wl_attack_impl(out.attacks[na++], atk, arena);
    }
    out.n_attacks = na;

    int nt = 0;
    for(const auto& tr : u.get_traits_list()) {
        if(nt >= 8) break;
        out.traits[nt++] = arena.store(tr);
    }
    out.n_traits = nt;

    int nab = 0;
    for(const auto& ab : u.get_ability_id_list()) {
        if(nab >= 8) break;
        out.abilities[nab++] = arena.store(ab);
    }
    out.n_abilities = nab;

    int nav = 0;
    for(const auto& adv : u.advances_to()) {
        if(nav >= 8) break;
        out.advances_to[nav++] = arena.store(adv);
    }
    out.n_advances = nav;

    int upk = u.upkeep();
    out.upkeep     = std::max(0, upk);
    out.canrecruit = u.can_recruit() ? 1 : 0;
}

/* =========================================================================
 * terrain_category  (declared in wl_impl.hpp)
 * ========================================================================= */

WL_TerrainCategory terrain_category(const terrain_type& tt)
{
    if(tt.is_village()) return WL_TERRAIN_VILLAGE;
    if(tt.is_castle() || tt.is_keep()) return WL_TERRAIN_CASTLE;

    /* Fall back to the terrain code string for other categories.
     * tt.id() returns the WML config id (e.g. "human_keep"), NOT the terrain
     * code ("Kh"), so use write_terrain_code on the type's number instead. */
    const std::string str = t_translation::write_terrain_code(tt.number());
    if(str.empty()) return WL_TERRAIN_OTHER;
    switch(str[0]) {
    case 'G': case 'R': case 'D': case 'S': return WL_TERRAIN_FLAT;
    case 'F':                                return WL_TERRAIN_FOREST;
    case 'H':                                return WL_TERRAIN_HILLS;
    case 'M':                                return WL_TERRAIN_MOUNTAINS;
    case 'W':                                return WL_TERRAIN_WATER_SHALLOW;
    case 'U':                                return WL_TERRAIN_UNWALKABLE;
    }
    return WL_TERRAIN_OTHER;
}

/* =========================================================================
 * fill_resistance_type  (used by wl_queries.cpp — not in wl_impl.hpp since
 * it's only called from wl_query_unit_type; declared below for that file)
 * ========================================================================= */

void fill_resistance_type(int (&res)[WL_DMG_COUNT], const unit_type& ut);

void fill_resistance_type(int (&res)[WL_DMG_COUNT], const unit_type& ut)
{
    res[WL_DMG_BLADE]  = 100 - ut.movement_type().resistance_against("blade");
    res[WL_DMG_PIERCE] = 100 - ut.movement_type().resistance_against("pierce");
    res[WL_DMG_IMPACT] = 100 - ut.movement_type().resistance_against("impact");
    res[WL_DMG_FIRE]   = 100 - ut.movement_type().resistance_against("fire");
    res[WL_DMG_COLD]   = 100 - ut.movement_type().resistance_against("cold");
    res[WL_DMG_ARCANE] = 100 - ut.movement_type().resistance_against("arcane");
}

/* Exported for wl_queries.cpp. */
void fill_wl_attack_pub(WL_Attack& out, const attack_type& atk, WLArena& arena)
{
    fill_wl_attack_impl(out, atk, arena);
}
