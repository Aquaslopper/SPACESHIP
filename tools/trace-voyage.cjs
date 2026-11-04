#!/usr/bin/env node
/* ============================================================================
   tools/trace-voyage.cjs — the other half of the differential harness.
   ----------------------------------------------------------------------------
   Runs a command script through the REFERENCE edition (Spaceship.hta, loaded in
   a Node VM exactly as the shipped tests do) and prints the canonical trace at
   every checkpoint. spaceship_console prints the same lines from the C++ core;
   tests/parity/differential.cjs diffs the two streams.

   The projection below is deliberately a copy of cpp/core/Trace.cpp. If one
   side gains a field, the other must gain it in the same commit — that is the
   point.

   Usage:  node tools/trace-voyage.cjs <script> [--hta <path>]
   ========================================================================== */

'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.join(__dirname, '..');

/* ---------------------------------------------------------------- loading */

function loadReference(htaPath) {
  const hta = fs.readFileSync(htaPath, 'utf8');
  const match = hta.match(/<script\s+type="text\/javascript">([\s\S]*?)<\/script>/);
  if (!match) { throw new Error('game script not found in ' + htaPath); }

  const sandbox = {
    document: { getElementById: () => null, createElement: () => ({ style: {} }) },
    window: { location: { protocol: 'http:' }, localStorage: { setItem() {}, getItem() { return null; } } }
  };
  vm.createContext(sandbox);
  vm.runInContext(match[1], sandbox, { filename: 'Spaceship.hta' });

  /* The reference calls paint*() after every change. Those are presentation
     only; with no DOM to paint into they are replaced by no-ops so the script
     exercises the model and nothing else. */
  for (const name of Object.keys(sandbox)) {
    if (/^(paint|build|render|layout|draw|update|show|hide|refresh)/.test(name) &&
        typeof sandbox[name] === 'function') {
      sandbox[name] = () => {};
    }
  }
  sandbox.repaintCurrent = () => {};
  sandbox.repaintQueue = () => {};
  sandbox.setStatus = () => {};
  sandbox.switchTab = () => {};
  sandbox.playActionSound = () => {};
  sandbox.buildSideFoot = () => {};
  sandbox.showLoss = () => {};
  sandbox.buildSidePanel = () => {};
  return sandbox;
}

/* ------------------------------------------------------------- projection */

/* Sorted-key stringify: object keys alphabetically, array order preserved.
   JSON.stringify already prints integral numbers the way JavaScript does. */
function stableStringify(value) {
  if (value === null) { return 'null'; }
  if (Array.isArray(value)) { return '[' + value.map(stableStringify).join(',') + ']'; }
  const t = typeof value;
  if (t === 'number') { return JSON.stringify(value); }
  if (t === 'boolean') { return value ? 'true' : 'false'; }
  if (t === 'string') { return JSON.stringify(value); }
  if (t === 'object') {
    const keys = Object.keys(value).sort();
    return '{' + keys.map(k => JSON.stringify(k) + ':' + stableStringify(value[k])).join(',') + '}';
  }
  return 'null';
}

function sortedTrueKeys(map) {
  const out = [];
  for (const key of Object.keys(map || {})) { if (map[key]) { out.push(key); } }
  return out.sort();
}

function trace(g) {
  const S = g.STATE;
  const out = {};

  out.actions = g.actionCounter;
  out.gameOver = S.gameOver || '';

  out.engine = { condition: S.engineCondition, wearTick: S.wearTick };

  out.filter = {
    airHold: S.airHold,
    life: S.filterLife,
    canisters: g.LIFE.canisters,
    rackWarned: !!S.rackWarned,
    rackWarnedMess: !!S.rackWarnedMess,
    slotStates: g.LIFE.slots.map(s => s.state)
  };

  out.stats = {};
  for (const stat of g.STATS) { out.stats[stat.id] = stat.value; }

  out.purse = {
    shiplets: S.shiplets,
    fuelCells: S.fuelCells,
    repairKits: S.repairKits,
    medkits: S.medkits,
    lotSerial: S.lotSerial
  };

  out.counters = { crewTick: S.crewTick, payTick: S.payTick };
  out.mess = { rack: g.MESS.rack };

  out.crew = g.CREW.map(c => ({
    id: c.id,
    name: c.name,
    health: c.health,
    morale: c.morale,
    fatigue: c.fatigue,
    hunger: c.hunger,
    hungerTick: c.hungerTick || 0,
    wallet: c.wallet || 0,
    job: c.job || '',
    skill: c.skill || '',
    jobAt: c.jobAt || 0,
    activity: c.activity || ''
  }));

  out.tasks = S.ongoing.map(t => ({
    id: t.id,
    type: t.type,
    label: t.label,
    total: t.total,
    left: t.left,
    state: t.state,
    target: t.target || '',
    by: t.by || '',
    who: t.who || '',
    slot: ('slot' in t) ? t.slot : -1,
    rate: t.rate || 0
  }));

  out.events = g.EVENTS.map(e => ({
    id: e.id,
    type: e.type,
    stage: e.stage,
    left: e.left,
    a: e.a || '',
    b: e.b || ''
  }));

  out.sim = {
    signal: S.sim.signal,
    insight: S.sim.insight,
    sensor: S.sim.sensor,
    serial: S.sim.serial,
    memories: S.sim.memories.map(m => ({
      id: m.id, kind: m.kind, text: m.text, at: m.at,
      who: m.who || '', other: m.other || '', processed: !!m.processed
    })),
    bonds: S.sim.bonds || {}
  };

  out.chronicle = {
    banks: S.chronicle.banks,
    projects: S.chronicle.projects,
    dreams: S.chronicle.dreams,
    meals: S.chronicle.meals,
    incidents: S.chronicle.incidents,
    earned: sortedTrueKeys(S.chronicle.earned)
  };

  out.cooldowns = {};
  for (const key of Object.keys(S.actionCooldowns || {})) {
    out.cooldowns[key] = S.actionCooldowns[key].remaining;
  }

  out.feed = S.feed.map(line => line.s);

  return stableStringify(out);
}

/* ---------------------------------------------------------------- commands */

function runCommand(g, line) {
  const parts = line.trim().split(/\s+/);
  const command = parts[0];

  if (command === 'action') {
    const detail = line.trim().slice('action'.length).trim() || 'Scripted action';
    g.performAction(detail);
    return true;
  }
  if (command === 'seat' && parts.length >= 2) {
    /* startInstall() without the DOM work, which is all the paint*() calls at
       the end of it. Everything that changes state is kept. */
    const i = parseInt(parts[1], 10);
    const sl = g.LIFE.slots[i];
    if (!sl || sl.state !== 'empty') { return true; }
    if (g.findOngoing('o2s' + i)) { return true; }
    if (g.LIFE.canisters <= 0) { g.logLine('No canisters left in the rack.'); return true; }
    g.LIFE.canisters--;
    g.O2_CLAIM = true;
    try { g.performAction('Canister carried to ' + sl.name); } finally { g.O2_CLAIM = false; }
    const task = g.addOngoing('o2s' + i, 'Seating - ' + sl.name, 2, 'o2slot', sl.id);
    if (task) { task.slot = i; }
    return true;
  }
  if (command === 'bank') {
    /* turnOnFilter() without the DOM work. */
    if (!g.bankReady() || g.bankRunning() || g.statValue('energy').value <= 0) { return true; }
    g.O2_CLAIM = true;
    try { g.performAction('Filter bank started'); } finally { g.O2_CLAIM = false; }
    if (g.statValue('energy').value <= 0) {
      g.logLine('The filter switch needs power.');
      return true;
    }
    g.runBank('Filters activated. Air purified. +10 Pro-Action.');
    return true;
  }
  if (command === 'purse' && parts.length >= 2) {
    g.STATE.shiplets = parseFloat(parts[1]);
    return true;
  }
  if (command === 'job' && parts.length >= 3) {
    const crew = g.findById(g.CREW, parts[1]);
    if (crew) { g.applyJob(crew, parts[2]); }
    return true;
  }
  return false;
}

function main() {
  const args = process.argv.slice(2);
  let scriptPath = null;
  let htaPath = path.join(ROOT, 'Spaceship.hta');
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--hta') { htaPath = args[++i]; } else { scriptPath = args[i]; }
  }
  if (!scriptPath) {
    console.error('usage: node tools/trace-voyage.cjs <script> [--hta <path>]');
    process.exit(2);
  }

  const g = loadReference(htaPath);
  const lines = fs.readFileSync(scriptPath, 'utf8').split(/\r?\n/);

  for (let n = 0; n < lines.length; n++) {
    const line = lines[n];
    if (!line.trim() || line.trim().startsWith('#')) { continue; }
    if (line.trim() === 'trace') {
      process.stdout.write('TRACE ' + trace(g) + '\n');
      continue;
    }
    if (!runCommand(g, line)) {
      console.error('unknown command on line ' + (n + 1) + ': ' + line);
      process.exit(2);
    }
  }
  process.stdout.write('END ' + trace(g) + '\n');
}

main();
