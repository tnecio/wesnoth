
- Board
    - game : Game
    - Interface
        - InputHandler
        - Selection
    - Visual Effects
        - Floating Text
    - Audio
        - Music
        - Sound Effects

## State

----

Game state – describes all the information needed from the engine to render the current state of the game

- Game
    - hexes: list[Hex]
    - sides: list[Side]
    - units: list[Unit]
    - villages: list[Village]
    - tod: DAWN | MORNING | AFTERNOON | DUSK | FIRST_WATCH | SECOND_WATCH | CAVE | INDOOR
    - tod_cycle_length: int
    - turn: int
    - maxTurn: int

- Hex
    - loc: Loc
    - terrain: str (specific type)
    - terrain_type: str (general type)
    - terrain_render: Sprite (note: it depends on neighbouring hexes)
    - illumination: NORMAL | ILLUMINATED
    - can_recruit: bool
    - visibility: list[SideVisibility]
    - items: list[Sprite]

- SideVisibility
    - side_id: int
    - visibility: NONE | FOG | SHROUD

- Side
    - id: int
    - gold: int
    - income: int
    - is_player_side: bool
    - is_enemy_side: bool
    - color: (int, int, int)

- Unit
    - id: str
    - loc: Loc
    - name: str
    - side: int
    - type_id: str
    - level: int
    - tod_type: LAWFUL | NEUTRAL | CHAOTIC | IN_BETWEEN
    - hp: int
    - maxHp: int
    - xp: int
    - maxXp: int
    - mp: int
    - maxMp: int
    - current_defence: int
    - race: str
    - attributes: list[str]
    - facing: LEFT | RIGHT
    - weapons: list[Weapon]
    - status_effects: Bitmap (SLOWED | POISONED | ...)
    - is_leader: bool
    - is_loyal: bool  (derived: "loyal" in attributes)
    - can_move: bool (derived: mp != 0)
    - sprite: Sprite
    - idle_animation: Animation

- Weapon
    - id: str
    - name: str
    - count: int
    - damage: int
    - range: MELEE | RANGED
    - type: blade | pierce | impact | fire | cold | arcane
    - specials: BitMap
    - icon: Sprite

- Village
    - loc: Loc
    - side: int | None
    - sprite: Sprite

Display is governed by the Sprite and Animation structures:

- Sprite
    - image_path: str
    - transformation: TODO
    - tint: (r: int, g: int, b: int, alpha: int)
    - overlays: list[str]

- Animation:
    - frames: Frame

- Frame:
    - sprite: Sprite
    - duration_ms: int
    - sound_effect_path: str (empty if no sound)

----

Reachability map is used when a unit is selected to show its range of movement

- ReachabilityMap: map[Loc, Reachability]

- Reachability
    - mp_left: int (reachable if mp_left >= 0)
    - defense: int
    - can_attack_from: bool
    - can_attack_unit_at: bool

----

CombatSimulation is used for CombatModal

- CombatSimulation
    - attacker: Unit
    - defender: Unit
    - options: list[CombatVariant]

- CombatVariant
    - attacker_weapon: Weapon
    - defender_weapon: Weapon
    - attacker_damage: DamageSimulation
    - defender_damage: DamageSimulation

- DamageSimulation
    - basic_damage: int
    - modifiers: list[str]
    - effective_damage: int
    - hits: int
    - hit_chance: int
    - simulation_results: list[(int, float)]  (first=hp after combat, second=chance in percent)

----

Recruit and recall lists are used for recruit/recall modal

- RecruitableUnit
    - cost: int
    - unit: Unit  (for recruitment, this "unit" has many fields empty/unset)

----

## Commands, Errors, Events, Queries, Answers

### Commands

Engine Start
- Start Campaign: (campaign_id: str)
- Load Save: (save: ByteArray)

Unit actions

- Move: (from: Loc, to: Loc)
- Attack: (from: Loc, at: Loc, weapon_id: int)

Unit management

- Recruit: (unit_type: str, at: Loc)
- Recall: (unit_id: str, at: Loc)
- Dismiss: (unit_id: str)
- Rename: (unit_id: str, new_name: str)

Other

- End Turn: ()
- Choose: (choice_id: int)
- Undo: ()
- Set Label: (where: Loc, text: str, for_all_sides: bool)
- Remove Label: (where: Loc)

### Events

Input

- Waiting For Input: ()

Scenario lifecycle

- Loading Progress: (percentage: int)
- Start Scenario: (initial_state: Game)
- End Scenario: (victor: int, outcome: str)
- End Campaign: (result: str)

Turn flow

- Start Turn: (turn_no: int)
- Start Side Turn: (side_id: int)

Unit

TODO: replace unit_id: str with at: Loc if it makes it more convenient to implement
- Move: (unit_id: str, from: Loc, to: Loc)
    - this may need to be step-by-step if there is fog revealing on the way, i.e list of events like [Move, FogUpdate, Move, FogUpdate, ...]
- Unit Update: (unit_id: str, new_state: Unit)
    - this + Animate can represent: xp change, advnacement, healing, status change, etc.
- Spawn: (where: Loc, who: Unit)
- Despawn: (unit_id: str)
- Animate: (unit_id: str, animation: Animation)

Map

- Capture Village: (loc: Loc, side_id: int)
- Fog Update: (none: list[Loc], fog: list[Loc], shroud: list[Loc])
- Hex Update: (loc: Loc, hex: Hex)
- Map Update: (state: Game)

Side

- Side Update: (side_id: int, new_state: Side)

Narrative

- Show Level Up Modal: (options: list[Unit])
- Show Message: (message: Message)
- Show Story: (story_slide: StorySlide)
- Update Objectives: (new_objectives: str)

Audio

- Play Sound: (sound_path: str)
- Change Music: (music: Music)

Other

- Delay: (delay_ms: int)
- Scroll: (to: Loc)
- Show Label: (where: Loc, text: str)
- Remove Label: (where: Loc)
- Show Text: (text: str, length_ms: int)

----

Supporting structures:

- Message
    - text: str
    - speaker_id: str
    - portrait_path: str
    - options: list[str]

- StorySlide
    - text: str
    - background_path: str
    - overlays: list[str] (TBD)

- Music
    - TBD

### Queries and Answers

- GameStateQuery: () -> Game
- RecallListQuery: (side: int) -> list[RecruitableUnit]
- RecruitListQuery: (side: int) -> list[RecruitableUnit]
- SimulateCombatQuery: (attacker_id: str, defender_id: str) -> CombatSimulation
- ReachabilityQuery: (unit_id: str) -> ReachabilityMap
- SaveGameQuery: () -> ByteArray
- CampaignsQuery: () -> list[Campaign]
- CanUndoQuery: () -> bool

----

Supporting structures

- Campaign
    - name: str
    - icon: Sprite
    - background_path: str
    - image_path: str
    - description: str
    - difficulties: list[str]

## Engine API

- init_engine(data_path: str, userdata_path: str, locale: str) -> Engine
- Engine
    - query_*(Query) -> Answer (one method per query type, returning corresponding answer type)
    - sendCommand(Command) -> ()
    - run() -> list[Event] (multiple Events can be returned simultaneously)

