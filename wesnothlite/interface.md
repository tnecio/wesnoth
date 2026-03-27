# WesnothLite — Embeddable Library API

## Overview

WesnothLite is a C API that wraps the Wesnoth singleplayer engine as an embeddable
library.  It is designed to compile to native shared libraries, static libraries, and
WebAssembly (WASM), so that arbitrary front-ends (web, mobile, desktop) can drive a
complete Wesnoth singleplayer session without knowing anything about Wesnoth internals.

### Design principles

* **C ABI.**  No C++ types in the public interface; safe to call from C, C++,
  Python (ctypes/cffi), JavaScript (Emscripten), Rust (bindgen), etc.
* **Single opaque handle.**  All state lives inside a `WL_Engine *`.  Multiple
  independent instances can coexist in the same process.
* **Synchronous event pump.**  `wl_step()` advances the engine by one logical step
  and returns one event, or `NULL` when waiting for player input.  Wesnoth has no
  real-time component; the engine only advances when told to.  The caller drives the
  loop at whatever rate it likes — fast for AI-only play, gated on UI animations for
  a graphical front-end.
* **Snapshot semantics.**  Query functions return freshly-allocated snapshots of
  game state.  The engine never hands out pointers into its own internals.
  The caller frees snapshots with `wl_free()`.
* **File-system independence.**  Every function that reads or writes persistent data
  has both a file-path variant and an in-memory buffer variant.  The buffer variants
  are the only ones available in a WASM environment where the host file system may
  not be accessible.
* **Exhaustive but minimal.**  Every action a human player can take through the
  standard Wesnoth GUI is reachable through this API.  Every piece of information
  the GUI presents to the player is queryable.

### Strings and memory

All strings inside snapshots are `const char *` pointers.  Each snapshot is allocated
as a single contiguous memory block; string fields point into that block.  All
string data is valid for the lifetime of the snapshot and is freed atomically by
`wl_free()`.  Strings are never truncated silently: if a WML author writes a message
longer than the internal staging buffer the engine will allocate a larger block.

Traits and abilities are exposed as plain strings rather than enums because WML
add-ons can define arbitrary new traits and abilities at runtime.  Consumers that
care about standard values (e.g. "Quick", "Resilient", "leadership") can compare
against the known names; unknown strings should be displayed as-is.

### Sprite and animation assets

The API exposes the paths to image files (unit sprites, terrain tiles, portraits,
icons) as they appear in the Wesnoth data directory.  Full animation frame sequences
and attack animations are not part of this API: they are defined in per-unit WML/CFG
files and are intended to be consumed by a renderer that speaks the Wesnoth asset
format directly.  A minimal front-end can render a static sprite at the unit's
location using `WL_Unit.sprite`; a richer front-end can parse the unit-type cfg for
full animation data.

---

## Table of Contents

1. [Basic types and enumerations](#1-basic-types-and-enumerations)
2. [Game-event struct](#2-game-event-struct)
3. [Snapshot structs](#3-snapshot-structs)
4. [Lifecycle functions](#4-lifecycle-functions)
5. [Scenario setup](#5-scenario-setup)
6. [Game pump](#6-game-pump)
7. [Player actions](#7-player-actions)
8. [State queries](#8-state-queries)
9. [Memory management](#9-memory-management)
10. [Typical usage patterns](#10-typical-usage-patterns)

---

## 1. Basic types and enumerations

```c
/* Opaque engine handle. */
typedef struct WL_Engine WL_Engine;

/* 1-based (x, y) hex coordinate matching WML convention. */
typedef struct { int x; int y; } WL_Loc;

/* Function return codes. */
typedef enum {
    WL_OK            =  0,  /* Success. */
    WL_ERR_GENERIC   = -1,  /* Unspecified error; check wl_last_error(). */
    WL_ERR_INVALID   = -2,  /* Invalid argument or precondition not met. */
    WL_ERR_NO_GAME   = -3,  /* No scenario loaded. */
    WL_ERR_NOT_TURN  = -4,  /* Not this side's turn, or AI is playing. */
    WL_ERR_BLOCKED   = -5,  /* Waiting for a choice; only wl_choose() is valid. */
    WL_ERR_NO_PATH   = -6,  /* Unit cannot reach the target hex. */
    WL_ERR_NO_GOLD   = -7,  /* Insufficient gold for recruit/recall. */
    WL_ERR_NO_SPACE  = -8,  /* No adjacent castle hex available. */
    WL_ERR_UNKNOWN   = -9,  /* Unit type / unit id not found. */
} WL_Status;

/* Overall game phase. */
typedef enum {
    WL_PHASE_NONE,      /* No game in progress. */
    WL_PHASE_STARTING,  /* Preload / prestart events running. */
    WL_PHASE_PLAYING,   /* Normal gameplay. */
    WL_PHASE_ENDED,     /* Victory / defeat determined; linger mode. */
} WL_Phase;

/* Who controls a side. */
typedef enum {
    WL_CTRL_HUMAN = 0,
    WL_CTRL_AI    = 1,
    WL_CTRL_NONE  = 2,
} WL_SideController;

/* Outcome of a completed scenario. */
typedef enum {
    WL_OUTCOME_NONE    = 0,
    WL_OUTCOME_VICTORY = 1,
    WL_OUTCOME_DEFEAT  = 2,
    WL_OUTCOME_QUIT    = 3,
} WL_Outcome;

/* Unit alignment (affects combat bonus with time-of-day). */
typedef enum {
    WL_ALIGN_LAWFUL  = 0,
    WL_ALIGN_NEUTRAL = 1,
    WL_ALIGN_CHAOTIC = 2,
    WL_ALIGN_LIMINAL = 3,
} WL_Alignment;

/*
 * The six damage types in Wesnoth.
 * Used as an index into WL_Unit.resistance[] and WL_UnitType.resistance[].
 */
typedef enum {
    WL_DMG_BLADE   = 0,
    WL_DMG_PIERCE  = 1,
    WL_DMG_IMPACT  = 2,
    WL_DMG_FIRE    = 3,
    WL_DMG_COLD    = 4,
    WL_DMG_ARCANE  = 5,
    WL_DMG_COUNT   = 6,
} WL_DamageType;

/* Bitfield: status effects currently affecting a unit. */
typedef enum {
    WL_STATUS_NONE      = 0,
    WL_STATUS_POISONED  = 1 << 0,
    WL_STATUS_SLOWED    = 1 << 1,
    WL_STATUS_PETRIFIED = 1 << 2,
    WL_STATUS_INVISIBLE = 1 << 3,
    WL_STATUS_GUARDIAN  = 1 << 4,
} WL_UnitStatusFlags;

/* Bitfield: special properties of an attack. */
typedef enum {
    WL_ATKSPC_NONE        = 0,
    WL_ATKSPC_MAGICAL     = 1 << 0,   /* Always hits at 70%. */
    WL_ATKSPC_MARKSMAN    = 1 << 1,   /* Min 60% chance to hit. */
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

/* What a player can currently do with a unit (for UI orb colour). */
typedef enum {
    WL_UNIT_DONE       = 0,
    WL_UNIT_CAN_MOVE   = 1 << 0,
    WL_UNIT_CAN_ATTACK = 1 << 1,
} WL_UnitCapability;

/* Terrain category (broad classification for UI display). */
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

/* Hex visibility from a given side's perspective. */
typedef enum {
    WL_VIS_VISIBLE  = 0,  /* Fully visible. */
    WL_VIS_FOGGED   = 1,  /* In fog of war (side uses fog but hasn't scouted). */
    WL_VIS_SHROUDED = 2,  /* Under shroud (side has never seen this hex). */
} WL_Visibility;

/* Type of pending player choice (accompanies WL_EVENT_CHOICE_NEEDED). */
typedef enum {
    WL_CHOICE_ADVANCE,  /* Unit leveled up; choose an advancement. */
    WL_CHOICE_MESSAGE,  /* WML [message] with [option] children. */
    WL_CHOICE_RECRUIT,  /* Rare: WML-driven recruit override. */
} WL_ChoiceKind;
```

---

## 2. Game-event struct

`wl_step()` returns a pointer to one of these on each call, or `NULL` when
the engine is waiting for player input.

```c
#define WL_MAX_OPTIONS  16   /* Max options in a dialog / advancement choice. */
#define WL_MAX_PATH     64   /* Max hexes in a move path. */
#define WL_MAX_BLOWS   128   /* Max individual blows in one attack exchange. */

/*
 * One individual blow within a combat exchange.
 * An attack exchange consists of alternating attacker and defender strikes
 * until all blows are exhausted or one unit dies.
 */
typedef struct {
    int attacker_strikes;  /* 1 = attacker struck this blow, 0 = defender. */
    int hit;               /* 1 = blow landed, 0 = missed. */
    int damage;            /* Damage dealt (0 if missed). */
    int attacker_hp_after; /* Attacker HP remaining after this blow. */
    int defender_hp_after; /* Defender HP remaining after this blow. */
} WL_Blow;

/* Aggregate combat result for one side of an attack exchange. */
typedef struct {
    int weapon_index;   /* Index into the unit's attacks[] array. */
    int damage_per_hit; /* Base damage per successful blow. */
    int num_blows;      /* Total blows attempted. */
    int hits;           /* Blows that landed. */
    int chance_to_hit;  /* Percentage. */
    int hp_start;       /* HP before the exchange. */
    int hp_end;         /* HP after the exchange (0 if killed). */
} WL_CombatResult;

typedef enum {
    /* ── Scenario lifecycle ────────────────────────────────────────── */
    WL_EVENT_SCENARIO_START,    /* Preload complete; game state is ready. */
    WL_EVENT_SCENARIO_END,      /* Scenario finished. */

    /* ── Turn flow ─────────────────────────────────────────────────── */
    WL_EVENT_TURN_START,        /* A new turn number has begun. */
    WL_EVENT_SIDE_TURN_START,   /* A side is about to play. */
    WL_EVENT_SIDE_TURN_END,     /* A side finished playing. */
    WL_EVENT_WAITING_FOR_INPUT, /* Human side ready; call action functions. */

    /* ── Unit actions (generated by AI and by human actions) ────────── */
    WL_EVENT_UNIT_MOVE,         /* A unit moved along a path. */
    WL_EVENT_UNIT_ATTACK,       /* A full attack exchange took place. */
    WL_EVENT_UNIT_SPAWN,      /* A unit was recruited or recalled. */
    WL_EVENT_UNIT_DISMISS,      /* A unit was dismissed from the recall list. */
    WL_EVENT_UNIT_DIE,          /* A unit was killed. */
    WL_EVENT_UNIT_ADVANCE,      /* A unit was promoted to a new type. */
    WL_EVENT_UNIT_XP,           /* A unit gained experience (after combat, kills). */
    WL_EVENT_UNIT_HEAL,         /* A unit was healed (village, ability, item…). */
    WL_EVENT_UNIT_STATUS,       /* A unit's status flags changed. */
    WL_EVENT_VILLAGE_CAPTURE,   /* A village changed ownership. */

    /* ── Narrative / UI ────────────────────────────────────────────── */
    WL_EVENT_MESSAGE,           /* [message] or [narrate] — display to player. */
    WL_EVENT_STORY,             /* [story] part — intro text / image sequence. */
    WL_EVENT_OBJECTIVES_UPDATE, /* Side objectives text was updated. */
    WL_EVENT_CHOICE_NEEDED,     /* Engine needs the player to pick an option. */

    /* ── Audio ─────────────────────────────────────────────────────── */
    WL_EVENT_SOUND,             /* Play a sound effect once. */
    WL_EVENT_MUSIC_CHANGE,      /* Switch background music track. */
} WL_EventType;


typedef struct WL_Event {
    WL_EventType type;

    union {

        /* WL_EVENT_SCENARIO_START — no extra fields. */

        struct {
            WL_Outcome  outcome;
            const char* next_scenario; /* NULL if no continuation. */
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
            const char*    attacker_id;
            const char*    defender_id;
            int            attacker_side;
            int            defender_side;
            WL_Loc         attacker_loc;
            WL_Loc         defender_loc;
            WL_CombatResult attacker_result;
            WL_CombatResult defender_result;
            /* Blow-by-blow sequence for animation. */
            WL_Blow        blows[WL_MAX_BLOWS];
            int            n_blows;
        } unit_attack;

        struct {
            const char* unit_type_id;
            const char* unit_id;
            int         side;
            WL_Loc      at;
        } unit_spawn;

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
            const char* killer_id; /* NULL if no killer (poison, scenario end…). */
        } unit_die;

        struct {
            const char* unit_id;
            int         side;
            WL_Loc      loc;
            const char* from_type_id;
            const char* to_type_id; /* NULL if modification-based advancement. */
        } unit_advance;

        struct {
            const char* unit_id;
            int         side;
            WL_Loc      loc;
            int         xp_gained;
            int         xp_total;
            int         xp_needed; /* For the NEXT level; 0 if at max level. */
        } unit_xp;

        struct {
            const char* unit_id;
            int         side;
            WL_Loc      loc;
            int         amount; /* Positive = healed, negative = damaged by event. */
        } unit_heal;

        struct {
            const char*       unit_id;
            int               side;
            WL_Loc            loc;
            WL_UnitStatusFlags flags; /* Full bitmask after the change. */
        } unit_status;

        struct {
            WL_Loc loc;
            int    old_side; /* 0 = was unowned. */
            int    new_side;
        } village_capture;

        struct {
            const char* speaker;   /* Unit id, "narrator", "WML", etc. */
            const char* portrait;  /* Image path, or NULL if none. */
            const char* text;
        } message;

        struct {
            const char* title;
            const char* text;
            const char* background; /* Image path, or NULL. */
        } story;

        struct {
            int         side;
            const char* text;
        } objectives_update;

        struct {
            WL_ChoiceKind kind;
            const char*   prompt;
            /*
             * For WL_CHOICE_ADVANCE: each option is a unit type id that can
             * be passed to wl_query_unit_type() to show the player full stats
             * before deciding.
             * For WL_CHOICE_MESSAGE: each option is the display string.
             */
            const char*   options[WL_MAX_OPTIONS];
            int           n_options;
        } choice_needed;

        struct { const char* path; } sound;

        struct {
            const char* path;
            const char* title;
        } music_change;

    }; /* anonymous union */
} WL_Event;
```

---

## 3. Snapshot structs

Returned by query functions; freed by the caller via `wl_free()`.
All `const char *` fields point into the same allocation as the struct itself.

```c
/* One attack type belonging to a unit or unit type. */
typedef struct {
    const char*       id;
    const char*       name;
    WL_DamageType     damage_type;
    const char*       icon;       /* Image path. */
    int               damage;
    int               num_attacks;
    int               range;      /* 0 = melee, 1 = ranged. */
    WL_AttackSpecials specials;
    const char*       specials_desc; /* Human-readable list of special names. */
} WL_Attack;

/*
 * Full description of a unit type (not a specific unit instance).
 * Useful for recruit / recall / advancement dialogs where the player needs
 * to evaluate a unit they do not yet own.
 * Obtain via wl_query_unit_type().
 */
typedef struct {
    const char*  type_id;
    const char*  name;
    const char*  description;
    const char*  portrait;     /* Image path. */
    const char*  sprite;       /* Base map sprite image path. */
    const char*  race;

    int          max_hp;
    int          max_moves;
    int          max_xp;
    int          level;
    WL_Alignment alignment;

    /* resistance[WL_DMG_BLADE..WL_DMG_ARCANE]: positive = resists, negative = vulnerable. */
    int          resistance[WL_DMG_COUNT];

    WL_Attack    attacks[8];
    int          n_attacks;

    const char*  abilities[8];  /* Ability names, e.g. "leadership", "skirmisher". */
    int          n_abilities;

    const char*  advances_to[8]; /* Type ids this type can promote to. */
    int          n_advances;

    int          cost;          /* Default recruit cost. */
    int          recall_cost;   /* Default recall cost (-1 = global default). */
} WL_UnitType;

/* Snapshot of a specific unit instance on the map or in a recall list. */
typedef struct {
    const char*        id;
    const char*        type_id;
    const char*        name;        /* Custom name if given, else type name. */
    const char*        portrait;
    const char*        sprite;      /* Base map sprite image path. */
    int                side;
    WL_Loc             loc;         /* {0,0} if in recall list. */

    int                hp;
    int                max_hp;
    int                xp;
    int                max_xp;
    int                level;
    int                moves;
    int                max_moves;

    WL_Alignment       alignment;
    WL_UnitStatusFlags status;
    WL_UnitCapability  capability;  /* What can this unit still do this turn? */

    /* resistance[WL_DMG_BLADE..WL_DMG_ARCANE] */
    int                resistance[WL_DMG_COUNT];

    WL_Attack          attacks[8];
    int                n_attacks;

    const char*        traits[8];   /* e.g. "Quick", "Resilient". */
    int                n_traits;
    const char*        abilities[8];
    int                n_abilities;

    const char*        advances_to[8];
    int                n_advances;

    /*
     * Upkeep cost in gold per turn.  0 means the unit is free (loyal or
     * explicitly free-upkeep).  Positive values equal the unit's level
     * unless modified by a trait or ability.
     */
    int                upkeep;
    int                canrecruit;  /* Non-zero if this unit is a leader. */
} WL_Unit;

/* List of unit instances. */
typedef struct {
    WL_Unit* units;
    int      count;
} WL_UnitList;

/* Unit type ids available for recruitment by a side. */
typedef struct {
    const char* types[64];
    int         count;
} WL_RecruitList;

/* Terrain at a single hex. */
typedef struct {
    WL_Loc             loc;
    WL_TerrainCategory category;
    const char*        id;          /* Wesnoth terrain code, e.g. "Gg", "Ww". */
    const char*        name;
    const char*        icon;        /* Representative tile image path. */
    int                village_side; /* 0 = unowned. */
    int                starting_side; /* >0 if this is a side's starting hex. */
} WL_Terrain;

/* Full map snapshot. */
typedef struct {
    int        width;
    int        height;
    WL_Terrain hexes[]; /* Flexible array; width*height entries, row-major. */
} WL_MapData;

/*
 * Visibility of every hex on the map from a specific side's perspective.
 * Returned by wl_query_visibility().
 * values[] is row-major, same layout as WL_MapData.hexes[].
 */
typedef struct {
    int            side;
    int            width;
    int            height;
    WL_Visibility  values[]; /* Flexible array; width*height entries. */
} WL_VisibilityMap;

/*
 * One reachable hex entry.
 * defense is the unit-specific defense percentage on that terrain,
 * e.g. 60 for an Elvish Fighter in forest.
 */
typedef struct {
    WL_Loc loc;
    int    moves_left; /* Movement points remaining after arriving here. */
    int    defense;    /* Defense % for the queried unit on this terrain. */
    int    can_attack; /* Non-zero if an enemy adjacent to this hex is attackable. */
} WL_ReachHex;

typedef struct {
    WL_ReachHex* hexes;
    int          count;
} WL_ReachList;

/* One-sided combat preview (before the attack is committed). */
typedef struct {
    int               weapon_index;
    int               damage;
    int               num_blows;
    int               chance_to_hit;  /* Percentage. */
    int               expected_damage; /* damage * chance_to_hit% * num_blows. */
    WL_AttackSpecials specials;
} WL_CombatPreview;

/* One attack pairing (attacker weapon × defender counter-weapon). */
typedef struct {
    int              attacker_weapon_index;
    int              defender_weapon_index; /* -1 if defender cannot counter. */
    WL_CombatPreview attacker;
    WL_CombatPreview defender;
} WL_AttackOption;

typedef struct {
    WL_AttackOption* options;
    int              count;
    int              default_option; /* Pre-selected index (best by heuristic). */
} WL_AttackOptionList;

/* Team / side snapshot. */
typedef struct {
    int               side;
    const char*       name;
    const char*       faction;
    const char*       color;        /* CSS color name or hex string. */
    WL_SideController controller;
    int               gold;
    int               income;       /* Net income per turn (base + villages − upkeep). */
    int               base_income;
    int               village_gold; /* Gold per village owned. */
    int               support;      /* Free upkeep capacity (units below this pay nothing). */
    int               recall_cost;

    WL_Loc            villages[256];
    int               n_villages;

    const char*       objectives;
    int               objectives_changed;

    int               enemy_sides[8]; /* 1-based side numbers that are enemies. */
    int               n_enemy_sides;

    int               lost; /* Non-zero if this side has been defeated. */
} WL_Team;

/* Time-of-day entry. */
typedef struct {
    const char* id;
    const char* name;
    int         lawful_bonus;  /* Positive = benefits lawful, hurts chaotic. */
    const char* image;
    const char* mask_image;
} WL_TimeOfDay;

/* Top-level game information. */
typedef struct {
    WL_Phase     phase;
    int          turn;
    int          max_turns;     /* 0 = unlimited. */
    int          current_side;
    int          n_sides;
    WL_TimeOfDay tod;
    const char*  scenario_id;
    const char*  scenario_name;
    const char*  campaign_id;
    const char*  campaign_name;
    const char*  difficulty;
    WL_Outcome   outcome;       /* WL_OUTCOME_NONE while the game is in progress. */
} WL_GameInfo;

/* Summary of one available campaign. */
typedef struct {
    const char* id;
    const char* name;
    const char* description;
    const char* image;
    const char* icon;
    const char* difficulties[8]; /* e.g. "EASY", "NORMAL", "HARD". */
    int         n_difficulties;
    const char* first_scenario;
} WL_CampaignInfo;

typedef struct {
    WL_CampaignInfo* campaigns;
    int              count;
} WL_CampaignList;
```

---

## 4. Lifecycle functions

```c
/*
 * Initialise the engine.
 *
 *   data_path     – path to the Wesnoth data/ directory (or its parent).
 *   userdata_path – writable directory for saves, preferences, add-ons.
 *                   Pass NULL to use a platform default.
 *
 * Returns a handle on success, NULL on failure.
 * Call wl_last_error() to retrieve a human-readable error string.
 */
WL_Engine* wl_init(const char* data_path, const char* userdata_path);

/*
 * Destroy the engine and release all resources.
 * After this call the handle is invalid.
 */
void wl_shutdown(WL_Engine* engine);

/*
 * Return a static string describing the last error, or "" if none.
 * The string is owned by the engine and valid until the next API call.
 */
const char* wl_last_error(WL_Engine* engine);
```

---

## 5. Scenario setup

```c
/*
 * Return all campaigns bundled with the loaded data.
 * Free with wl_free().
 */
WL_CampaignList* wl_list_campaigns(WL_Engine* engine);

/*
 * Start a campaign from its first scenario.
 *
 *   campaign_id – e.g. "Heir_To_The_Throne", "tutorial".
 *   difficulty  – e.g. "EASY", "NORMAL", "HARD".  NULL → "NORMAL".
 *
 * Returns WL_OK on success.  Drive events with wl_step().
 */
WL_Status wl_start_campaign(WL_Engine* engine,
                             const char* campaign_id,
                             const char* difficulty);

/*
 * Start a specific scenario directly (bypasses campaign flow).
 * Useful for testing, replays, and scenario packs.
 *
 *   campaign_id – campaign whose defines to activate; NULL for none.
 *   scenario_id – e.g. "01_The_Elves_Besieged".
 *   difficulty  – NULL → "NORMAL".
 */
WL_Status wl_start_scenario(WL_Engine* engine,
                              const char* campaign_id,
                              const char* scenario_id,
                              const char* difficulty);

/*
 * Load a previously saved game from a file.
 * Not available in WASM; use wl_load_from_buffer() instead.
 */
WL_Status wl_load_save(WL_Engine* engine, const char* save_path);

/*
 * Load a previously saved game from a memory buffer.
 * The buffer is the raw bytes of a save file (gzip-compressed WML).
 * The engine does not retain the buffer after the call returns.
 * This is the preferred variant for WASM environments.
 */
WL_Status wl_load_from_buffer(WL_Engine*          engine,
                               const unsigned char* buf,
                               size_t               len);

/*
 * Save the current game state to a file.
 * Not available in WASM; use wl_save_to_buffer() instead.
 */
WL_Status wl_save(WL_Engine* engine, const char* save_path);

/*
 * Save the current game state into a freshly-allocated byte buffer.
 * The format is the same as wl_save() (gzip-compressed WML).
 * *out_size receives the number of bytes written.
 * Free the returned buffer with wl_free().
 * This is the preferred variant for WASM environments.
 */
unsigned char* wl_save_to_buffer(WL_Engine* engine, size_t* out_size);
```

---

## 6. Game pump

```c
/*
 * Advance the engine by one logical step and return the next event.
 *
 * Returns NULL when the engine is paused waiting for player input —
 * either because it is a human side's turn, or because a
 * WL_EVENT_CHOICE_NEEDED event has been delivered and wl_choose() has not
 * been called yet.
 *
 * Ownership: the returned pointer is owned by the engine and is valid only
 * until the next call to wl_step().  Do NOT pass it to wl_free().
 *
 * The engine only advances when wl_step() is called; it does nothing on its
 * own between calls.  Callers may drain the queue as fast as they like (for
 * AI-only simulation) or pace the calls to match animation timing.
 */
const WL_Event* wl_step(WL_Engine* engine);
```

---

## 7. Player actions

All action functions are valid only after `wl_step()` has returned `NULL`
and there is no pending `WL_EVENT_CHOICE_NEEDED`.  While a choice is pending
the only valid call is `wl_choose()`.

After every action call, resume the event pump with `wl_step()` to process
the consequences (unit movement events, combat events, etc.).

```c
/*
 * Move the unit at `from` to `to`.
 * The engine finds the shortest path automatically.
 * The unit may stop partway if it runs out of movement points.
 * If the destination is occupied by an enemy the move is rejected;
 * use wl_attack() for combat.
 */
WL_Status wl_move(WL_Engine* engine, WL_Loc from, WL_Loc to);

/*
 * Attack with the unit at `attacker_loc`, targeting the unit at
 * `defender_loc`.  Both units must be on adjacent hexes.
 *
 *   weapon_index – index into the attacker's WL_Unit.attacks[] array.
 *                  Pass -1 to let the engine choose the best weapon.
 */
WL_Status wl_attack(WL_Engine* engine,
                     WL_Loc attacker_loc,
                     WL_Loc defender_loc,
                     int    weapon_index);

/*
 * Recruit a new unit of the given type for the current side.
 *
 *   unit_type_id – type id string, e.g. "Elvish Fighter".
 *   at           – destination hex (must be empty castle adjacent to a
 *                  keep occupied by the side's leader).
 *                  Pass {0,0} to let the engine place the unit automatically.
 */
WL_Status wl_recruit(WL_Engine* engine,
                      const char* unit_type_id,
                      WL_Loc      at);

/*
 * Recall a unit from the current side's recall list onto the map.
 *
 *   unit_id – the unit's id string (from WL_Unit.id).
 *   at      – destination hex.  Pass {0,0} for automatic placement.
 */
WL_Status wl_recall(WL_Engine* engine,
                     const char* unit_id,
                     WL_Loc      at);

/*
 * Permanently dismiss a unit from the recall list.
 */
WL_Status wl_dismiss(WL_Engine* engine, const char* unit_id);

/*
 * End the current human side's turn.
 * Subsequent wl_step() calls will drive AI sides automatically.
 */
WL_Status wl_end_turn(WL_Engine* engine);

/*
 * Respond to a pending WL_EVENT_CHOICE_NEEDED.
 *
 *   option_index – 0-based index into event.choice_needed.options[].
 *
 * For WL_CHOICE_ADVANCE the option strings are unit type ids; pass them to
 * wl_query_unit_type() to show the player full stats before deciding.
 */
WL_Status wl_choose(WL_Engine* engine, int option_index);

/*
 * Undo the last undoable action taken this turn.
 * Attacks, village captures, and some WML-triggered actions are not undoable.
 * Returns WL_ERR_INVALID if nothing can be undone.
 */
WL_Status wl_undo(WL_Engine* engine);
```

---

## 8. State queries

May be called at any time after the scenario starts, including during AI
turns.  All returned pointers are snapshots; free with `wl_free()`.

```c
/*
 * High-level game information: turn, phase, current side, time-of-day, outcome.
 */
WL_GameInfo* wl_query_game(WL_Engine* engine);

/*
 * Full map snapshot: terrain category, id, name, icon, and village ownership
 * for every hex.
 */
WL_MapData* wl_query_map(WL_Engine* engine);

/*
 * Visibility of every hex from a given side's perspective.
 * Necessary for rendering fog-of-war, shroud overlays, and minimaps correctly.
 * side = 1-based side number.
 */
WL_VisibilityMap* wl_query_visibility(WL_Engine* engine, int side);

/*
 * All units currently on the map (regardless of visibility).
 * To respect fog-of-war, filter by wl_query_visibility() before displaying.
 */
WL_UnitList* wl_query_units(WL_Engine* engine);

/*
 * The unit at a specific hex, or NULL if the hex is empty.
 * Free with wl_free().
 */
WL_Unit* wl_query_unit_at(WL_Engine* engine, WL_Loc loc);

/*
 * Full statistics for a unit type by type id.
 * Use this to populate recruit dialogs, recall dialogs, and advancement
 * choice dialogs (WL_CHOICE_ADVANCE option strings are type ids).
 * Free with wl_free().
 */
WL_UnitType* wl_query_unit_type(WL_Engine* engine, const char* unit_type_id);

/*
 * Recall list for a side (units available to recall).
 */
WL_UnitList* wl_query_recall_list(WL_Engine* engine, int side);

/*
 * Recruit list for a side (unit type ids that can be recruited this turn).
 */
WL_RecruitList* wl_query_recruit_list(WL_Engine* engine, int side);

/*
 * Team / side information.
 */
WL_Team* wl_query_team(WL_Engine* engine, int side);

/*
 * Hexes reachable by the unit at `loc` this turn, with movement points
 * remaining and the unit-specific defense percentage on each terrain.
 * Also flags hexes from which the unit could attack an adjacent enemy.
 * Returns NULL if there is no friendly unit at loc, or it has no moves left.
 */
WL_ReachList* wl_query_reach(WL_Engine* engine, WL_Loc loc);

/*
 * All valid attack pairings if the unit at `attacker_loc` attacks the unit
 * at `defender_loc`, with a combat preview for each pairing.
 * Returns NULL if the attack is not possible.
 */
WL_AttackOptionList* wl_query_attack_options(WL_Engine* engine,
                                              WL_Loc attacker_loc,
                                              WL_Loc defender_loc);
```

---

## 9. Memory management

```c
/*
 * Free any snapshot returned by a wl_query_*(), wl_list_*(), or
 * wl_save_to_buffer() call.  Passing NULL is a no-op.
 *
 * Do NOT use this to free WL_Event pointers — those are engine-owned and
 * are invalidated automatically on the next wl_step() call.
 */
void wl_free(void* snapshot);
```

---

## 10. Typical usage patterns

### 10.1 Correct event-pump loop

The engine is purely synchronous.  The front-end owns the loop and calls
`wl_step()` until it returns `NULL`, then waits for user input.

```c
/*
 * Drain all pending events.  Returns true when the scenario has ended,
 * false when the engine is waiting for player input.
 */
bool pump(WL_Engine* e) {
    for (;;) {
        const WL_Event* ev = wl_step(e);
        if (ev == NULL)
            return false;  /* Waiting for input — stop pumping. */

        switch (ev->type) {

        case WL_EVENT_UNIT_MOVE:
            animate_move(ev->unit_move.unit_id,
                         ev->unit_move.path,
                         ev->unit_move.path_len);
            break;

        case WL_EVENT_UNIT_ATTACK:
            animate_combat(&ev->unit_attack);
            break;

        case WL_EVENT_UNIT_XP:
            show_xp_gain(ev->unit_xp.unit_id, ev->unit_xp.xp_gained);
            break;

        case WL_EVENT_MESSAGE:
            show_dialog(ev->message.speaker,
                        ev->message.portrait,
                        ev->message.text);
            break;

        case WL_EVENT_CHOICE_NEEDED:
            /* Must call wl_choose() before pumping resumes. */
            handle_choice(e, &ev->choice_needed);
            /* handle_choice calls wl_choose() internally, then we continue. */
            break;

        case WL_EVENT_SCENARIO_END:
            handle_end(ev->scenario_end.outcome,
                       ev->scenario_end.next_scenario);
            return true;

        default:
            break;
        }
    }
}

void on_user_action(WL_Engine* e) {
    /* Called after wl_move(), wl_attack(), wl_end_turn(), etc. */
    bool done = pump(e);
    if (!done) {
        render_board(e);   /* Re-render with updated state. */
    }
}
```

### 10.2 Campaign session

```c
WL_Engine* e = wl_init("/usr/share/wesnoth/data", NULL);

WL_CampaignList* cl = wl_list_campaigns(e);
int picked = show_campaign_menu(cl);
const char* cid = cl->campaigns[picked].id;
wl_free(cl);

wl_start_campaign(e, cid, "NORMAL");
pump(e);        /* Runs preload events; stops at first WAITING_FOR_INPUT. */
render_board(e);
/* Now wait for the human player to act. */
```

### 10.3 Moving a unit

```c
/* User clicked unit at (4,8) then destination (6,7). */
WL_ReachList* reach = wl_query_reach(e, (WL_Loc){4, 8});
bool reachable = false;
for (int i = 0; i < reach->count; i++) {
    if (reach->hexes[i].loc.x == 6 && reach->hexes[i].loc.y == 7) {
        reachable = true;
        /* Optionally show defense %: reach->hexes[i].defense */
        break;
    }
}
wl_free(reach);

if (reachable) {
    wl_move(e, (WL_Loc){4, 8}, (WL_Loc){6, 7});
    on_user_action(e);
}
```

### 10.4 Attacking

```c
/* User wants to attack from (6,7) to (7,7). */
WL_AttackOptionList* opts =
    wl_query_attack_options(e, (WL_Loc){6, 7}, (WL_Loc){7, 7});
if (!opts) { /* Attack not possible. */ return; }

int chosen = show_weapon_picker(opts);  /* Player sees damage previews. */
wl_attack(e, (WL_Loc){6, 7}, (WL_Loc){7, 7},
          opts->options[chosen].attacker_weapon_index);
wl_free(opts);
on_user_action(e);
```

### 10.5 Recruiting

```c
WL_RecruitList* rl = wl_query_recruit_list(e, current_side);
WL_Team*        tm = wl_query_team(e, current_side);

/* Show the player each option with full type stats. */
for (int i = 0; i < rl->count; i++) {
    WL_UnitType* ut = wl_query_unit_type(e, rl->types[i]);
    show_unit_type_card(ut);
    wl_free(ut);
}

int chosen = get_player_recruit_choice();
wl_recruit(e, rl->types[chosen], (WL_Loc){0, 0}); /* {0,0} = auto-place */
wl_free(rl);
wl_free(tm);
on_user_action(e);
```

### 10.6 Advancement choice

```c
/* Inside handle_choice(), when kind == WL_CHOICE_ADVANCE: */
void handle_advance(WL_Engine* e, const struct { ... } *choice) {
    /* Show the player the full stats of each candidate type. */
    for (int i = 0; i < choice->n_options; i++) {
        WL_UnitType* ut = wl_query_unit_type(e, choice->options[i]);
        show_advance_option(ut);
        wl_free(ut);
    }
    int picked = get_player_advance_choice();
    wl_choose(e, picked);
}
```

### 10.7 Fog-of-war rendering

```c
WL_MapData*       map = wl_query_map(e);
WL_VisibilityMap* vis = wl_query_visibility(e, current_side);
WL_UnitList*      ul  = wl_query_units(e);

for (int y = 0; y < map->height; y++) {
    for (int x = 0; x < map->width; x++) {
        int idx = y * map->width + x;
        WL_Terrain*    t = &map->hexes[idx];
        WL_Visibility  v = vis->values[idx];
        draw_hex(t, v);   /* Front-end applies shroud/fog overlay. */
    }
}
for (int i = 0; i < ul->count; i++) {
    WL_Unit* u = &ul->units[i];
    int idx = (u->loc.y - 1) * map->width + (u->loc.x - 1);
    if (vis->values[idx] == WL_VIS_VISIBLE)
        draw_unit(u);
}

wl_free(map);
wl_free(vis);
wl_free(ul);
```

### 10.8 WASM / in-memory save and load

```c
/* Save */
size_t save_len;
unsigned char* save_buf = wl_save_to_buffer(e, &save_len);
js_store_bytes("save_slot_1", save_buf, save_len);  /* Host JS call. */
wl_free(save_buf);

/* Load */
size_t load_len;
unsigned char* load_buf = js_load_bytes("save_slot_1", &load_len);
wl_load_from_buffer(e, load_buf, load_len);
free(load_buf);  /* Caller-allocated; freed by caller. */
pump(e);
```

---

## Appendix: header synopsis

```c
/* wesnothlite.h — complete public API */
#pragma once
#include <stddef.h>

/* ── types ──────────────────────────────────────────────────────────── */
typedef struct WL_Engine WL_Engine;
typedef struct { int x; int y; } WL_Loc;

/* … all enums, structs, and typedefs from sections 1–3 … */

/* ── lifecycle ──────────────────────────────────────────────────────── */
WL_Engine*           wl_init(const char*, const char*);
void                 wl_shutdown(WL_Engine*);
const char*          wl_last_error(WL_Engine*);

/* ── scenario setup ─────────────────────────────────────────────────── */
WL_CampaignList*     wl_list_campaigns(WL_Engine*);
WL_Status            wl_start_campaign(WL_Engine*, const char*, const char*);
WL_Status            wl_start_scenario(WL_Engine*, const char*, const char*, const char*);
WL_Status            wl_load_save(WL_Engine*, const char*);
WL_Status            wl_load_from_buffer(WL_Engine*, const unsigned char*, size_t);
WL_Status            wl_save(WL_Engine*, const char*);
unsigned char*       wl_save_to_buffer(WL_Engine*, size_t*);

/* ── pump ───────────────────────────────────────────────────────────── */
const WL_Event*      wl_step(WL_Engine*);

/* ── actions ────────────────────────────────────────────────────────── */
WL_Status            wl_move(WL_Engine*, WL_Loc, WL_Loc);
WL_Status            wl_attack(WL_Engine*, WL_Loc, WL_Loc, int);
WL_Status            wl_recruit(WL_Engine*, const char*, WL_Loc);
WL_Status            wl_recall(WL_Engine*, const char*, WL_Loc);
WL_Status            wl_dismiss(WL_Engine*, const char*);
WL_Status            wl_end_turn(WL_Engine*);
WL_Status            wl_choose(WL_Engine*, int);
WL_Status            wl_undo(WL_Engine*);

/* ── queries ────────────────────────────────────────────────────────── */
WL_GameInfo*         wl_query_game(WL_Engine*);
WL_MapData*          wl_query_map(WL_Engine*);
WL_VisibilityMap*    wl_query_visibility(WL_Engine*, int side);
WL_UnitList*         wl_query_units(WL_Engine*);
WL_Unit*             wl_query_unit_at(WL_Engine*, WL_Loc);
WL_UnitType*         wl_query_unit_type(WL_Engine*, const char* unit_type_id);
WL_UnitList*         wl_query_recall_list(WL_Engine*, int side);
WL_RecruitList*      wl_query_recruit_list(WL_Engine*, int side);
WL_Team*             wl_query_team(WL_Engine*, int side);
WL_ReachList*        wl_query_reach(WL_Engine*, WL_Loc);
WL_AttackOptionList* wl_query_attack_options(WL_Engine*, WL_Loc, WL_Loc);

/* ── memory ─────────────────────────────────────────────────────────── */
void                 wl_free(void*);
```

Total public API surface: **3 lifecycle + 7 setup + 1 pump + 8 actions + 11 queries + 1 free = 31 functions**.
