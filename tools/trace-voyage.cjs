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

  out.map = { layer: g.MAP.layer, here: g.MAP.here, sector: g.MAP.sector, zoom: g.MAP.zoom };
  out.navTarget = S.navTarget || null;
  out.docked = !!S.docked;
  out.shipClass = S.shipClass || '';
  out.scanCost = g.SCAN_COST;
  out.cargo = { capacity: g.CARGO.capacity, used: g.cargoUsed(), reserved: g.cargoReserved() };

  /* Phase 6: the settings popup is free, but its numbers are still compared. */
  const o = S.options || {};
  out.options = {
    largeText: !!o.largeText, confirmRisk: !!o.confirmRisk,
    hideCrewLabels: !!o.hideCrewLabels, theme: o.theme || 'classic',
    soundEffects: !!o.soundEffects, music: !!o.music,
    audioLevel: typeof o.audioLevel === 'number' ? o.audioLevel : 0.4
  };
  out.reducedMotion = !!S.reducedMotion;

  out.contracts = {};
  for (const key of Object.keys(S.contracts || {})) { out.contracts[key] = S.contracts[key]; }

  out.scanned = Object.keys(S.scanned || {}).filter(k => S.scanned[k]).sort();

  out.goods = g.GOODS.map(lot => ({
    id: lot.id, name: lot.name, kind: lot.kind, vol: lot.vol, value: lot.value,
    contractId: lot.contractId || ''
  }));

  out.survey = g.SURVEY.map(record => ({
    id: record.id, name: record.name, value: record.value, state: record.state
  }));

  out.passengers = (S.passengers || []).map(p => p.id);
  out.upgrades = Object.keys(S.upgrades || {}).filter(k => S.upgrades[k]).sort();

  out.commissions = {
    serial: S.commissions.serial,
    completed: S.commissions.completed,
    active: S.commissions.active ? {
      kind: S.commissions.active.kind,
      origin: S.commissions.active.origin || '',
      target: S.commissions.active.target || '',
      progress: S.commissions.active.progress,
      reward: S.commissions.active.reward
    } : null
  };

  out.infos = {};
  for (const info of g.INFOS) { out.infos[info.id] = info.value; }

  out.rooms = g.ROOMS.map(room => ({
    id: room.id, x: room.x, y: room.y, w: room.w, h: room.h, tag: room.tag
  }));

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
    rate: t.rate || 0,
    vol: t.vol || 0,
    payload: (typeof t.payload === 'string') ? t.payload : ''
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
  if (command === 'maint') {
    /* startMaint(e, 'maint', 'the commander') without the paint calls. */
    if (g.STATE.engineCondition >= 100) {
      g.logLine('Maintenance refused - the drive is already at full condition.');
      return true;
    }
    if (g.maintRunning()) { return true; }
    const cost = g.maintCost();
    g.MAINT_CLAIM = true;
    try { g.performAction('Engine maintenance begun by the commander'); }
    finally { g.MAINT_CLAIM = false; }
    if (g.STATE.gameOver) { return true; }
    const t = g.addOngoing('maint', 'Maintenance - the commander', cost, 'maint', 'engine');
    if (t) { t.by = 'the commander'; }
    return true;
  }
  if (command === 'cook') {
    /* startCook(e, 'cook', 'the commander') without the paint calls. */
    if (g.MESS.rack >= g.MESS.capacity) {
      g.logLine('Cooking refused - the rack is already full.');
      return true;
    }
    if (g.findOngoing('cook') || g.findOngoing('project-food') || g.PROJECT_CLAIM === 'food') { return true; }
    const rate = 1;
    const cost = g.crewCost(Math.ceil((g.MESS.capacity - g.MESS.rack) / rate));
    g.COOK_CLAIM = true;
    try { g.performAction('Cooking begun by the commander'); }
    finally { g.COOK_CLAIM = false; }
    if (g.STATE.gameOver) { return true; }
    const t = g.addOngoing('cook', 'Cooking - the commander', cost, 'cook', 'mess');
    if (t) { t.by = 'the commander'; t.rate = rate; }
    return true;
  }
  if (command === 'purse' && parts.length >= 2) {
    g.STATE.shiplets = parseFloat(parts[1]);
    return true;
  }
  if (command === 'target' && parts.length >= 2) {
    /* Selection is free. */
    if (g.findBody(parts[1])) { g.STATE.navTarget = parts[1]; g.LAYERS[g.MAP.layer].sel = parts[1]; }
    return true;
  }
  if (command === 'travel') {
    /* startTravel() without the DOM work at the end of it. */
    const b = g.STATE.navTarget ? g.findBody(g.STATE.navTarget) : null;
    if (!b || !g.inRange(b) || g.atTarget(b)) { return true; }
    if (g.findOngoing('travel')) { return true; }
    if (g.STATE.docked || g.STATE.engineCondition <= 0 || g.statValue('energy').value <= 0) { return true; }
    const m = g.navMetrics(b);
    if (g.statValue('fuel').value < m.fuel) { return true; }
    g.statValue('fuel').value -= m.fuel;
    g.performAction('Course locked - ' + b.name);
    if (g.STATE.gameOver) { return true; }
    g.addOngoing('travel', 'Under way to ' + b.name, m.dist, 'travel', b.id);
    return true;
  }
  if (command === 'scan') {
    const b = g.STATE.navTarget ? g.findBody(g.STATE.navTarget) : null;
    if (!b || !g.inRange(b) || b.type === 'system') { return true; }
    if (g.findOngoing('scan') || g.statValue('energy').value <= 0) { return true; }
    const cost = g.simScanCost();
    g.STATE.sim.sensor = 0;
    g.performAction('Deep scan started - ' + b.name);
    if (g.STATE.gameOver) { return true; }
    g.addOngoing('scan', 'Deep scan - ' + b.name, cost, 'scan', b.id);
    return true;
  }
  if (command === 'watch') {
    if (g.STATE.gameOver) { return true; }
    g.performAction('Stood watch');
    return true;
  }
  if (command === 'ack' && parts.length >= 2) {
    const t = g.findOngoing(parts[1]);
    if (!t || t.state !== 'done') { return true; }
    const hook = g.taskHook(t, 'onAcknowledge');
    if (hook) { hook(t); }
    g.clearOngoing(parts[1]);
    g.logLine('Process acknowledged and cleared.');
    return true;
  }
  if (command === 'dock') {
    const port = g.PORTS[g.MAP.here];
    if (!port || g.STATE.docked || g.STATE.gameOver) { return true; }
    g.STATE.docked = true;
    g.contractProgress('visit', g.MAP.here);
    g.findById(g.INFOS, 'location').value = port.name + ' - docked';
    g.performAction('Docked at ' + port.name);
    return true;
  }
  if (command === 'undock') {
    const port = g.PORTS[g.MAP.here];
    if (!port || !g.STATE.docked || g.STATE.gameOver) { return true; }
    g.STATE.docked = false;
    g.STATE.activeTab = 'home';
    g.findById(g.INFOS, 'location').value = port.name + ' - undocked';
    g.performAction('Undocked from ' + port.name);
    return true;
  }
  if (command === 'jobassign' && parts.length >= 3) {
    g.stageJob(parts[1], parts[2]);
    return true;
  }
  if (command === 'stage' && parts.length >= 3) {
    g.toggleOrder(parts[1], parts[2]);
    return true;
  }
  if (command === 'orders') {
    /* commitOrders() without the DOM work: the fee, the one action, the queue. */
    const n = g.ordersCost();
    if (n <= 0 || g.findOngoing('crew-assign')) { return true; }
    let busy = false;
    for (const id of Object.keys(g.STATE.pendingOrders)) {
      const c = g.findById(g.CREW, id);
      if (c && g.crewBusy(c)) { busy = true; }
    }
    if (busy) { return true; }
    const fee = g.ordersFee();
    if (fee > g.STATE.shiplets) { return true; }
    const sheet = g.STATE.pendingOrders;
    g.STATE.pendingOrders = {};
    g.STATE.shiplets -= fee;
    for (const id of Object.keys(sheet)) { g.CREW_CLAIMS[id] = true; }
    try { g.performAction('Crew assignments issued'); } finally {
      for (const id of Object.keys(sheet)) { delete g.CREW_CLAIMS[id]; }
    }
    if (g.STATE.gameOver) { return true; }
    if (fee > 0) { g.logLine('Training fees paid - ' + fee + ' sh. Purse ' + g.STATE.shiplets + ' sh.'); }
    g.queueOrders(sheet, n);
    g.logLine('Crew assignments updated. ' + n + ' actions of work.');
    return true;
  }
  if (command === 'talk' && parts.length >= 2) {
    const c = g.findById(g.CREW, parts[1]);
    if (c && !g.atDoc(c)) {
      c.said = g.crewNeed(c);
      g.logLine(c.name + ': "' + c.said + '"');
    }
    return true;
  }
  if (command === 'layer' && parts.length >= 2) {
    g.setLayer(parts[1]);
    return true;
  }
  if (command === 'sector' && parts.length >= 2) {
    g.viewSector(parts[1]);
    return true;
  }
  if (command === 'buy' && parts.length >= 2) {
    /* buildStationScreen()'s market rows, minus the markup. The screen only
       exists at a station, so every row is implicitly gated on being docked. */
    const id = parts[1];
    const S = g.STATE;
    if (!S.docked || !g.PORTS[g.MAP.here] || S.gameOver) { return true; }
    const change = (fn, message) => { if (S.gameOver) { return true; } fn(); g.performAction(message); return true; };
    if (id === 'fuel-cell' && S.shiplets >= 45) {
      return change(() => { S.shiplets -= 45; S.fuelCells++; }, 'Fuel cell purchased');
    }
    if (id === 'repair-kit' && S.shiplets >= 70) {
      return change(() => { S.shiplets -= 70; S.repairKits++; }, 'Repair kit purchased');
    }
    if (id === 'medkit' && S.shiplets >= 50) {
      return change(() => { S.shiplets -= 50; S.medkits++; }, 'Medkit purchased');
    }
    if (id === 'canister' && S.shiplets >= 25 && g.LIFE.canisters < g.LIFE.capacity && !g.findOngoing('project-oxygen')) {
      return change(() => { S.shiplets -= 25; g.LIFE.canisters++; }, 'O2 canister purchased');
    }
    if (id === 'rations' && S.shiplets >= 20 && g.MESS.rack <= g.MESS.capacity - 6 && !g.findOngoing('project-food')) {
      return change(() => { S.shiplets -= 20; g.MESS.rack += 6; }, 'Rations purchased');
    }
    const good = g.findById(g.MARKET, id);
    if (!good) { return true; }
    const price = Math.ceil(g.lotPrice(good) * 1.2);
    if (S.shiplets < price || g.cargoUsed() + g.cargoReserved() + good.vol > g.CARGO.capacity) { return true; }
    return change(() => {
      S.shiplets -= price; S.lotSerial++;
      g.GOODS.push({ id: 'lot-' + S.lotSerial, name: good.name, vol: good.vol,
                     value: good.value, kind: good.kind, risk: good.risk });
    }, 'Bought ' + good.name);
  }
  if (command === 'sellgood' && parts.length >= 2) {
    const S = g.STATE;
    if (!S.docked || !g.PORTS[g.MAP.here] || S.gameOver) { return true; }
    for (let i = 0; i < g.GOODS.length; i++) {
      const lot = g.GOODS[i];
      if (lot.id !== parts[1] ) { continue; }
      if (lot.contractId) { return true; }
      const price = g.lotPrice(lot);
      if (S.gameOver) { return true; }
      S.shiplets += price; g.GOODS.splice(i, 1);
      g.performAction('Sold ' + lot.name);
      return true;
    }
    return true;
  }
  if (command === 'sellsurvey' && parts.length >= 2) {
    const S = g.STATE;
    if (!S.docked || !g.PORTS[g.MAP.here] || S.gameOver) { return true; }
    for (let i = 0; i < g.SURVEY.length; i++) {
      if (g.SURVEY[i].id !== parts[1]) { continue; }
      const record = g.SURVEY[i];
      if (g.findOngoing('pkg-' + record.id)) { return true; }
      if (S.gameOver) { return true; }
      S.shiplets += record.value; g.SURVEY.splice(i, 1);
      g.performAction('Sold survey - ' + record.name);
      return true;
    }
    return true;
  }
  if (command === 'jettison' && parts.length >= 2) {
    const S = g.STATE;
    for (let i = 0; i < g.GOODS.length; i++) {
      const lot = g.GOODS[i];
      if (lot.id !== parts[1]) { continue; }
      g.GOODS.splice(i, 1);
      if (lot.contractId) { S.contracts[lot.contractId] = 'failed'; }
      g.performAction('Jettisoned ' + lot.name);
      return true;
    }
    return true;
  }
  if (command === 'package' && parts.length >= 2) {
    const record = g.findById(g.SURVEY, parts[1]);
    if (!record || g.findOngoing('pkg-' + record.id) || record.state === 'packaged') { return true; }
    g.performAction('Packaging survey - ' + record.name);
    if (!g.STATE.gameOver) { g.addOngoing('pkg-' + record.id, 'Packaging ' + record.name, 2, 'surveyPackage', record.id); }
    return true;
  }
  if (command === 'salvage' && parts.length >= 2) {
    const bay = parseInt(parts[1], 10);
    const id = 'salvage-' + bay;
    const trip = g.findOngoing('travel');
    if (!trip || trip.state !== 'running' || g.findOngoing(id)) { return true; }
    if (g.statValue('energy').value <= 4) { return true; }
    if (g.cargoUsed() + g.cargoReserved() + 3 > g.CARGO.capacity) { return true; }
    g.clampStat('energy', -4);
    g.performAction('Salvage recovery started');
    if (!g.STATE.gameOver) { const task = g.addOngoing(id, 'Recovering salvage - bay ' + (bay + 1), 2, 'salvage', null); if (task) { task.vol = 3; } }
    return true;
  }
  if (command === 'contract' && parts.length >= 3) {
    const S = g.STATE;
    if (!S.docked || !g.PORTS[g.MAP.here] || S.gameOver) { return true; }
    const q = g.findById(g.CONTRACTS, parts[2]);
    if (!q) { return true; }
    if (parts[1] === 'accept') {
      const status = S.contracts[q.id] || 'available';
      if (status !== 'available' && status !== 'failed') { return true; }
      if (q.origin && q.origin !== g.MAP.here) { return true; }
      if (q.vol && g.cargoUsed() + g.cargoReserved() + q.vol > g.CARGO.capacity) { return true; }
      if (q.goal === 'passenger' && g.CREW.length + S.passengers.length >= parseInt(g.SHIP.berths, 10)) { return true; }
      if (S.gameOver) { return true; }
      S.contracts[q.id] = 'active';
      if (q.goal === 'freight') {
        g.GOODS.push({ id: 'contract-' + q.id, name: q.cargo || 'Medical consignment', vol: q.vol,
                       value: 0, kind: 'Contract freight', risk: false, contractId: q.id });
      }
      if (q.goal === 'passenger') { S.passengers.push({ id: q.id, name: 'Station passenger' }); }
      if (q.recognizePrior && ((q.goal === 'visit' && (q.target === g.MAP.here || q.target === g.currentSystem().id)) ||
          (q.goal === 'scan' && S.scanned[q.target]))) {
        S.contracts[q.id] = 'ready';
        g.logLine('Contract ready to collect: ' + q.title + '.');
      }
      g.performAction('Accepted contract - ' + q.title);
      return true;
    }
    if (parts[1] === 'collect') {
      if (S.contracts[q.id] !== 'ready' || S.gameOver) { return true; }
      S.contracts[q.id] = 'complete';
      S.shiplets += q.reward;
      g.performAction('Contract paid - ' + q.title);
      return true;
    }
    return true;
  }
  if (command === 'commission' && parts.length >= 2) {
    const S = g.STATE;
    if (parts[1] === 'accept' && parts.length >= 3) {
      const offer = g.commissionOffer(parts[2]);
      if (!offer || S.gameOver || S.commissions.active) { return true; }
      S.commissions.active = offer;
      g.performAction('Commission accepted');
      return true;
    }
    if (parts[1] === 'collect') {
      const c = S.commissions.active;
      if (!c || !S.docked || !g.PORTS[g.MAP.here] || c.progress < g.commissionGoal(c.kind) || S.gameOver) { return true; }
      S.shiplets += c.reward;
      S.commissions.completed++;
      S.commissions.serial++;
      S.commissions.active = null;
      g.performAction('Commission paid');
      return true;
    }
    if (parts[1] === 'abandon') {
      if (!S.commissions.active || !S.docked || !g.PORTS[g.MAP.here] || S.gameOver) { return true; }
      S.commissions.serial++;
      S.commissions.active = null;
      g.performAction('Commission abandoned');
      return true;
    }
    return true;
  }
  if (command === 'hire' && parts.length >= 2) {
    const S = g.STATE;
    const candidate = g.findById(g.CANDIDATES, parts[1]);
    if (!candidate || S.gameOver || !S.docked || !g.PORTS[g.MAP.here]) { return true; }
    if (candidate.port && candidate.port !== g.MAP.here) { return true; }
    if (S.hired[candidate.id]) { return true; }
    if (g.CREW.length + S.passengers.length >= parseInt(g.SHIP.berths, 10)) { return true; }
    if (S.shiplets < candidate.fee) { return true; }
    const port = g.PORTS[g.MAP.here];
    S.shiplets -= candidate.fee;
    S.hired[candidate.id] = true;
    const member = JSON.parse(JSON.stringify(candidate));
    delete member.fee; delete member.port;
    g.CREW.push(member);
    g.performAction(member.name + ' joined the crew');
    g.simRemember('Arrival', member.name + ' joined the crew at ' + port.name + '.', member.id);
    return true;
  }
  if (command === 'upgrade' && parts.length >= 2) {
    const S = g.STATE, id = parts[1], cls = S.shipClass;
    if (S.gameOver || !S.docked || !g.PORTS[g.MAP.here]) { return true; }
    if (id === 'bay' && !S.upgrades.bay && S.shiplets >= 500) {
      const cap = g.shipCargoCapacity(cls, 1);
      S.shiplets -= 500; S.upgrades.bay = true; g.CARGO.capacity = Math.max(g.CARGO.capacity, cap);
      g.performAction('Cargo hold expanded');
      return true;
    }
    if (id === 'bay2' && S.upgrades.bay && !S.upgrades.bay2 && S.shiplets >= 900) {
      const cap = g.shipCargoCapacity(cls, 2);
      S.shiplets -= 900; S.upgrades.bay2 = true; g.CARGO.capacity = Math.max(g.CARGO.capacity, cap);
      g.performAction('Cargo hold enlarged again');
      return true;
    }
    if (id === 'drive' && !S.upgrades.drive && S.shiplets >= 900) {
      S.shiplets -= 900; S.upgrades.drive = true; g.performAction('Efficient drive fitted'); return true;
    }
    if (id === 'rack' && !S.upgrades.rack && S.shiplets >= 400) {
      S.shiplets -= 400; S.upgrades.rack = true; g.LIFE.capacity = 15; g.performAction('O2 rack expanded'); return true;
    }
    if (id === 'scanner' && !S.upgrades.scanner && S.shiplets >= 600) {
      S.shiplets -= 600; S.upgrades.scanner = true; g.SCAN_COST = 3; g.performAction('Sensors upgraded'); return true;
    }
    if (id === 'hull' && g.statValue('hull').value < 100 && S.shiplets >= 120) {
      S.shiplets -= 120; g.clampStat('hull', 50); g.performAction('Station repaired the hull'); return true;
    }
    return true;
  }
  if (command === 'shipyard') {
    const S = g.STATE;
    if (S.gameOver || !S.docked || !g.PORTS[g.MAP.here] || S.shipClass !== 'wren' ||
        S.shiplets < g.LARGE_SHIP_PRICE) { return true; }
    S.shiplets -= g.LARGE_SHIP_PRICE;
    S.shipClass = 'kestrel';
    g.SHIP = JSON.parse(JSON.stringify(g.LARGE_SHIP));
    g.CARGO.capacity = Math.max(g.CARGO.capacity, g.shipCargoCapacity('kestrel', g.cargoRefitLevel()));
    g.applyShipLayout();
    g.performAction('Commissioned SC Tessellate');
    g.simRemember('Work', 'The crew transferred from SC Wren to SC Tessellate at ' + g.PORTS[g.MAP.here].name + '.');
    return true;
  }
  if (command === 'project' && parts.length >= 2) {
    const id = parts[1], p = g.SHIP_PROJECTS[id], S = g.STATE;
    if (!p || S.gameOver || g.projectBlock(id)) { return true; }
    if (p.lot) { const lot = g.projectLot(p.lot); g.GOODS.splice(lot, 1); }
    let payload = null;
    if (id === 'dream') {
      const pending = g.simUnprocessed(), first = pending[0], second = pending[1];
      payload = [g.dreamPart(first), g.dreamPart(second)];
      first.processed = true; second.processed = true;
    }
    if (id === 'sensors' || id === 'decode') { S.sim.insight--; }
    if (id === 'decode') { S.sim.signal = 'decoding'; }
    if (id === 'observe') { payload = g.MAP.here; S.sim.observed[payload] = true; }
    g.clampStat('energy', -p.energy);
    g.PROJECT_CLAIM = id;
    try { g.performAction(p.label + ' started'); } finally { g.PROJECT_CLAIM = ''; }
    if (S.gameOver) { return true; }
    const task = g.addOngoing('project-' + id, p.label, p.work, 'shipProject', id);
    if (task && payload !== null) { task.payload = payload; }
    return true;
  }
  if (command === 'dream' && parts.length >= 3) {
    /* startProject(e, "dream", [a, b]) without the DOM work. */
    const S = g.STATE, one = parseInt(parts[1], 10), two = parseInt(parts[2], 10);
    const pending = g.simUnprocessed();
    const first = pending.find(m => m.id === one), second = pending.find(m => m.id === two);
    if (!first || !second || first === second || g.projectBlock('dream')) { return true; }
    const p = g.SHIP_PROJECTS.dream;
    first.processed = true; second.processed = true;
    g.clampStat('energy', -p.energy);
    g.PROJECT_CLAIM = 'dream';
    try { g.performAction(p.label + ' started'); } finally { g.PROJECT_CLAIM = ''; }
    if (S.gameOver) { return true; }
    const task = g.addOngoing('project-' + 'dream', p.label, p.work, 'shipProject', 'dream');
    if (task) { task.payload = [g.dreamPart(first), g.dreamPart(second)]; }
    return true;
  }
  if (command === 'signalstate' && parts.length >= 2) {
    g.STATE.sim.signal = parts[1];
    return true;
  }
  if (command === 'broadcast') {
    /* broadcastLocal() without the repaint. */
    const sys = g.currentSystem(), history = g.WORLD_HISTORY[sys.region];
    if (g.STATE.gameOver || g.STATE.sim.broadcasts[sys.id] || g.statValue('energy').value <= 2) { return true; }
    g.clampStat('energy', -2);
    g.STATE.sim.broadcasts[sys.id] = true;
    g.performAction('Local traffic report received');
    if (!g.STATE.gameOver) { g.simRemember('World', sys.name + ': ' + history[1]); }
    return true;
  }
  if (command === 'signal' && parts.length >= 2) {
    /* finishSignal(e, choice) without the confirmation dialog. */
    const choice = parts[1], S = g.STATE;
    if (S.gameOver || S.sim.signal !== 'request' || (choice !== 'preserved' && choice !== 'sold')) { return true; }
    if (choice === 'preserved' && (g.MAP.here !== 'palimpsest-site' || S.repairKits < 1 || g.statValue('energy').value <= 5)) { return true; }
    if (choice === 'sold' && !S.docked) { return true; }
    S.sim.signal = choice;
    if (choice === 'preserved') {
      S.repairKits--; g.clampStat('energy', -5);
      if (!g.findById(g.SURVEY, 'silicon-marrow')) {
        g.SURVEY.push({ id: 'silicon-marrow', name: 'The sheltered archive', src: 'Signal', value: 360, state: 'filed' });
      }
      for (let i = 0; i < g.CREW.length; i++) {
        g.CREW[i].morale = Math.min(100, g.CREW[i].morale + 5);
        for (let j = i + 1; j < g.CREW.length; j++) { g.simChangeBond(g.CREW[i].id, g.CREW[j].id, 5); }
      }
      g.simRemember('Choice', 'The crew sheltered the signal at the Archive Hull.');
    } else {
      S.shiplets += 900;
      for (const c of g.CREW) { c.morale = Math.max(0, c.morale - 8); }
      g.simRemember('Choice', 'The signal was sold despite its request not to be copied.');
    }
    g.performAction(choice === 'preserved' ? "Silicon Marrow's archive was sheltered" : "Silicon Marrow's signal was sold");
    if (!S.gameOver) { g.checkCommendations(); }
    return true;
  }
  if (command === 'claim' && parts.length >= 2) {
    const a = g.findById(g.COMMENDATIONS, parts[1]), j = g.STATE.chronicle;
    if (!a || !j.earned[parts[1]] || j.claimed[parts[1]] || g.STATE.gameOver) { return true; }
    j.claimed[parts[1]] = true;
    const r = a.reward;
    g.STATE.shiplets += r.cash || 0; g.STATE.fuelCells += r.fuel || 0;
    g.STATE.repairKits += r.kits || 0; g.STATE.medkits += r.med || 0;
    g.logLine('Reward claimed: ' + a.title + ' - ' + g.rewardText(r) + '.');
    return true;
  }
  if (command === 'remedy' && parts.length >= 2) {
    const ev = g.findById(g.EVENTS, parts[1]);
    if (!ev) { return true; }
    const spec = g.incidentSpec(ev);
    const needsKit = typeof spec.requiresKit === 'boolean' ? spec.requiresKit : spec.kind !== 'Crew';
    if (!spec || g.STATE.gameOver || (needsKit && g.STATE.repairKits < 1)) { return true; }
    if (needsKit) { g.STATE.repairKits--; }
    g.performAction(spec.remedy);
    if (!g.STATE.gameOver && g.findById(g.EVENTS, parts[1])) { g.removeIncident(parts[1]); }
    return true;
  }
  if (command === 'assign' && parts.length >= 2) {
    const ev = g.findById(g.EVENTS, parts[1]), worker = ev && g.incidentHand(ev);
    if (!worker || g.STATE.gameOver || g.findOngoing('incident-' + parts[1])) { return true; }
    g.CREW_CLAIMS[worker.id] = true;
    try { g.performAction(worker.name + ' assigned to ' + g.incidentSpec(ev).title); }
    finally { delete g.CREW_CLAIMS[worker.id]; }
    if (!g.STATE.gameOver && g.findById(g.EVENTS, parts[1]) && g.findById(g.CREW, worker.id)) {
      const task = g.addOngoing('incident-' + parts[1], 'Resolving ' + g.incidentSpec(ev).title, 6, 'incident', parts[1]);
      if (task) { task.by = worker.name; task.who = worker.id; }
    }
    return true;
  }
  if (command === 'medical' && parts.length >= 2) {
    const c = g.findById(g.CREW, parts[1]);
    if (!c || c.health >= 100 || g.crewBusy(c) || !g.STATE.medkits || g.statValue('energy').value <= 2 || g.STATE.gameOver) { return true; }
    g.STATE.medkits--;
    g.clampStat('energy', -2);
    g.CREW_CLAIMS[c.id] = true;
    try { g.performAction('Treatment started for ' + c.name); } finally { delete g.CREW_CLAIMS[c.id]; }
    if (g.STATE.gameOver || !g.findById(g.CREW, c.id)) { return true; }
    const task = g.addOngoing('doc-' + c.id, 'Treating ' + c.name, 3, 'shipProject', 'medical');
    if (task) { task.who = c.id; c.activity = 'In the medical bay'; }
    return true;
  }
  if (command === 'social' && parts.length >= 3) {
    const one = g.findById(g.CREW, parts[1]), two = g.findById(g.CREW, parts[2]);
    const S = g.STATE;
    if (!one || !two || parts[1] === parts[2] || g.crewBusy(one) || g.crewBusy(two) ||
        g.findOngoing('project-social') || g.MESS.rack < 2 || S.gameOver) { return true; }
    g.MESS.rack -= 2;
    g.CREW_CLAIMS[one.id] = true; g.CREW_CLAIMS[two.id] = true;
    try { g.performAction(one.name + ' and ' + two.name + ' arranged a meal'); }
    finally { delete g.CREW_CLAIMS[one.id]; delete g.CREW_CLAIMS[two.id]; }
    if (S.gameOver || !g.findById(g.CREW, one.id) || !g.findById(g.CREW, two.id)) { return true; }
    const task = g.addOngoing('project-social', 'Sharing a meal', 3, 'shipProject', 'social');
    if (task) { task.who = one.id; task.other = two.id; }
    return true;
  }
  if ((command === 'savejs' || command === 'save') && parts.length >= 2) {
    fs.writeFileSync(parts[1], JSON.stringify(g.saveSnapshot(), null, 2));
    return true;
  }
  if ((command === 'loadjs' || command === 'load') && parts.length >= 2) {
    const d = JSON.parse(fs.readFileSync(parts[1], 'utf8'));
    if (!g.validSave(d)) { console.error('loadjs: invalid save'); process.exit(3); }
    g.applySnapshot(d);
    return true;
  }
  if (command === 'option' && parts.length >= 2) {
    /* The settings popup's toggle rows. Free: a preference is not a turn. */
    const key = parts[1], o = g.STATE.options;
    if (!o) { return true; }
    if (key === 'reducedMotion') { g.STATE.reducedMotion = !g.STATE.reducedMotion; return true; }
    if (key === 'music' || key === 'soundEffects' || key === 'largeText' ||
        key === 'confirmRisk' || key === 'hideCrewLabels') {
      o[key] = !o[key];
      return true;
    }
    if (key === 'theme' && parts.length >= 3) {
      if (g.THEMES[parts[2]]) { o.theme = parts[2]; }
      return true;
    }
    if (key === 'audio' && parts.length >= 3) { o.audioLevel = parseFloat(parts[2]); return true; }
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
