#!/usr/bin/env node
/**
 * Micro-benchmark for the terrain/animation query path.
 *
 * Runs the engine headlessly in Node against the real WML data, so it measures
 * C++ cost without a browser, a dev server, or a renderer in the way. Much
 * faster to iterate on than a board screenshot.
 *
 * It deliberately separates the first queryTerrainAt call from the rest: the
 * first one lazily constructs the terrain_builder for the whole map, so lumping
 * it in with the others hides where the time actually goes.
 *
 * Usage (from the repo root, after building build-wasm/):
 *   node wesnothlite/bench_terrain.mjs
 *   node wesnothlite/bench_terrain.mjs --campaign Two_Brothers --difficulty EASY
 */

import { mkdirSync } from 'fs'
import { dirname, resolve } from 'path'
import { fileURLToPath, pathToFileURL } from 'url'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const BUILD = resolve(REPO, 'build-wasm')
const USERDATA = '/tmp/wl_userdata'

function arg(name, dflt) {
  const i = process.argv.indexOf(`--${name}`)
  return i === -1 ? dflt : process.argv[i + 1]
}
const CAMPAIGN = arg('campaign', null)
const DIFFICULTY = arg('difficulty', 'EASY')

const stats = xs => {
  const s = [...xs].sort((a, b) => a - b)
  const sum = s.reduce((a, b) => a + b, 0)
  const pct = p => s[Math.min(s.length - 1, Math.floor(s.length * p))]
  return {
    n: s.length, total: sum, mean: sum / s.length,
    p50: pct(0.5), p90: pct(0.9), p99: pct(0.99), max: s[s.length - 1],
  }
}
const ms = n => `${n.toFixed(2)} ms`

mkdirSync(USERDATA, { recursive: true })

const factory = (await import(pathToFileURL(resolve(BUILD, 'wesnothlite.js')).href)).default

const Module = await factory({
  noInitialRun: true,
  locateFile: f => resolve(BUILD, f),
  print: () => {},
  printErr: () => {},
})

// Mount the repo so the engine can read data/ directly.
const NODEFS = Module.FS.filesystems.NODEFS
Module.FS.mkdir('/game')
Module.FS.mount(NODEFS, { root: REPO }, '/game')
Module.FS.mkdir('/userdata')
Module.FS.mount(NODEFS, { root: USERDATA }, '/userdata')

console.log('Initialising engine…')
let t0 = performance.now()
const eng = new Module.WesnothEngine('/game/data', '/userdata', '')
console.log(`  engine init: ${ms(performance.now() - t0)}`)
if (!eng.initialized) {
  console.error('engine failed to initialise:', eng.lastError())
  process.exit(1)
}

const campaigns = eng.listCampaigns()
const list = []
for (let i = 0; i < campaigns.length; i++) list.push(campaigns[i])
const pick = CAMPAIGN
  ? list.find(c => c.id === CAMPAIGN)
  : list[0]
if (!pick) { console.error(`campaign not found: ${CAMPAIGN}`); process.exit(1) }
console.log(`Starting campaign: ${pick.id} (${DIFFICULTY})`)

t0 = performance.now()
if (eng.startCampaign(pick.id, DIFFICULTY) !== 0) {
  console.error('startCampaign failed:', eng.lastError()); process.exit(1)
}
console.log(`  startCampaign: ${ms(performance.now() - t0)}`)

// Pump events until the engine wants input — that's when a map exists.
t0 = performance.now()
for (let i = 0; i < 20_000; i++) {
  const ev = eng.step()
  if (!ev) break
  if (ev.type === 'waiting_for_input') break
}
console.log(`  event pump:    ${ms(performance.now() - t0)}`)

const map = eng.queryMap()
if (!map) { console.error('no map'); process.exit(1) }
const hexes = map.hexes
console.log(`Map: ${hexes.length} hexes\n`)

// ── First call: constructs terrain_builder for the whole map ──────────────────
const h0 = hexes[0]
t0 = performance.now()
eng.queryTerrainAt(h0.loc.x, h0.loc.y)
const firstMs = performance.now() - t0

// ── Remaining hexes ───────────────────────────────────────────────────────────
const times = []
let layerCount = 0
for (let i = 1; i < hexes.length; i++) {
  const h = hexes[i]
  const t = performance.now()
  const r = eng.queryTerrainAt(h.loc.x, h.loc.y)
  times.push(performance.now() - t)
  if (r) layerCount += (r.background?.length ?? 0) + (r.foreground?.length ?? 0)
}

// ── Repeat pass: same hexes again, all caches warm ─────────────────────────────
const times2 = []
for (let i = 1; i < hexes.length; i++) {
  const h = hexes[i]
  const t = performance.now()
  eng.queryTerrainAt(h.loc.x, h.loc.y)
  times2.push(performance.now() - t)
}

const s1 = stats(times)
const s2 = stats(times2)

console.log('queryTerrainAt')
console.log(`  first call (builds terrain_builder): ${ms(firstMs)}`)
console.log(`  cold pass  n=${s1.n}  total=${ms(s1.total)}  mean=${ms(s1.mean)}  p50=${ms(s1.p50)}  p90=${ms(s1.p90)}  p99=${ms(s1.p99)}  max=${ms(s1.max)}`)
console.log(`  warm pass  n=${s2.n}  total=${ms(s2.total)}  mean=${ms(s2.mean)}  p50=${ms(s2.p50)}  p90=${ms(s2.p90)}  p99=${ms(s2.p99)}  max=${ms(s2.max)}`)
console.log(`  layers returned: ${layerCount} (${(layerCount / s1.n).toFixed(1)} per hex)`)
console.log(`\n  => whole-map cost as the frontend does it: ${ms(firstMs + s1.total)}`)

eng.delete?.()
