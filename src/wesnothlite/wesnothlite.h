/**
 * wesnothlite.h  —  WesnothLite public C API
 *
 * An embeddable, platform-neutral interface to the Wesnoth singleplayer
 * engine.  See wesnothlite/interface.md for the full design document.
 *
 * Memory model
 * ============
 *   All snapshots returned by wl_query_*() / wl_list_*() are allocated as a
 *   single contiguous block.  const char * fields inside them point into that
 *   same block.  Free any snapshot with a single wl_free() call.
 *
 *   WL_Event pointers returned by wl_step() are engine-owned; they are
 *   valid only until the next call to wl_step().  Do NOT pass them to
 *   wl_free().
 */

#pragma once
#ifndef WESNOTHLITE_H
#define WESNOTHLITE_H

#include <stddef.h>   /* size_t */

/**
 * WL_API — marks functions that must be exported from the WASM module.
 * Under Emscripten, expands to EMSCRIPTEN_KEEPALIVE so the linker does not
 * strip them; on other platforms it is a no-op.
 */
#ifdef __EMSCRIPTEN__
#  include <emscripten.h>
#  define WL_API EMSCRIPTEN_KEEPALIVE
#else
#  define WL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Forward declarations / opaque types
 * ========================================================================= */

/** Opaque engine handle.  Create with wl_init(), destroy with wl_shutdown(). */
typedef struct WL_Engine WL_Engine;

/* =========================================================================
 * Basic scalar types
 * ========================================================================= */

/** 1-based (x, y) hex coordinate matching WML convention. */
typedef struct { int x; int y; } WL_Loc;

/** Function return codes. */
typedef enum {
    WL_OK            =  0,
    WL_ERR_GENERIC   = -1,  /**< Unspecified error; check wl_last_error(). */
    WL_ERR_INVALID   = -2,  /**< Bad argument or precondition not met. */
    WL_ERR_NO_GAME   = -3,  /**< No scenario loaded yet. */
    WL_ERR_NOT_TURN  = -4,  /**< Not a human side's turn, or AI is playing. */
    WL_ERR_BLOCKED   = -5,  /**< Awaiting wl_choose(); no other action allowed. */
    WL_ERR_NO_PATH   = -6,  /**< Unit cannot reach the target hex. */
    WL_ERR_NO_GOLD   = -7,  /**< Insufficient gold for recruit/recall. */
    WL_ERR_NO_SPACE  = -8,  /**< No adjacent castle hex available. */
    WL_ERR_UNKNOWN   = -9,  /**< Unit type or unit id not found. */
} WL_Status;

/** Overall game phase. */
typedef enum {
    WL_PHASE_NONE,       /**< No game in progress. */
    WL_PHASE_STARTING,   /**< Preload / prestart events running. */
    WL_PHASE_PLAYING,    /**< Normal gameplay. */
    WL_PHASE_ENDED,      /**< Victory/defeat determined; linger mode. */
} WL_Phase;

/** Who controls a side. */
typedef enum {
    WL_CTRL_HUMAN = 0,
    WL_CTRL_AI    = 1,
    WL_CTRL_NONE  = 2,
} WL_SideController;

/** Scenario outcome. */
typedef enum {
    WL_OUTCOME_NONE    = 0,
    WL_OUTCOME_VICTORY = 1,
    WL_OUTCOME_DEFEAT  = 2,
    WL_OUTCOME_QUIT    = 3,
} WL_Outcome;

/** Unit alignment. */
typedef enum {
    WL_ALIGN_LAWFUL  = 0,
    WL_ALIGN_NEUTRAL = 1,
    WL_ALIGN_CHAOTIC = 2,
    WL_ALIGN_LIMINAL = 3,
} WL_Alignment;

/**
 * The six damage types.
 * Used as an array index into WL_Unit.resistance[] and WL_UnitType.resistance[].
 */
typedef enum {
    WL_DMG_BLADE  = 0,
    WL_DMG_PIERCE = 1,
    WL_DMG_IMPACT = 2,
    WL_DMG_FIRE   = 3,
    WL_DMG_COLD   = 4,
    WL_DMG_ARCANE = 5,
    WL_DMG_COUNT  = 6,
} WL_DamageType;

/** Unit status effect flags (bitfield). */
typedef enum {
    WL_STATUS_NONE      = 0,
    WL_STATUS_POISONED  = 1 << 0,
    WL_STATUS_SLOWED    = 1 << 1,
    WL_STATUS_PETRIFIED = 1 << 2,
    WL_STATUS_INVISIBLE = 1 << 3,
    WL_STATUS_GUARDIAN  = 1 << 4,
} WL_UnitStatusFlags;

/** Attack special properties (bitfield). */
typedef enum {
    WL_ATKSPC_NONE        = 0,
    WL_ATKSPC_MAGICAL     = 1 << 0,
    WL_ATKSPC_MARKSMAN    = 1 << 1,
    WL_ATKSPC_POISON      = 1 << 2,
    WL_ATKSPC_SLOW        = 1 << 3,
    WL_ATKSPC_DRAIN       = 1 << 4,
    WL_ATKSPC_PETRIFY     = 1 << 5,
    WL_ATKSPC_PLAGUE      = 1 << 6,
    WL_ATKSPC_BACKSTAB    = 1 << 7,
    WL_ATKSPC_CHARGE      = 1 << 8,
    WL_ATKSPC_FIRSTSTRIKE = 1 << 9,
    WL_ATKSPC_SWARM       = 1 << 10,
    WL_ATKSPC_BERSERK     = 1 << 11,
} WL_AttackSpecials;

/** What the unit can still do this turn (bitfield). */
typedef enum {
    WL_UNIT_DONE       = 0,
    WL_UNIT_CAN_MOVE   = 1 << 0,
    WL_UNIT_CAN_ATTACK = 1 << 1,
} WL_UnitCapability;

/** Broad terrain category for UI display. */
typedef enum {
    WL_TERRAIN_FLAT,
    WL_TERRAIN_FOREST,
    WL_TERRAIN_HILLS,
    WL_TERRAIN_MOUNTAINS,
    WL_TERRAIN_WATER_SHALLOW,
    WL_TERRAIN_WATER_DEEP,
    WL_TERRAIN_VILLAGE,
    WL_TERRAIN_CASTLE,
    WL_TERRAIN_CAVE,
    WL_TERRAIN_UNWALKABLE,
    WL_TERRAIN_OTHER,
} WL_TerrainCategory;

/** Hex visibility from a given side's perspective. */
typedef enum {
    WL_VIS_VISIBLE  = 0,  /**< Fully visible. */
    WL_VIS_FOGGED   = 1,  /**< Fog-of-war (side uses fog but hasn't scouted). */
    WL_VIS_SHROUDED = 2,  /**< Shroud (side has never seen this hex). */
} WL_Visibility;

/** Kind of pending player choice (accompanies WL_EVENT_CHOICE_NEEDED). */
typedef enum {
    WL_CHOICE_ADVANCE,  /**< Unit leveled up; options are unit type ids. */
    WL_CHOICE_MESSAGE,  /**< WML [message] with [option] children. */
    WL_CHOICE_RECRUIT,  /**< Rare: WML-driven recruit override. */
} WL_ChoiceKind;

/* =========================================================================
 * Limits
 * ========================================================================= */

#define WL_MAX_OPTIONS   16
#define WL_MAX_PATH      64
#define WL_MAX_BLOWS    128

/* =========================================================================
 * Combat helper structs
 * ========================================================================= */

/** One individual blow within a combat exchange. */
typedef struct {
    int attacker_strikes;   /**< 1 = attacker struck, 0 = defender struck. */
    int hit;                /**< 1 = landed, 0 = missed. */
    int damage;             /**< Damage dealt (0 if miss). */
    int attacker_hp_after;
    int defender_hp_after;
} WL_Blow;

/** Aggregate result for one side of an attack exchange. */
typedef struct {
    int weapon_index;
    int damage_per_hit;
    int num_blows;
    int hits;
    int chance_to_hit;   /**< Percentage. */
    int hp_start;
    int hp_end;          /**< 0 if killed. */
} WL_CombatResult;

/* =========================================================================
 * Game-event struct
 * ========================================================================= */

typedef enum {
    /* Scenario lifecycle */
    WL_EVENT_LOADING_CONFIG,   /* emitted by game thread before config parse */
    WL_EVENT_SCENARIO_START,
    WL_EVENT_SCENARIO_END,

    /* Turn flow */
    WL_EVENT_TURN_START,
    WL_EVENT_SIDE_TURN_START,
    WL_EVENT_SIDE_TURN_END,
    WL_EVENT_WAITING_FOR_INPUT,

    /* Unit actions */
    WL_EVENT_UNIT_MOVE,
    WL_EVENT_UNIT_ATTACK,
    WL_EVENT_UNIT_RECRUIT,
    WL_EVENT_UNIT_RECALL,
    WL_EVENT_UNIT_DISMISS,
    WL_EVENT_UNIT_DIE,
    WL_EVENT_UNIT_ADVANCE,
    WL_EVENT_UNIT_XP,
    WL_EVENT_UNIT_HEAL,
    WL_EVENT_UNIT_STATUS,
    WL_EVENT_VILLAGE_CAPTURE,

    /* Narrative */
    WL_EVENT_MESSAGE,
    WL_EVENT_STORY,
    WL_EVENT_OBJECTIVES_UPDATE,
    WL_EVENT_CHOICE_NEEDED,

    /* Audio */
    WL_EVENT_SOUND,
    WL_EVENT_MUSIC_CHANGE,
} WL_EventType;


typedef struct WL_Event {
    WL_EventType type;

    union {

        struct {
            WL_Outcome  outcome;
            const char* next_scenario;   /**< NULL if no continuation. */
        } scenario_end;

        struct { int turn; }           turn_start;
        struct { int side; int turn; } side_turn_start;
        struct { int side; int turn; } side_turn_end;
        struct { int side; int turn; } waiting_for_input;

        struct {
            const char* unit_id;
            int         side;
            WL_Loc      from;
            WL_Loc      to;
            WL_Loc      path[WL_MAX_PATH];
            int         path_len;
        } unit_move;

        struct {
            const char*     attacker_id;
            const char*     defender_id;
            int             attacker_side;
            int             defender_side;
            WL_Loc          attacker_loc;
            WL_Loc          defender_loc;
            WL_CombatResult attacker_result;
            WL_CombatResult defender_result;
            WL_Blow         blows[WL_MAX_BLOWS];
            int             n_blows;
        } unit_attack;

        struct {
            const char* unit_type_id;
            const char* unit_id;
            int         side;
            WL_Loc      at;
        } unit_recruit;

        struct {
            const char* unit_id;
            const char* unit_type_id;
            int         side;
            WL_Loc      at;
        } unit_recall;

        struct {
            const char* unit_id;
            const char* unit_type_id;
            int         side;
        } unit_dismiss;

        struct {
            const char* unit_id;
            const char* unit_type_id;
            int         side;
            WL_Loc      loc;
            const char* killer_id;   /**< NULL if no killer. */
        } unit_die;

        struct {
            const char* unit_id;
            int         side;
            WL_Loc      loc;
            const char* from_type_id;
            const char* to_type_id;  /**< NULL if modification-based. */
        } unit_advance;

        struct {
            const char* unit_id;
            int         side;
            WL_Loc      loc;
            int         xp_gained;
            int         xp_total;
            int         xp_needed;   /**< 0 if at max level. */
        } unit_xp;

        struct {
            const char* unit_id;
            int         side;
            WL_Loc      loc;
            int         amount;      /**< Positive = healed, negative = hurt. */
        } unit_heal;

        struct {
            const char*        unit_id;
            int                side;
            WL_Loc             loc;
            WL_UnitStatusFlags flags;
        } unit_status;

        struct {
            WL_Loc loc;
            int    old_side;
            int    new_side;
        } village_capture;

        struct {
            const char* speaker;
            const char* portrait;  /**< NULL if none. */
            const char* text;
        } message;

        struct {
            const char* title;
            const char* text;
            const char* background; /**< NULL if none. */
        } story;

        struct {
            int         side;
            const char* text;
        } objectives_update;

        struct {
            WL_ChoiceKind kind;
            const char*   prompt;
            /**
             * For WL_CHOICE_ADVANCE: strings are unit type ids, pass to
             * wl_query_unit_type() before deciding.
             * For WL_CHOICE_MESSAGE: strings are display text.
             */
            const char*   options[WL_MAX_OPTIONS];
            int           n_options;
            /** WL_CHOICE_MESSAGE only: speaker name and portrait image path. */
            const char*   speaker;
            const char*   portrait;
        } choice_needed;

        struct { const char* path; } sound;

        struct {
            const char* path;
            const char* title;
        } music_change;

    }; /* anonymous union */
} WL_Event;

/* =========================================================================
 * Snapshot structs (returned by wl_query_*; freed with wl_free)
 * ========================================================================= */

/** One attack type belonging to a unit or unit type. */
typedef struct {
    const char*       id;
    const char*       name;
    WL_DamageType     damage_type;
    const char*       icon;
    int               damage;
    int               num_attacks;
    int               range;         /**< 0 = melee, 1 = ranged. */
    WL_AttackSpecials specials;
    const char*       specials_desc;
} WL_Attack;

/** Full description of a unit type (not an instance). */
typedef struct {
    const char*  type_id;
    const char*  name;
    const char*  description;
    const char*  portrait;
    const char*  sprite;
    const char*  race;

    int          max_hp;
    int          max_moves;
    int          max_xp;
    int          level;
    WL_Alignment alignment;
    int          cost;
    int          recall_cost;   /**< -1 = use global default. */

    /** resistance[WL_DMG_*]: positive = resists, negative = vulnerable. */
    int          resistance[WL_DMG_COUNT];

    WL_Attack    attacks[8];
    int          n_attacks;

    const char*  abilities[8];
    int          n_abilities;

    const char*  advances_to[8];
    int          n_advances;
} WL_UnitType;

/** Snapshot of a specific unit instance. */
typedef struct {
    const char*        id;
    const char*        type_id;
    const char*        name;
    const char*        portrait;
    const char*        sprite;
    int                side;
    WL_Loc             loc;          /**< {0,0} if in recall list. */

    int                hp;
    int                max_hp;
    int                xp;
    int                max_xp;
    int                level;
    int                moves;
    int                max_moves;

    WL_Alignment       alignment;
    WL_UnitStatusFlags status;
    WL_UnitCapability  capability;

    /** resistance[WL_DMG_*]: positive = resists. */
    int                resistance[WL_DMG_COUNT];

    WL_Attack          attacks[8];
    int                n_attacks;

    const char*        traits[8];
    int                n_traits;
    const char*        abilities[8];
    int                n_abilities;

    const char*        advances_to[8];
    int                n_advances;

    int                upkeep;       /**< Gold/turn; 0 = free/loyal. */
    int                canrecruit;
} WL_Unit;

typedef struct {
    WL_Unit* units;
    int      count;
} WL_UnitList;

typedef struct {
    const char* types[64];
    int         count;
} WL_RecruitList;

typedef struct {
    WL_Loc             loc;
    WL_TerrainCategory category;
    const char*        id;
    const char*        name;
    const char*        icon;
    const char*        overlay_icon;  /**< Editor image of overlay terrain (e.g. village building), or "" if none. */
    int                village_side;
    int                starting_side;
} WL_Terrain;

typedef struct {
    int        width;
    int        height;
    WL_Terrain hexes[]; /**< width*height entries, row-major. */
} WL_MapData;

typedef struct {
    int           side;
    int           width;
    int           height;
    WL_Visibility values[]; /**< width*height entries, row-major. */
} WL_VisibilityMap;

typedef struct {
    WL_Loc loc;
    int    moves_left;
    int    defense;      /**< Unit-specific defense % on this terrain. */
    int    can_attack;
} WL_ReachHex;

typedef struct {
    WL_ReachHex* hexes;
    int          count;
} WL_ReachList;

typedef struct {
    int               weapon_index;
    int               damage;
    int               num_blows;
    int               chance_to_hit;
    int               expected_damage;
    WL_AttackSpecials specials;
} WL_CombatPreview;

typedef struct {
    int              attacker_weapon_index;
    int              defender_weapon_index;  /**< -1 if defender cannot counter. */
    WL_CombatPreview attacker;
    WL_CombatPreview defender;
} WL_AttackOption;

typedef struct {
    WL_AttackOption* options;
    int              count;
    int              default_option;
} WL_AttackOptionList;

typedef struct {
    int               side;
    const char*       name;
    const char*       faction;
    const char*       color;
    WL_SideController controller;
    int               gold;
    int               income;
    int               base_income;
    int               village_gold;
    int               support;
    int               recall_cost;

    WL_Loc            villages[256];
    int               n_villages;

    const char*       objectives;
    int               objectives_changed;

    int               enemy_sides[8];
    int               n_enemy_sides;

    int               lost;
} WL_Team;

typedef struct {
    const char* id;
    const char* name;
    int         lawful_bonus;
    const char* image;
    const char* mask_image;
} WL_TimeOfDay;

typedef struct {
    WL_Phase     phase;
    int          turn;
    int          max_turns;
    int          current_side;
    int          n_sides;
    WL_TimeOfDay tod;
    const char*  scenario_id;
    const char*  scenario_name;
    const char*  campaign_id;
    const char*  campaign_name;
    const char*  difficulty;
    WL_Outcome   outcome;
} WL_GameInfo;

typedef struct {
    const char* id;
    const char* name;
    const char* description;
    const char* image;
    const char* icon;
    const char* difficulties[8];
    int         n_difficulties;
    const char* first_scenario;
} WL_CampaignInfo;

typedef struct {
    WL_CampaignInfo* campaigns;
    int              count;
} WL_CampaignList;

/* =========================================================================
 * API functions
 * ========================================================================= */

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

WL_API WL_Engine*  wl_init(const char* data_path, const char* userdata_path);
WL_API void        wl_shutdown(WL_Engine* engine);
WL_API const char* wl_last_error(WL_Engine* engine);

/**
 * Configure the locale used for translating in-game strings.
 *
 * Call this after wl_init() and before wl_start_campaign() / wl_start_scenario().
 *
 * @param locale
 *   POSIX locale name, e.g. "pl_PL", "fr_FR", "de_DE".
 *   Pass NULL or "" to use the system locale (LANG/LC_ALL environment).
 *
 * @param translations_path
 *   Path to the directory that contains per-locale subdirectories of the form
 *   <locale>/LC_MESSAGES/<domain>.mo  (the standard gettext layout).
 *   Pass NULL to use <data_path>/../translations, which matches the default
 *   Wesnoth source/build layout where data/ and translations/ are siblings.
 *
 * @return WL_OK on success, WL_ERR_NO_GAME if the engine is not initialised.
 */
WL_API WL_Status wl_set_locale(WL_Engine* engine,
                                const char* locale,
                                const char* translations_path);

/* ── Scenario setup ─────────────────────────────────────────────────────── */

WL_API WL_CampaignList* wl_list_campaigns(WL_Engine* engine);

WL_API WL_Status wl_start_campaign(WL_Engine* engine,
                                    const char* campaign_id,
                                    const char* difficulty);

WL_API WL_Status wl_start_scenario(WL_Engine* engine,
                                    const char* campaign_id,
                                    const char* scenario_id,
                                    const char* difficulty);

WL_API WL_Status      wl_load_save(WL_Engine* engine, const char* save_path);
WL_API WL_Status      wl_load_from_buffer(WL_Engine* engine,
                                           const unsigned char* buf, size_t len);

WL_API WL_Status      wl_save(WL_Engine* engine, const char* save_path);
WL_API unsigned char* wl_save_to_buffer(WL_Engine* engine, size_t* out_size);

/* ── Game pump ──────────────────────────────────────────────────────────── */

/**
 * Advance the engine by one step and return the next event, or NULL when
 * the engine is waiting for human player input.
 *
 * The returned pointer is engine-owned and valid until the next wl_step().
 * Do NOT pass it to wl_free().
 *
 * wl_step() blocks briefly if the game thread has not yet produced an event
 * or reached a wait state — the duration is bounded by one logical game step.
 */
WL_API const WL_Event* wl_step(WL_Engine* engine);

/* ── Player actions (valid only when wl_step() returned NULL) ──────────── */

WL_API WL_Status wl_move(WL_Engine* engine, WL_Loc from, WL_Loc to);
WL_API WL_Status wl_attack(WL_Engine* engine, WL_Loc attacker, WL_Loc defender,
                             int weapon_index);
WL_API WL_Status wl_recruit(WL_Engine* engine, const char* unit_type_id, WL_Loc at);
WL_API WL_Status wl_recall(WL_Engine* engine, const char* unit_id, WL_Loc at);
WL_API WL_Status wl_dismiss(WL_Engine* engine, const char* unit_id);
WL_API WL_Status wl_end_turn(WL_Engine* engine);
WL_API WL_Status wl_choose(WL_Engine* engine, int option_index);
WL_API WL_Status wl_undo(WL_Engine* engine);

/* ── State queries ──────────────────────────────────────────────────────── */

WL_API WL_GameInfo*         wl_query_game(WL_Engine* engine);
WL_API WL_MapData*          wl_query_map(WL_Engine* engine);
WL_API WL_VisibilityMap*    wl_query_visibility(WL_Engine* engine, int side);
WL_API WL_UnitList*         wl_query_units(WL_Engine* engine);
WL_API WL_Unit*             wl_query_unit_at(WL_Engine* engine, WL_Loc loc);
WL_API WL_UnitType*         wl_query_unit_type(WL_Engine* engine,
                                                const char* unit_type_id);
WL_API WL_UnitList*         wl_query_recall_list(WL_Engine* engine, int side);
WL_API WL_RecruitList*      wl_query_recruit_list(WL_Engine* engine, int side);
WL_API WL_Team*             wl_query_team(WL_Engine* engine, int side);
WL_API WL_ReachList*        wl_query_reach(WL_Engine* engine, WL_Loc loc);
WL_API WL_AttackOptionList* wl_query_attack_options(WL_Engine* engine,
                                                     WL_Loc attacker, WL_Loc defender);

/* ── Memory management ──────────────────────────────────────────────────── */

/** Free any snapshot returned by a query or list function. NULL-safe. */
WL_API void wl_free(void* snapshot);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WESNOTHLITE_H */
