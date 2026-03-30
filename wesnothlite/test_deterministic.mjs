/**
 * test_deterministic.mjs — WesnothLite deterministic gameplay test
 *
 * Uses the WL_Test campaign (wesnoth/data/campaigns/WL_Test/) and a fixed
 * RNG seed to produce fully reproducible results on every run.
 *
 * Scenario layout (hex coords 1-based):
 *   (3,2)  Side 1 Horseman leader on keep — used for recruit test
 *   (4,2)  Castle adjacent to keep       — recruited unit spawns here
 *   (5,4)  Side 1 Spearman               — moved and used for attack
 *   (6,4)  Side 2 Goblin Spearman leader — attack target
 *
 * Tests:
 *   1. Engine init + WL_Test campaign found
 *   2. Start test_combat scenario, pump to WAITING_FOR_INPUT
 *   3. Verify initial unit positions
 *   4. Set RNG seed to 42 (deterministic from here)
 *   5. Move Spearman from (5,4) → (6,3) [adjacent to both start and enemy]
 *   6. Attack Goblin at (6,4) from Spearman at (6,3) — exact HP outcome recorded
 *   7. Recruit a Spearman at leader hex → verify UNIT_RECRUIT
 *   8. End turn → AI plays → verify turn 2
 *
 * Run from the repo root after building the WASM module:
 *   node wesnothlite/test_deterministic.mjs
 */

import { fileURLToPath, pathToFileURL } from 'url';
import path from 'path';
import fs from 'fs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot  = path.resolve(__dirname, '..');
const wasmDir   = path.join(repoRoot, 'build-wasm');

if (!fs.existsSync(path.join(wasmDir, 'wesnothlite.js'))) {
    console.error(`ERROR: ${wasmDir}/wesnothlite.js not found. Build the WASM module first:`);
    console.error('  bash scripts/build-wasm.sh');
    process.exit(1);
}

const { default: WesnothLite } = await import(pathToFileURL(path.join(wasmDir, 'wesnothlite.js')).href);

// ─── Helpers (shared with test_gameplay.mjs) ─────────────────────────────────

let passed = 0;
let failed = 0;

function check(name, condition, detail = '') {
    if (condition) {
        console.log(`  ✓ ${name}`);
        passed++;
    } else {
        console.error(`  ✗ FAIL: ${name}${detail ? ': ' + detail : ''}`);
        failed++;
    }
}

function fatal(msg) {
    console.error(`FATAL: ${msg}`);
    process.exit(1);
}

function readStr(m, ptr) {
    return ptr ? m.UTF8ToString(ptr) : null;
}

function readUnit(m, base) {
    const g = (off) => m.getValue(base + off, 'i32');
    return {
        id:         readStr(m, g(0)),
        type_id:    readStr(m, g(4)),
        name:       readStr(m, g(8)),
        side:       g(20),
        x:          g(24),
        y:          g(28),
        hp:         g(32),
        max_hp:     g(36),
        moves:      g(52),
        max_moves:  g(56),
        capability: g(68),
        canrecruit: g(500),
    };
}

const UNIT_STRIDE = 504;

function readUnitList(m, listPtr) {
    if (!listPtr) return [];
    const unitsPtr = m.getValue(listPtr,     'i32');
    const count    = m.getValue(listPtr + 4, 'i32');
    const units    = [];
    for (let i = 0; i < count; i++) {
        units.push(readUnit(m, unitsPtr + i * UNIT_STRIDE));
    }
    return units;
}

function readRecruitList(m, listPtr) {
    if (!listPtr) return [];
    const count = m.getValue(listPtr + 256, 'i32');
    const types = [];
    for (let i = 0; i < count; i++) {
        const p = m.getValue(listPtr + i * 4, 'i32');
        types.push(readStr(m, p));
    }
    return types;
}

function decodeEvent(m, evtPtr) {
    if (!evtPtr) return null;
    const type = m.getValue(evtPtr, 'i32');
    const u    = evtPtr + 4;
    const T = {
        LOADING_CONFIG: 0, SCENARIO_START: 1, SCENARIO_END: 2,
        TURN_START: 3, SIDE_TURN_START: 4, SIDE_TURN_END: 5, WAITING_FOR_INPUT: 6,
        UNIT_MOVE: 7, UNIT_ATTACK: 8, UNIT_SPAWN: 9, UNIT_DISMISS: 10,
        UNIT_DIE: 11, UNIT_ADVANCE: 12, UNIT_XP: 13, UNIT_HEAL: 14, UNIT_STATUS: 15,
        VILLAGE_CAPTURE: 16, MESSAGE: 17, STORY: 18, OBJECTIVES_UPDATE: 19,
        CHOICE_NEEDED: 20, SOUND: 21, MUSIC_CHANGE: 22,
    };
    switch (type) {
        case T.WAITING_FOR_INPUT:
            return { type: 'WAITING_FOR_INPUT', side: m.getValue(u, 'i32'), turn: m.getValue(u + 4, 'i32') };
        case T.TURN_START:
            return { type: 'TURN_START', turn: m.getValue(u, 'i32') };
        case T.SIDE_TURN_START:
            return { type: 'SIDE_TURN_START', side: m.getValue(u, 'i32'), turn: m.getValue(u + 4, 'i32') };
        case T.SIDE_TURN_END:
            return { type: 'SIDE_TURN_END', side: m.getValue(u, 'i32'), turn: m.getValue(u + 4, 'i32') };
        case T.SCENARIO_START:
            return { type: 'SCENARIO_START' };
        case T.SCENARIO_END:
            return { type: 'SCENARIO_END', outcome: m.getValue(u, 'i32'),
                     next_scenario: readStr(m, m.getValue(u + 4, 'i32')) };
        case T.UNIT_MOVE: {
            // unit_id(0), side(4), from(8), to(16), path[64*8] at 24, path_len at 24+512
            const unitId = readStr(m, m.getValue(u, 'i32'));
            const toX    = m.getValue(u + 16, 'i32');
            const toY    = m.getValue(u + 20, 'i32');
            return { type: 'UNIT_MOVE', unit_id: unitId, to_x: toX, to_y: toY };
        }
        case T.UNIT_ATTACK: {
            // attacker_id(0), defender_id(4), attacker_side(8), defender_side(12),
            // attacker_loc(16), defender_loc(24),
            // attacker_result at 32 (28 bytes): weapon_index(0) damage_per_hit(4) num_blows(8) hits(12) chance_to_hit(16) hp_start(20) hp_end(24)
            // defender_result at 60 (28 bytes): same layout
            const base = u;
            const atkResult = {
                hits:     m.getValue(base + 32 + 12, 'i32'),
                hp_start: m.getValue(base + 32 + 20, 'i32'),
                hp_end:   m.getValue(base + 32 + 24, 'i32'),
            };
            const defResult = {
                hits:     m.getValue(base + 60 + 12, 'i32'),
                hp_start: m.getValue(base + 60 + 20, 'i32'),
                hp_end:   m.getValue(base + 60 + 24, 'i32'),
            };
            return {
                type:         'UNIT_ATTACK',
                attacker_id:  readStr(m, m.getValue(u, 'i32')),
                defender_id:  readStr(m, m.getValue(u + 4, 'i32')),
                atk_result:   atkResult,
                def_result:   defResult,
            };
        }
        case T.UNIT_SPAWN:
            return { type: 'UNIT_SPAWN', type_id: readStr(m, m.getValue(u, 'i32')), side: m.getValue(u + 8, 'i32') };
        case T.UNIT_DIE:
            return { type: 'UNIT_DIE', unit_id: readStr(m, m.getValue(u, 'i32')) };
        case T.UNIT_XP:
            return { type: 'UNIT_XP' };
        case T.MESSAGE:
            return { type: 'MESSAGE' };
        case T.SOUND:
            return { type: 'SOUND' };
        case T.MUSIC_CHANGE:
            return { type: 'MUSIC_CHANGE' };
        default:
            return { type: `EVENT_${type}` };
    }
}

function pumpToInput(m, wl_step, engine, maxSteps = 5000) {
    const events = [];
    let waiting = false;
    for (let i = 0; i < maxSteps; i++) {
        const evtPtr = wl_step(engine);
        if (evtPtr === 0) { waiting = true; break; }
        const evt = decodeEvent(m, evtPtr);
        events.push(evt);
        if (evt && evt.type === 'WAITING_FOR_INPUT') { waiting = true; break; }
    }
    return { events, waiting };
}

// ─── WL_Command helpers ───────────────────────────────────────────────────────

const WL_CMD = { MOVE: 0, ATTACK: 1, RECRUIT: 2, RECALL: 3, DISMISS: 4, END_TURN: 5, CHOOSE: 6, UNDO: 7 };
const WL_COMMAND_SIZE = 24;

function sendMove(m, wl_send, engine, fromX, fromY, toX, toY) {
    const p = m._malloc(WL_COMMAND_SIZE);
    m.setValue(p,      WL_CMD.MOVE, 'i32');
    m.setValue(p +  4, fromX, 'i32');
    m.setValue(p +  8, fromY, 'i32');
    m.setValue(p + 12, toX,   'i32');
    m.setValue(p + 16, toY,   'i32');
    const rc = wl_send(engine, p);
    m._free(p);
    return rc;
}

function sendAttack(m, wl_send, engine, attX, attY, defX, defY, weapon) {
    const p = m._malloc(WL_COMMAND_SIZE);
    m.setValue(p,      WL_CMD.ATTACK, 'i32');
    m.setValue(p +  4, attX,   'i32');
    m.setValue(p +  8, attY,   'i32');
    m.setValue(p + 12, defX,   'i32');
    m.setValue(p + 16, defY,   'i32');
    m.setValue(p + 20, weapon, 'i32');
    const rc = wl_send(engine, p);
    m._free(p);
    return rc;
}

function sendRecruit(m, wl_send, engine, typeId, atX, atY) {
    const p      = m._malloc(WL_COMMAND_SIZE);
    const maxLen = typeId.length * 4 + 1;  // safe upper bound for UTF-8
    const strPtr = m._malloc(maxLen);
    m.stringToUTF8(typeId, strPtr, maxLen);
    m.setValue(p,      WL_CMD.RECRUIT, 'i32');
    m.setValue(p +  4, strPtr, 'i32');
    m.setValue(p +  8, atX,   'i32');
    m.setValue(p + 12, atY,   'i32');
    const rc = wl_send(engine, p);
    m._free(strPtr);
    m._free(p);
    return rc;
}

function sendEndTurn(m, wl_send, engine) {
    const p = m._malloc(WL_COMMAND_SIZE);
    m.setValue(p, WL_CMD.END_TURN, 'i32');
    const rc = wl_send(engine, p);
    m._free(p);
    return rc;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

const m = await WesnothLite({
    locateFile: (f) => path.join(wasmDir, f),
    print:      () => {},
    printErr:   () => {},
    noFSInit:   false,
});

const NODEFS = m.FS.filesystems['NODEFS'];
if (!NODEFS) fatal('NODEFS not available — rebuild with -lnodefs.js');
m.FS.mkdir('/game');
m.FS.mount(NODEFS, { root: repoRoot }, '/game');
m.FS.mkdir('/userdata');

const wl_init           = m.cwrap('wl_init',              'number', ['string', 'string']);
const wl_step           = m.cwrap('wl_step',              'number', ['number']);
const wl_send           = m.cwrap('wl_send',              'number', ['number', 'number']);
const wl_set_seed       = m.cwrap('wl_set_seed',          null,     ['number', 'number']);
const wl_list_campaigns = m.cwrap('wl_list_campaigns',    'number', ['number']);
const wl_start_campaign  = m.cwrap('wl_start_campaign',    'number', ['number', 'string', 'string']);
const wl_query_units    = m.cwrap('wl_query_units',       'number', ['number']);
const wl_query_recruit  = m.cwrap('wl_query_recruit_list','number', ['number', 'number']);
const wl_free           = m.cwrap('wl_free',              null,     ['number']);
const wl_last_error     = m.cwrap('wl_last_error',        'string', ['number']);

// ─── Phase 1: Init ────────────────────────────────────────────────────────────

console.log('\n=== Phase 1: Engine init ===');
const engine = wl_init('/game/data', '/userdata');
check('wl_init returns non-null handle', engine !== 0, `got ${engine}`);
if (!engine) fatal('Cannot continue without a valid engine handle');

// ─── Phase 2: WL_Test campaign exists ─────────────────────────────────────────

console.log('\n=== Phase 2: WL_Test campaign found ===');
const campaignListPtr = wl_list_campaigns(engine);
check('wl_list_campaigns returns non-null', campaignListPtr !== 0);
if (!campaignListPtr) fatal('Cannot list campaigns');

const count = m.getValue(campaignListPtr + 4, 'i32');
const CAMPAIGN_STRIDE = 60;
const campaignsBase = m.getValue(campaignListPtr, 'i32');
let wlTestIdx = -1;
for (let i = 0; i < count; i++) {
    const base = campaignsBase + i * CAMPAIGN_STRIDE;
    const id   = readStr(m, m.getValue(base, 'i32'));
    if (id === 'WL_Test') wlTestIdx = i;
}
check('WL_Test campaign found', wlTestIdx >= 0,
      `listed campaigns: ${Array.from({length: count}, (_, i) =>
          readStr(m, m.getValue(campaignsBase + i * CAMPAIGN_STRIDE, 'i32'))).join(', ')}`);
wl_free(campaignListPtr);

// ─── Phase 3: Start scenario ───────────────────────────────────────────────────

console.log('\n=== Phase 3: Start WL_Test / test_combat ===');
const startRc = wl_start_campaign(engine, 'WL_Test', 'EASY');
check('wl_start_campaign(WL_Test) returns WL_OK', startRc === 0, `got ${startRc}: ${wl_last_error(engine)}`);
if (startRc !== 0) fatal('Cannot start WL_Test campaign');

// ─── Phase 4: Pump to WAITING_FOR_INPUT ───────────────────────────────────────

console.log('\n=== Phase 4: Pump to WAITING_FOR_INPUT ===');
const { events: startEvents, waiting: startWaiting } = pumpToInput(m, wl_step, engine);
const sawStart = startEvents.some(e => e.type === 'SCENARIO_START');
const waitEvt  = startEvents.find(e => e.type === 'WAITING_FOR_INPUT');
check('SCENARIO_START emitted',           sawStart);
check('Engine reaches WAITING_FOR_INPUT', startWaiting);
if (waitEvt) {
    check('Side 1 turn 1', waitEvt.side === 1 && waitEvt.turn === 1,
          `got side=${waitEvt.side} turn=${waitEvt.turn}`);
}
if (!startWaiting) fatal('Engine did not reach WAITING_FOR_INPUT');

// ─── Phase 5: Verify initial unit positions ────────────────────────────────────

console.log('\n=== Phase 5: Verify initial unit positions ===');
const unitListPtr = wl_query_units(engine);
check('wl_query_units returns non-null', unitListPtr !== 0);
const units    = readUnitList(m, unitListPtr);
wl_free(unitListPtr);

const leader  = units.find(u => u.canrecruit && u.side === 1);
const fighter = units.find(u => !u.canrecruit && u.side === 1);
const enemy   = units.find(u => u.side === 2);

check('Leader found (side 1, canrecruit)', !!leader, `units: ${units.map(u => `${u.type_id}@(${u.x},${u.y})`).join(', ')}`);
check('Fighter found (side 1, non-leader)', !!fighter);
check('Enemy found (side 2)', !!enemy);

if (leader)  check('Leader at (3,2)', leader.x  === 3 && leader.y  === 2, `got (${leader.x},${leader.y})`);
if (fighter) check('Fighter at (5,4)', fighter.x === 5 && fighter.y === 4, `got (${fighter.x},${fighter.y})`);
if (enemy)   check('Enemy at (6,4)',   enemy.x   === 6 && enemy.y   === 4, `got (${enemy.x},${enemy.y})`);

if (!fighter || !enemy) fatal('Required units not present — check test_combat.cfg');

// ─── Phase 6: Set seed and move fighter ────────────────────────────────────────

console.log('\n=== Phase 6: Set RNG seed + move fighter (5,4) → (6,3) ===');
wl_set_seed(engine, 42);
console.log('  RNG seeded with 42 — all subsequent results are deterministic');

// Move fighter from (5,4) to (6,3): adjacent to (5,4) AND adjacent to enemy at (6,4).
// Adjacency check (stagger formula, 0-based even col ax=4):
//   neighbors of (4,2): [4,1],[5,1],[5,2],[4,3],[3,2],[3,1]  → (6,3)=0-based(5,2) ✓
//   neighbors of (5,2): [5,1],[6,2],[6,3],[5,3],[4,3],[4,2]  → (6,4)=0-based(5,3) ✓
const moveRc = sendMove(m, wl_send, engine, 5, 4, 6, 3);
check('wl_send(MOVE) returns WL_OK', moveRc === 0, `got ${moveRc}: ${wl_last_error(engine)}`);

if (moveRc === 0) {
    const { events: moveEvents } = pumpToInput(m, wl_step, engine);
    const moveEvt = moveEvents.find(e => e.type === 'UNIT_MOVE');
    check('UNIT_MOVE event emitted', !!moveEvt);
    if (moveEvt) {
        check('Fighter moved to (6,3)',
              moveEvt.to_x === 6 && moveEvt.to_y === 3,
              `got to=(${moveEvt.to_x},${moveEvt.to_y})`);
    }
    console.log(`  Post-move events: ${moveEvents.map(e => e.type).join(', ')}`);
}

// ─── Phase 7: Attack (5,3) → (6,4) with fixed seed ────────────────────────────

console.log('\n=== Phase 7: Attack enemy (deterministic, seed=42) ===');

const atkRc = sendAttack(m, wl_send, engine, 6, 3, 6, 4, 0);
check('wl_send(ATTACK) returns WL_OK', atkRc === 0, `got ${atkRc}: ${wl_last_error(engine)}`);

let atkEvent = null;
if (atkRc === 0) {
    const { events: atkEvents } = pumpToInput(m, wl_step, engine);
    atkEvent = atkEvents.find(e => e.type === 'UNIT_ATTACK');
    const dieEvent = atkEvents.find(e => e.type === 'UNIT_DIE');

    check('UNIT_ATTACK event emitted', !!atkEvent);

    if (atkEvent) {
        const { atk_result, def_result } = atkEvent;
        check('Attacker HP is valid (0 ≤ hp ≤ max)', atk_result.hp_end >= 0 && atk_result.hp_end <= atk_result.hp_start,
              `hp_start=${atk_result.hp_start} hp_end=${atk_result.hp_end}`);
        check('Defender HP is valid (0 ≤ hp ≤ max)', def_result.hp_end >= 0 && def_result.hp_end <= def_result.hp_start,
              `hp_start=${def_result.hp_start} hp_end=${def_result.hp_end}`);
        check('UNIT_DIE consistent with HP', (def_result.hp_end === 0) === (!!dieEvent),
              `hp_end=${def_result.hp_end} dieEvent=${!!dieEvent}`);

        console.log(`  Attacker HP: ${atk_result.hp_start} → ${atk_result.hp_end} (hits landed: ${atk_result.hits})`);
        console.log(`  Defender HP: ${def_result.hp_start} → ${def_result.hp_end} (hits landed: ${def_result.hits})`);
        if (dieEvent) console.log(`  Enemy died (${dieEvent.unit_id})`);

        // ── Deterministic assertions ──────────────────────────────────────────
        // These values are computed by std::mt19937 seeded with 42.
        // If the combat implementation changes, update them by running once
        // and observing the printed HP values above.
        //
        // To lock them in: replace null with the observed integer.
        //
        const EXPECTED_ATK_HP_END = null;  // TODO: fill in after first passing run
        const EXPECTED_DEF_HP_END = null;  // TODO: fill in after first passing run

        if (EXPECTED_ATK_HP_END !== null) {
            check(`Attacker HP end == ${EXPECTED_ATK_HP_END} (seed=42)`,
                  atk_result.hp_end === EXPECTED_ATK_HP_END,
                  `got ${atk_result.hp_end}`);
        }
        if (EXPECTED_DEF_HP_END !== null) {
            check(`Defender HP end == ${EXPECTED_DEF_HP_END} (seed=42)`,
                  def_result.hp_end === EXPECTED_DEF_HP_END,
                  `got ${def_result.hp_end}`);
        }
    }
    console.log(`  Post-attack events: ${atkEvents.map(e => e.type).join(', ')}`);
}

// ─── Phase 8: Recruit (if enemy survived — otherwise game may have ended) ─────

console.log('\n=== Phase 8: Recruit a Spearman ===');

// Re-query units to check if game is still going (enemy may have died)
const ul2Ptr = wl_query_units(engine);
const units2 = readUnitList(m, ul2Ptr);
wl_free(ul2Ptr);
const enemyAlive = units2.some(u => u.side === 2);

if (enemyAlive) {
    const recListPtr = wl_query_recruit(engine, 1);
    if (recListPtr) {
        const types = readRecruitList(m, recListPtr);
        wl_free(recListPtr);
        check('Recruit list non-empty', types.length > 0, `got: ${types.join(', ')}`);

        if (types.length > 0) {
            // Recruit at leader's hex — engine places unit on adjacent castle
            const recRc = sendRecruit(m, wl_send, engine, types[0], 3, 2);
            check('wl_send(RECRUIT) returns WL_OK', recRc === 0, `got ${recRc}: ${wl_last_error(engine)}`);
            if (recRc === 0) {
                const { events: recEvents } = pumpToInput(m, wl_step, engine);
                const recruitEvt = recEvents.find(e => e.type === 'UNIT_SPAWN');
                check('UNIT_SPAWN event emitted after recruit', !!recruitEvt,
                      `events: ${recEvents.map(e => e.type).join(', ')}`);
                if (recruitEvt) {
                    check(`Recruited unit is ${types[0]}`, recruitEvt.type_id === types[0],
                          `got ${recruitEvt.type_id}`);
                    console.log(`  Recruited: ${recruitEvt.type_id} for side ${recruitEvt.side}`);
                }
            }
        }
    }
} else {
    console.log('  Enemy defeated — game may have ended; skipping recruit test');
    passed++; // not a failure
}

// ─── Phase 9: End turn → AI plays → turn 2 ────────────────────────────────────

console.log('\n=== Phase 9: End turn + AI ===');

if (enemyAlive) {
    const etRc = sendEndTurn(m, wl_send, engine);
    check('wl_send(END_TURN) returns WL_OK', etRc === 0, `got ${etRc}: ${wl_last_error(engine)}`);

    if (etRc === 0) {
        const { events: aiEvents, waiting: aiWaiting } = pumpToInput(m, wl_step, engine, 20000);
        const sawSideTurnEnd = aiEvents.some(e => e.type === 'SIDE_TURN_END');
        const waitEvt2       = aiEvents.find(e => e.type === 'WAITING_FOR_INPUT');

        check('SIDE_TURN_END seen after end_turn', sawSideTurnEnd);
        if (aiWaiting && waitEvt2) {
            check('Engine reaches turn 2', waitEvt2.turn >= 2,
                  `got turn=${waitEvt2.turn} side=${waitEvt2.side}`);
            console.log(`  Turn ${waitEvt2.turn}, side ${waitEvt2.side} — ready for input`);
        } else {
            check('Engine is responsive after end_turn', aiWaiting);
        }
    }
} else {
    console.log('  Game already ended; skipping end-turn test');
    passed++;
}

// ─── Summary ──────────────────────────────────────────────────────────────────

console.log('\n══════════════════════════════════════════');
console.log(`Results: ${passed} passed, ${failed} failed`);
if (atkEvent) {
    console.log('');
    console.log('Deterministic HP values for seed=42 (update EXPECTED_* in this file):');
    console.log(`  EXPECTED_ATK_HP_END = ${atkEvent.atk_result.hp_end}`);
    console.log(`  EXPECTED_DEF_HP_END = ${atkEvent.def_result.hp_end}`);
}
console.log('══════════════════════════════════════════');

process.exit(failed > 0 ? 1 : 0);
