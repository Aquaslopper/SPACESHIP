#!/usr/bin/env node
/* ============================================================================
   tools/export-data.cjs
   ----------------------------------------------------------------------------
   Extracts every authored catalogue out of the reference edition
   (Spaceship.hta) and writes it as stable, reviewable JSON for the C++ port.

   Why a script instead of hand-transcription: the HTA is the single source of
   truth for 243 chart bodies, 30 ports, 48 systems, 21 rooms, 18 contracts and
   21 handbook entries. Copying those by hand guarantees drift. Running the real
   script in a VM and dumping its own tables guarantees they cannot disagree.

   Determinism: object keys are sorted recursively, arrays keep source order,
   and numbers use JavaScript's shortest round-trip form. A re-run on an
   unchanged HTA therefore produces byte-identical files, which is what makes
   the committed JSON reviewable in a diff.

   Usage:  node tools/export-data.cjs [--check]
           --check  compare against the committed files and exit non-zero on
                    any difference (used by CI and by the ctest data gate).
   ========================================================================== */

'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');
const crypto = require('crypto');

const ROOT = path.join(__dirname, '..');
const HTA = path.join(ROOT, 'Spaceship.hta');
const OUT_DATA = path.join(ROOT, 'assets', 'data');
const OUT_ART = path.join(ROOT, 'assets', 'art');

/* ---------------------------------------------------------------- utilities */

/* Sort keys recursively. Functions and undefined values are dropped: the
   catalogues carry behaviour in a few places (TASK_TYPES hooks), and behaviour
   is ported by hand, never generated. */
function stable(value) {
  if (value === null) return null;
  const t = typeof value;
  if (t === 'number') return Number.isFinite(value) ? value : null;
  if (t === 'string' || t === 'boolean') return value;
  if (t === 'function' || t === 'undefined') return undefined;
  if (Array.isArray(value)) {
    const out = [];
    for (let i = 0; i < value.length; i++) {
      const v = stable(value[i]);
      out.push(v === undefined ? null : v);
    }
    return out;
  }
  if (t === 'object') {
    const keys = Object.keys(value).sort();
    const out = {};
    for (const k of keys) {
      const v = stable(value[k]);
      if (v !== undefined) out[k] = v;
    }
    return out;
  }
  return undefined;
}

function json(value) {
  return JSON.stringify(stable(value), null, 2) + '\n';
}

function loadGame() {
  const hta = fs.readFileSync(HTA, 'utf8');
  const m = hta.match(/<script\s+type="text\/javascript">([\s\S]*?)<\/script>/);
  if (!m) throw new Error('game script not found in Spaceship.hta');
  const sandbox = {
    document: { getElementById: () => null, createElement: () => ({ style: {} }) },
    window: { location: { protocol: 'http:' }, localStorage: { setItem() {}, getItem() { return null; } } }
  };
  vm.createContext(sandbox);
  vm.runInContext(m[1], sandbox, { filename: 'Spaceship.hta' });
  return { game: sandbox, hta };
}

/* Derive the compact Wren deck plan. applyShipLayout() mutates ROOMS in place
   at boot for the starting ship class, so the authored large plan survives only
   in ROOM_BLUEPRINTS; the compact plan is a coordinate table applied to it. */
function wrenRooms(g) {
  return g.ROOM_BLUEPRINTS.map(r => {
    const box = g.SMALL_ROOMS[r.id];
    const out = Object.assign({}, r);
    if (box) {
      out.x = box[0]; out.y = box[1]; out.w = box[2]; out.h = box[3]; out.tag = box[4];
    }
    out.small = !!r.small;
    return out;
  });
}

/* ------------------------------------------------------------- the catalogue */

function build(g) {
  const counts = {
    ships: 2,
    roomsKestrel: g.ROOM_BLUEPRINTS.length,
    roomsWren: g.ROOM_BLUEPRINTS.length,
    fittedRooms: Object.keys(g.ROOM_FITS).length,
    crew: g.CREW.length,
    hires: g.CANDIDATES.length,
    jobs: g.JOBS.length,
    orders: g.ORDERS.length,
    commodities: g.MARKET.length,
    regions: g.GALAXY_BODIES.length,
    systems: g.SECTOR_BODIES.length,
    bodies: g.BODIES.length,
    ports: Object.keys(g.PORTS).length,
    lanes: g.SECTOR_LINKS.length,
    contracts: g.CONTRACTS.length,
    incidents: g.INCIDENT_TYPES.length,
    projects: Object.keys(g.SHIP_PROJECTS).length,
    commendations: g.COMMENDATIONS.length,
    wiki: g.WIKI.length,
    themes: Object.keys(g.THEMES).length,
    taskTypes: Object.keys(g.TASK_TYPES).length
  };

  const files = {};

  files['ship.json'] = {
    comment: 'Hull geometry, deck plans and ship classes. Stage coordinates, pixels.',
    ships: { wren: g.SHIP, kestrel: g.LARGE_SHIP },
    priceKestrel: g.LARGE_SHIP_PRICE,
    cargoByClass: { wren: [40, 60, 90], kestrel: [60, 90, 120] },
    hulls: { wren: g.SMALL_HULL, kestrel: g.LARGE_HULL },
    /* CORRIDOR itself is reassigned at boot to the compact plan, so the
       authored large corridor survives only in LARGE_CORRIDOR. */
    corridors: { wren: g.SMALL_CORRIDOR, kestrel: g.LARGE_CORRIDOR },
    rooms: { wren: wrenRooms(g), kestrel: g.ROOM_BLUEPRINTS },
    roomFits: g.ROOM_FITS,
    roomScenes: g.ROOM_SCENES
  };

  files['crew.json'] = {
    comment: 'Starting crew, hire pool, job ladder, order sheet and payroll tuning.',
    crew: g.CREW,
    candidates: g.CANDIDATES,
    jobs: g.JOBS,
    orders: g.ORDERS,
    pay: g.PAY,
    docAway: g.DOC_AWAY
  };

  files['survival.json'] = {
    comment: 'Life support, air stages, rations, drive wear and the store definitions.',
    engine: g.ENGINE,
    life: g.LIFE,
    air: g.AIR,
    airStages: g.AIR_STAGES,
    mess: g.MESS,
    messEvery: g.MESS_EVERY,
    messStarve: g.MESS_STARVE,
    messStarveEvery: g.MESS_STARVE_EVERY,
    scanCost: g.SCAN_COST,
    stats: g.STATS,
    infos: g.INFOS,
    resources: g.RESOURCES,
    cargo: g.CARGO,
    goods: g.GOODS,
    survey: g.SURVEY,
    surveyWorth: g.SURVEY_WORTH
  };

  files['chart.json'] = {
    comment: 'The star chart: three layers, 7 regions, 48 systems, 243 bodies, 30 ports.',
    map: g.MAP,
    bodies: g.BODIES,
    sectorBodies: g.SECTOR_BODIES,
    sectorLinks: g.SECTOR_LINKS,
    galaxyBodies: g.GALAXY_BODIES,
    galaxyLinks: g.GALAXY_LINKS,
    layerOrder: g.LAYER_ORDER,
    ports: g.PORTS,
    market: g.MARKET,
    portPrices: g.PORT_PRICES,
    portHints: g.PORT_HINTS,
    orbitalWorlds: g.ORBITAL_WORLDS,
    orbitalPlanetNames: g.ORBITAL_PLANET_NAMES,
    orbitalMoonNames: g.ORBITAL_MOON_NAMES
  };

  files['progression.json'] = {
    comment: 'Contracts, incidents, ship projects, commendations, commissions and world history.',
    contracts: g.CONTRACTS,
    incidents: g.INCIDENT_TYPES,
    projects: g.SHIP_PROJECTS,
    commendations: g.COMMENDATIONS,
    worldHistory: g.WORLD_HISTORY
  };

  files['content.json'] = {
    comment: 'Handbook, palettes, layout and pacing constants.',
    wiki: g.WIKI,
    themes: g.THEMES,
    stage: g.STAGE,
    feed: g.FEED,
    fx: g.FX,
    ind: g.IND,
    resize: { debounceMs: 90 },
    cooldownRules: g.ACTION_COOLDOWN_RULES,
    /* TASK_TYPES carries functions; only the identity and flags travel. */
    taskTypes: Object.keys(g.TASK_TYPES).map(k => ({
      type: k,
      autoClear: !!g.TASK_TYPES[k].autoClear,
      quiet: !!g.TASK_TYPES[k].quiet,
      hasOnDone: typeof g.TASK_TYPES[k].onDone === 'function',
      hasOnAcknowledge: typeof g.TASK_TYPES[k].onAcknowledge === 'function'
    })),
    defaults: {
      options: g.newGameOptions(),
      chronicle: g.newChronicle(),
      commissions: g.newCommissions(),
      sim: g.newSimState()
    }
  };

  return { files, counts };
}

/* ------------------------------------------------------------------ the art */

function extractTitleArt(hta, check) {
  const m = hta.match(/data:image\/jpeg;base64,([A-Za-z0-9+/=\s]+)"/);
  if (!m) throw new Error('embedded JPEG not found in the stylesheet');
  const jpeg = Buffer.from(m[1].replace(/\s+/g, ''), 'base64');
  if (jpeg[0] !== 0xff || jpeg[1] !== 0xd8 || jpeg[jpeg.length - 2] !== 0xff || jpeg[jpeg.length - 1] !== 0xd9) {
    throw new Error('embedded blob is not a complete JPEG');
  }
  const target = path.join(OUT_ART, 'title.jpg');
  if (check) {
    const old = fs.existsSync(target) ? fs.readFileSync(target) : Buffer.alloc(0);
    return { target, changed: !old.equals(jpeg), bytes: jpeg.length };
  }
  fs.mkdirSync(OUT_ART, { recursive: true });
  fs.writeFileSync(target, jpeg);
  return { target, changed: true, bytes: jpeg.length };
}

/* -------------------------------------------------------------------- main */

function main() {
  const check = process.argv.indexOf('--check') >= 0;
  const { game, hta } = loadGame();
  const { files, counts } = build(game);

  files['manifest.json'] = {
    comment: 'Generated by tools/export-data.cjs from Spaceship.hta. Do not edit by hand.',
    generator: 'tools/export-data.cjs',
    format: 'spaceship-catalog-1',
    source: {
      file: 'Spaceship.hta',
      bytes: Buffer.byteLength(hta, 'utf8'),
      sha256: crypto.createHash('sha256').update(hta, 'utf8').digest('hex')
    },
    counts
  };

  fs.mkdirSync(OUT_DATA, { recursive: true });

  let changed = 0;
  const names = Object.keys(files).sort();
  for (const name of names) {
    const text = json(files[name]);
    const target = path.join(OUT_DATA, name);
    const old = fs.existsSync(target) ? fs.readFileSync(target, 'utf8') : null;
    if (old !== text) {
      changed++;
      if (!check) fs.writeFileSync(target, text);
      else console.error('OUT OF DATE: assets/data/' + name);
    }
  }

  const art = extractTitleArt(hta, check);
  if (art.changed && check) { console.error('OUT OF DATE: assets/art/title.jpg'); changed++; }

  console.log('exported ' + names.length + ' catalogue files to assets/data/');
  console.log('counts: ' + Object.keys(counts).map(k => k + '=' + counts[k]).join(' '));
  console.log('title art: ' + art.bytes + ' bytes -> assets/art/title.jpg');

  if (check && changed) { console.error(changed + ' file(s) differ from the committed catalogue'); process.exit(1); }
  console.log(check ? 'catalogue is up to date' : changed + ' file(s) written or updated');
}

main();
