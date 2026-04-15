# Wesnothlite Animation and Terrain Rendering

## Overview

The wesnothlite API exposes two methods for extracting rendering data from the C++ engine:

- **`queryTerrainAt(x, y)`** — returns the full terrain image stack for a hex (accounts for
  neighbouring terrain; background and foreground layers separated)
- **`queryUnitTypeAnimations(typeId)`** — returns all animation frame sequences for a unit type,
  grouped by event name

Both methods return pure data (image paths, frame durations, sounds). The frontend owns all
rendering and animation playback; the C++ display subsystem remains fully stubbed.

---

## How WML Animations Work

### Source of truth: `unit_type::animations()`

`unit_animation::fill_initial_animations(animations_, get_cfg())` is called lazily from
`unit_type::animations()`. It calls `add_anims(animations, cfg)`, which reads **only** from the
unit type's own WML config node. No other WML location contributes to unit sprite animations.

`add_anims` handles all WML animation shorthand tags:

| WML tag | Event name set |
|---|---|
| `[animation]` with `apply_to=X` | X (supports `[if]/[else]` branching) |
| `[standing_anim]` | `standing`, also `default` |
| `[movement_anim]` | `movement` |
| `[idle_anim]` | `idling` |
| `[attack]` | `attack` |
| `[defend]` | `defend` |
| `[death_anim]` | `death` |
| `[victory_anim]` | `victory` |
| `[recruit_anim]` | `recruited` |
| `[levelin_anim]` | `levelin` |
| `[levelout_anim]` | `levelout` |
| `[healing_anim]` | `healing` |
| `[healed_anim]` | `healed` |
| `[poison_anim]` | `poisoned` |
| `[resistance_anim]` | `resistance` |

`fill_initial_animations` also **synthesises defaults** from `cfg["image"]` for any event not
explicitly defined in WML. `unit_type::animations()` therefore always returns a complete set.

### `unit_animation` internal structure

Each `unit_animation` holds:
- `event_` — the event strings (accessible via public `get_flags()`)
- `unit_anim_` — a `particle` (= `animated<unit_frame>`) with the main sprite frame sequence
- `terrain_types_` — optional terrain filter
- `directions_` — optional facing direction filter
- `hits_` — optional hit-result filter (for attack/defend animations)
- `primary_attack_filter_`, `secondary_attack_filter_` — optional attack-type filters

`unit_animation::matches()` scores animations to find the best one for a given game context.
It previously called `display::get_singleton()` in exactly two places:
1. `disp.context().map().get_terrain(loc)` — terrain type at the unit's location
2. `disp.context().units().find(second_loc)` — unit at a second location (secondary_unit_filter_)

The new `matches_headless()` function accepts these as explicit arguments instead, removing the
display dependency.

### Frame data access

Each `unit_frame` in `particle::get_frame(n)` exposes:
- `parameters(t)` → `frame_parameters` containing `image` (locator), `sound`, `image_mod`, etc.
- `duration()` → total duration of the WML `[frame]` tag

Per-frame duration within `animated<unit_frame>` was not previously accessible via public API —
`get_frame_duration(n)` was added to `animated<T>`.

### Note on progressive_image

A WML `[frame]` with `image="a.png:100,b.png:200"` creates ONE `unit_frame` with a
`progressive_image` internally cycling between `a.png` at t=0..100ms and `b.png` at t=100..300ms.
`parameters(0ms)` always returns the first image. Full sub-frame expansion (exposing all
progressive sub-frames as separate entries) requires access to the private
`frame_parsed_parameters::image_.data()` field and is a future enhancement.

---

## How Terrain Image Resolution Works

### `terrain_builder` pipeline

```
terrain_builder::set_terrain_rules_cfg(cfg)
  ↓ STATIC: parses all [terrain_graphics] WML rules into building_rules_ multiset

terrain_builder(level_cfg, gamemap*, offmap_img, draw_border)
  ↓ applies static rules to the map; builds per-hex tile image cache

get_terrain_at(loc, tod_id, BACKGROUND | FOREGROUND)
  ↓ returns const imagelist* = const vector<animated<image::locator>>*

Each animated<image::locator> = one image layer for this hex
  May be static (1 frame) or animated (multiple frames cycling — water, village smoke, etc.)
```

`set_terrain_rules_cfg` is normally called from the `display` constructor, which is not executed
in the wesnothlite headless build. It must be called manually after `init_game_config` completes.

### Background vs foreground

The threshold is `terrain_builder::UNITPOS = 36 + 18 = 54`.  
Images with `basey <= 54` are background (drawn below unit sprites).  
Images with `basey > 54` are foreground (drawn above unit sprites).

The caller passes `BACKGROUND` or `FOREGROUND` to `get_terrain_at`; the builder splits them.

---

## API Reference

### `queryTerrainAt(x, y)` → `WlTerrainLayers`

Returns the image layers for the hex at WML coordinates (x, y). Call once per hex at scenario
start; result is stable until map reload.

```ts
interface WlTerrainFrame {
  path:       string   // resolved VFS path, ready for PIXI.Assets.load()
  mods:       string   // IPF modifier string (e.g. "~RC(coast>sea)~FL()")
  durationMs: number   // 0 = static; >0 = animated frame (part of a cycle)
}
interface WlTerrainLayers {
  background: WlTerrainFrame[]
  foreground:  WlTerrainFrame[]
}
```

Rendering rule:
- Blit all `background` entries in order (bottom to top)
- Draw unit sprite
- Blit all `foreground` entries in order
- Entries from the same `animated<image::locator>` share one animation cycle timer

### `queryUnitTypeAnimations(typeId)` → `WlUnitAnimations`

Returns all animation frame sequences for a unit type. Call once per type; cache the result.

```ts
interface WlAnimFrame {
  image:      string   // resolved path
  mods:       string   // IPF modifiers (team color, flip, etc.)
  durationMs: number   // display duration of this frame in ms
  sound:      string   // "" if none; play at frame start if non-empty
}
type WlUnitAnimations = Record<string, WlAnimFrame[]>
```

The key is the `apply_to` event string from WML: `"standing"`, `"movement"`, `"attack"`,
`"defend"`, `"death"`, `"victory"`, etc.

Animation selection uses `matches_headless()` with a default context (grass terrain, no attack
filter). This gives the correct default animation for almost all units. Terrain-conditional or
direction-conditional variants that differ from the default are a future enhancement.

### Frontend `AnimationCache`

```ts
class AnimationCache {
  get(typeId, engine): WlUnitAnimations   // caches by type_id
  static resolve(anims, event): WlAnimFrame[]
  // fallback chain: exact match → event prefix → "standing"
}
```

---

## Implementation Notes

### `terrain_builder` initialization timing

`set_terrain_rules_cfg` must be called after `init_game_config` completes and before any
`queryTerrainAt` call. It is called in `WesnothEngine::WesnothEngine()` after the config manager
init. The `terrain_builder` instance itself is created lazily in `queryTerrainAt()` (requires
`resources::gameboard` to be live, which only happens after a scenario starts).

### `terrain_builder` per-scenario instance

The builder is stored as `std::unique_ptr<terrain_builder> tbuilder` in `WLEngineImpl`.
It is reset when the scenario ends / map reloads. Passing `config{}` as the level config omits
scenario-local `[terrain_graphics]` rules (uncommon; can be improved later).

### Internal event names filtered

Events prefixed with `_` (`_disabled_`, `_ghosted_`, `_disabled_selected_`, etc.) are internal
animation states used by the display system. They are excluded from `queryUnitTypeAnimations`
output.

---

## Files Changed

| File | What changed |
|---|---|
| `src/animated.hpp` + `.tpp` | Added `get_frame_duration(n)` public getter |
| `src/units/animation.hpp` | Added `matches_headless()` declaration; `get_frames_count()`, `get_frame(n)`, `get_frame_duration(n)` wrappers |
| `src/units/animation.cpp` | Implemented `matches_headless()`; refactored `matches()` to delegate to it |
| `src/wesnothlite/wl_engine.hpp` | Added `tbuilder` field to `WLEngineImpl` |
| `src/wesnothlite/wesnoth_engine.hpp` | Declared `queryTerrainAt`, `queryUnitTypeAnimations` |
| `src/wesnothlite/wesnoth_engine.cpp` | `set_terrain_rules_cfg` at init; implemented both query methods |
| `src/wesnothlite/wl_bindings.cpp` | Exposed both methods via Embind |
| `frontend/src/engine/protocol.ts` | Added `WlTerrainFrame`, `WlTerrainLayers`, `WlAnimFrame`, `WlUnitAnimations` |
| `frontend/src/board/game/AnimationCache.ts` | New file: cache + fallback resolver |
