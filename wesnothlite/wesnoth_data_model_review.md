# Review: wesnoth_data_model.md

Comments are grouped by topic. Each item is self-contained so it can be addressed or dismissed individually.

---

## State omissions

**[S1] `side_turn_end` event is missing.**
The current engine emits `SideTurnEnd` so the UI knows when the AI finishes its turn and the player's turn is about to begin. The model only has `Start Side Turn`. Without this, the frontend has no clean signal to re-enable UI after AI moves.
- What about StartSideTurn(player's side id) + WaitForInput?

**[S2] `waiting_for_input` signal is absent.**
The model drops this sentinel entirely. In the `run()` model, an empty return list implicitly means "waiting", but the model never documents this. The distinction between "no events happened yet" and "engine is now waiting for player input" is essential for correctness — the client must not send commands before the engine is ready.
- Re-added.

**[S3] Per-side fog visibility is not representable.**
`Hex.fog` is a single field, presumably for the player's perspective. The current `queryVisibility(side)` is parameterized. Per-side visibility is needed for replays, spectator modes, or scenarios with multiple human players.
- Addressed, added SideVisibility.

**[S4] Village ownership is absent from `Start Scenario`.**
`Game.villages` is a list, but the model does not specify that `Start Scenario`'s `initial_state` must populate it. Without it, the client has no village ownership state before the first `Capture Village` event. The current `queryMap()` encodes `villageSide` per hex and would need to be mapped over on scenario start.
- initial_state will contain the villages; I thought it was obvious.

**[S5] Recruit vs. recall distinction is lost in `Spawn`.**
`Spawn: (where, who: Unit)` gives no signal about whether the unit is being recruited from a castle hex or recalled from the recall list. This matters for animation (and for state tracking). The current engine distinguishes these at the event level.
- animation will be handled by Animate. We want to decouple animation from state changes

**[S6] `Unit.facing` is absent.**
The model has `sprite` and `idle_animation` but no `facing: LEFT | RIGHT`. Without it the client cannot mirror sprites correctly for units facing different directions.
- WDYM? This is omission in the current implementation, not in the proposed model.

**[S7] Effective ToD bonus is absent.**
The model has `tod_type: LAWFUL | ...` on Unit but not the current effective combat bonus/penalty (derived from `tod_type` and the current `tod.lawful_bonus`). Useful for combat preview UI without requiring the client to recompute it.
- Not needed, see comment to D5

**[S8] `Unit.defense` at standing position is absent.**
`Reachability.defense` is given per hex, but the unit's defense at its *current* hex is not exposed anywhere in the state model. The client would need to synthesize it from the reachability query or by a separate computation.
- Noted; added to Unit.

**[S9] No undo availability flag.**
`Undo: ()` is a command but the model gives the client no way to know whether undo is currently valid. Undo is only available after a move that has not ended the turn; it cannot undo attacks or recruits. The UI needs this to enable/disable the undo button.
- Added CanUndoQuery

**[S10] `items: list[Item]` has no defined structure beyond `icon`.**
Items placed by scenario WML can have flags, descriptions, and trigger effects. Modeling them as icon-only makes them displayable but not interactable in any meaningful way. Either expand the model or scope items explicitly as "display-only overlays."
- Moved items into Hex as list of sprites.

---

## Factual errors in the model

**[F1] `Weapon.type` enum is wrong.**
The model lists `PIERCE | CRUSH | ASTRAL`. Wesnoth's actual damage types are `blade | pierce | impact | fire | cold | arcane`. `CRUSH` does not exist (it is `impact`); `ASTRAL` is not a standard Wesnoth type.
- This was just an example.

**[F2] `Weapon.id: int` should be `str`.**
Attack IDs in Wesnoth are strings (the `id=` field in `[attack]` WML). An integer maps to the weapon *index* used in `sendAttack`, not to a stable identity. The two should be kept separate: `index: int` for command dispatch, `id: str` for identity.
- Fixed

**[F3] Attack specials are not modeled.**
`Weapon` has `attributes: list[str]` but no structured specials. Wesnoth has named specials (poison, slow, drain, backstab, firststrike, marksman, etc.) that the combat UI and simulation must understand. A string list is fine if documented as containing these exact Wesnoth special names, but needs to be explicit.
- Done

---

## API design concerns

**[D1] `run(Command) -> list[Event]` does not eliminate the client drain loop.**
The proposal argues this reduces JS↔WASM boundary crossings. In practice the client still has to iterate all returned events sequentially and run animations one by one — it has its own drain loop. The actual saving is ~one `step()` call per event. The cost is that pausing mid-batch becomes harder: if a `choice_needed` or narrative event arrives mid-batch the client must still stop, await user input, and resume. With per-event `step()` this happens naturally; with a batch return the client would need to split the batch at the blocking event. Overall the batch model is not obviously better.
- What about situation where you get a lot of small events at once, like unit moves one step, does some animation, fog clears somehwere, unit moves, etc.

**[D2] `query(Query) -> Answer` as a single polymorphic entry point loses type safety.**
A single `query()` taking a discriminated union and returning a discriminated union forces the caller to pattern-match on the answer type. Named methods (`queryReach()`, `queryGame()`, etc.) are more idiomatic for TypeScript / Embind and produce better type inference with no extra work.
- Fixed

**[D3] `Reachability.can_attack: bool` conflates two distinct predicates.**
`can_attack` as written appears to mean "this hex contains an attackable enemy." The current `canAttackFrom` means "if I land on this hex, I can attack an adjacent enemy from here." Both are needed: the first controls which enemy hexes to highlight red; the second controls from which movement hexes attacks are possible. Conflating them loses information.
- Fixed

**[D4] `Message.options: list[str]` inside `Message` creates a type ambiguity.**
A `Message` with `options: []` is indistinguishable from a `Message` with no choice — should the UI show a dismiss button, nothing, or a choice list? The current separation of `message` (always dismiss-to-continue) from `choice_needed` (requires a selection) is clearer. If merging them, `options` should be nullable and the semantics of null vs. empty documented explicitly.
- It is not a problem; if the list is empty then there are no choices so it is the same as the plain Message.

**[D5] `CombatSimulation` probability distributions are a performance risk.**
`simulation_results: list[(hp, float)]` is the full hp probability distribution, computed via the stochastic combat simulation algorithm (O(max_hp²) per weapon combination, worse with berserk/swarm). If `SimulateCombatQuery` is called on every hover over an enemy hex, it will cause visible hitches on slow hardware. A per-(attacker, defender, weapon-pair) cache is required.
- This does not show on hover, only in the Combat modal, once the player actually initiates the combat.

**[D6] `villages` in `Game` + `Capture Village` events creates a dual source of truth.**
The client must merge the initial village list from `Start Scenario` with incremental `Capture Village` events. Any missed or reordered event corrupts the village-ownership map. The current approach of encoding `villageSide` per hex in the map snapshot and invalidating the map on capture is less elegant but more robust.
- There should be no reordered or missed events -- this is not RPC, it's all running in the same machine.

---

## Feasibility concerns

**[F_IMPL1] `Sprite`, `Animation`, `Frame`, `terrain_render`, `idle_animation` — the entire rendering data layer is not feasible with small modifications.**
Getting `terrain_render: Sprite` that accounts for neighbouring-hex blending requires the display subsystem's terrain rendering pipeline. Getting `idle_animation: Animation` for a unit requires the `unit_animation` / `unit_frame` system. Both are entirely stubbed out (`stubs/display.cpp`, `stubs/picture.cpp`, `stubs/units/animation.cpp`, etc.). Un-stubbing them or reimplementing their logic is a large undertaking and would couple the engine layer to display-timing concepts. This is the single biggest gap between the proposal and what is feasible with targeted additions to the current codebase.
- Ok; I see your point. I will address this separately as it requires careful consideration.

**[F_IMPL2] `Sprite.transformation: TODO` leaves the most critical display field undefined.**
Wesnoth's image compositor (`~RC` for team recolor, `~BLIT`, `~SCALE`, `~ROTATE`, `~CROP`, `~ADJUST_ALPHA`) is a mini-language applied to image paths. Without it, all unit sprites appear in the wrong team color, and terrain compositing is lost. Any `Sprite` representation must either expose the raw compositor suffix string or decompose it into structured fields.
- Yes, this will be decomposed into structured fields.

**[F_IMPL3] `Show Level Up Modal: (options: list[Unit])` requires synthesizing virtual units.**
The current engine surfaces advancement options as strings (type IDs). Building a full `Unit` object for each potential advancement requires reading the type registry and constructing virtual unit instances. Feasible but requires expanding `queryUnitType()` and adding a new synthesis path.
- Engine can populate the Unit fields from UnitType easily; other fields can be left empty. This won't matter for the purposes of recruitment dialogue anyway.

**[F_IMPL4] `Fog Update` events require new hooks.**
Fog/shroud revelation currently happens inside the headless game thread with no hook. Adding incremental `Fog Update` events requires intercepting `team::set_fog()` and related shroud-reveal paths, similar to how `wl_lua.cpp` hooks move and die events. Medium effort.
- Yes but we want to update the fog incrementally as a unit moves so this is actually by design.

**[F_IMPL5] `Side Update` events require new hooks.**
Gold and income changes occur at well-defined points (start of side turn, village capture, WML effects). Emitting a `Side Update` event at those points is feasible but requires identifying all code paths that modify team gold/income and adding hooks.
- How else would you express change of the side stats, like gold, when e.g. a WML-defined Lua event fires?