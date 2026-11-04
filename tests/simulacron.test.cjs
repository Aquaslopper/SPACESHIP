/* Run with: node tests/simulacron.test.cjs. No browser or npm packages required. */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const hta = fs.readFileSync(path.join(__dirname, '..', 'Spaceship.hta'), 'utf8');
const script = hta.match(/<script type="text\/javascript">([\s\S]*?)<\/script>/);
assert(script, 'game script not found');
function fresh() {
  let stored = '';
  const g = {document:{getElementById:()=>null}, window:{location:{protocol:'http:'},
    localStorage:{setItem:(k,v)=>{stored=v;}, getItem:()=>stored}}};
  vm.createContext(g); vm.runInContext(script[1], g);
  g.repaintCurrent = () => {}; g.redrawAfterLoad = () => {};
  return g;
}
const copy = x => JSON.parse(JSON.stringify(x));
const test = (name, fn) => { fn(); console.log('PASS: ' + name); };
const act = (g, n) => { for (let i = 0; i < n; i++) g.performAction('Test commitment'); };

test('all compartments are fitted and the bridge/readouts have live screens', () => {
  const g = fresh();
  assert.strictEqual(g.ROOMS.length, 21);
  assert(!g.findById(g.ROOMS, 'test'));
  for (let i = 1; i <= 13; i++) {
    const r = g.findById(g.ROOMS, 'r' + i);
    assert(r && !r.blank && !/^Room \d+$/.test(r.name), r && r.name);
    assert(g.ROOM_FITS[r.id], r.id);
  }
  assert(g.findById(g.ROOMS, 'simulacron')); assert(g.findById(g.ROOMS, 'bridge'));
  assert.strictEqual(typeof g.buildShipRoom, 'function'); assert.strictEqual(typeof g.buildInfoScreen, 'function');
});

test('legacy test-room tabs, crew, scans, and active disputes migrate together', () => {
  const g = fresh(), d = copy(g.saveSnapshot()), r = d.rooms.find(x => x.id === 'simulacron');
  delete d.state.sim; delete d.state.medkits; delete d.map.sector;
  r.id = 'test'; r.name = 'Test Room'; r.temp = true; r.tag = 'TEMP';
  const old = d.rooms.find(x => x.id === 'r1'); old.name = 'Room 1'; old.blank = true;
  d.crew[0].room = 'test'; d.state.scanned.halvan = true;
  d.state.tabs = [{kind:'room', id:'test', key:'room:test', title:'Test Room', kindLabel:'TEMP'}];
  d.state.activeTab = 'room:test'; d.state.freshTabs['room:test'] = true;
  d.events = [{id:'crew-old',type:'crew',kind:'Crew',source:'Mess hall',stage:0,left:7}];
  assert(g.validSave(d)); g.applySnapshot(d);
  assert(!g.findById(g.ROOMS, 'test')); assert.strictEqual(g.CREW[0].room, 'simulacron');
  assert.strictEqual(g.STATE.activeTab, 'room:simulacron');
  assert.strictEqual(g.STATE.tabs[0].title, 'Simulacron Core');
  assert.strictEqual(g.findById(g.ROOMS, 'r1').name, 'Medical Bay');
  assert.strictEqual(g.STATE.sim.signal, 'intercepted'); assert.strictEqual(g.STATE.medkits, 2);
  assert(g.EVENTS[0].a && g.EVENTS[0].b); assert(g.validSave(copy(g.saveSnapshot())));
});

test('memories consolidate once, persist through work, and calibrate a single scan', () => {
  const g = fresh();
  g.simRemember('World', 'A local traffic report was received.');
  const energy = g.statValue('energy').value;
  g.startProject({}, 'dream');
  assert.strictEqual(g.actionCounter, 1); assert.strictEqual(g.statValue('energy').value, energy - 3);
  assert.strictEqual(g.simUnprocessed().length, 0); assert(g.findOngoing('project-dream'));
  const saved = copy(g.saveSnapshot()); assert(g.validSave(saved)); g.applySnapshot(saved);
  act(g, 4); assert.strictEqual(g.STATE.sim.insight, 1); assert.strictEqual(g.STATE.sim.dreams.length, 1);
  assert.strictEqual(g.simUnprocessed().length, 1); assert(!g.findOngoing('project-dream'));
  g.startProject({}, 'sensors'); act(g, 3);
  assert.strictEqual(g.STATE.sim.insight, 0); assert.strictEqual(g.STATE.sim.sensor, 1);
  g.STATE.navTarget = 'halvan'; const base = g.SCAN_COST; g.startScan({});
  assert.strictEqual(g.findOngoing('scan').total, base - 2); assert.strictEqual(g.STATE.sim.sensor, 0);
  g.advanceTasks(base - 2); g.TASK_TYPES.scan.onAcknowledge(g.findOngoing('scan')); g.clearOngoing('scan');
  assert.strictEqual(g.STATE.sim.signal, 'intercepted'); assert.strictEqual(g.simScanCost(), base);
  const serial = g.STATE.sim.serial; g.revealScan('halvan'); assert.strictEqual(g.STATE.sim.serial, serial);
});

test('relationships use rations, reserve crew, remember care and affect morale', () => {
  const g = fresh(), a = g.CREW[0], b = g.CREW[1], meals = g.MESS.rack;
  g.startSocial({}, a.id, b.id);
  assert.strictEqual(g.MESS.rack, meals - 2); assert(g.crewBusy(a) && g.crewBusy(b));
  assert.strictEqual(g.crewWhere(a), 'Recreation');
  const before = g.STATE.medkits; g.startMedical({}, b.id); assert.strictEqual(g.STATE.medkits, before);
  act(g, 3); assert.strictEqual(g.simBond(a.id, b.id), 12);
  assert(g.STATE.sim.memories.some(m => m.who === a.id && m.other === b.id));
  assert(!g.crewBusy(a)); assert(!g.crewBusy(b));
  g.simChangeBond(a.id, b.id, 20); const morale = a.morale;
  g.actionCounter = 25; g.tickSim(); assert.strictEqual(a.morale, morale + 1);
  const hp = b.health; g.startMedical({}, b.id); assert.strictEqual(g.STATE.medkits, before - 1);
  assert.strictEqual(g.crewWhere(b), 'Medical Bay'); act(g, 3);
  assert.strictEqual(b.health, Math.min(100, hp + 25));
  assert(g.STATE.sim.memories.some(m => m.kind === 'Care' && m.who === b.id));
});

test('crew cannot start another job while autonomous work or a staged sheet uses them', () => {
  const g = fresh(), engineer = g.findById(g.CREW, 'jax');
  const work = g.addOngoing('maint-eng', 'Maintenance', 4, 'maint', 'engine'); work.by = engineer.name;
  assert.strictEqual(g.crewBusy(engineer), work);
  const oldMeals = g.MESS.rack; g.startSocial({}, 'jax', 'rell'); assert.strictEqual(g.MESS.rack, oldMeals);
  g.STATE.pendingOrders.jax = {rest:true}; assert(!g.ordersAvailable());
  const n = g.actionCounter; g.commitOrders({}); assert.strictEqual(g.actionCounter, n);
  g.clearOngoing('maint-eng'); assert(g.ordersAvailable());
  g.STATE.engineCondition = 30; g.commitOrders({});
  assert(g.findOngoing('cw-jax')); assert(!g.findOngoing('maint-eng'), 'claim protects the initiating action');
});

test('conflict escalation damages a real bond and mediation repairs it', () => {
  const g = fresh(); g.actionCounter = 110; g.tickIncidents();
  const ev = g.EVENTS.find(x => x.type === 'crew'); assert(ev && ev.a && ev.b);
  for (let i = 0; i < 12; i++) g.tickIncidents();
  assert.strictEqual(g.simBond(ev.a, ev.b), -8);
  assert(g.STATE.sim.memories.some(x => x.kind === 'Conflict' && x.who === ev.a));
  g.removeIncident(ev.id); assert.strictEqual(g.simBond(ev.a, ev.b), 4);
  assert(!g.findById(g.EVENTS, ev.id));
});

test('resource projects reserve inputs, finish once, and respect capacity', () => {
  const g = fresh(); g.GOODS.push({id:'recovery-a',name:'Recovered salvage',vol:3,value:30,kind:'Salvage',risk:false});
  const kits = g.STATE.repairKits, energy = g.statValue('energy').value;
  g.startProject({}, 'kit'); assert(!g.findById(g.GOODS, 'recovery-a'));
  assert.strictEqual(g.statValue('energy').value, energy - 3);
  const n = g.actionCounter; g.startProject({}, 'kit'); assert.strictEqual(g.actionCounter, n);
  act(g, 4); assert.strictEqual(g.STATE.repairKits, kits + 1);
  act(g, 4); assert.strictEqual(g.STATE.repairKits, kits + 1);
  g.GOODS.push({id:'recovery-b',name:'Recovered salvage',vol:3,value:30,kind:'Salvage',risk:false});
  g.LIFE.canisters = g.LIFE.capacity; g.startProject({}, 'oxygen');
  assert(g.findById(g.GOODS, 'recovery-b'), 'full rack must refuse input');
  g.LIFE.canisters--; g.startProject({}, 'oxygen'); act(g, 4);
  assert.strictEqual(g.LIFE.canisters, g.LIFE.capacity);
  g.MESS.rack = g.MESS.capacity; const produce = g.findById(g.GOODS, 'greens');
  g.startProject({}, 'food'); assert(g.findById(g.GOODS, produce.id));
});

test('observations are unique, recorded at the committed location and cannot be sold repeatedly', () => {
  const g = fresh(); g.startProject({}, 'observe');
  const saved = copy(g.saveSnapshot()); assert(g.validSave(saved)); g.applySnapshot(saved);
  g.MAP.here = 'halvan'; act(g, 4);
  assert(g.findById(g.SURVEY, 'obs-kessler')); assert(!g.findById(g.SURVEY, 'obs-halvan'));
  g.MAP.here = 'kessler'; const n = g.actionCounter; g.startProject({}, 'observe');
  assert.strictEqual(g.actionCounter, n); assert(g.STATE.sim.observed.kessler);
});

test('signal chain supports both consequential endings and recognizes a prior Choir scan', () => {
  for (const choice of ['preserved', 'sold']) {
    const g = fresh(); g.revealScan('echo-site'); g.revealScan('halvan');
    assert.strictEqual(g.STATE.sim.signal, 'intercepted'); g.STATE.sim.insight = 1;
    g.startProject({}, 'decode'); assert.strictEqual(g.STATE.sim.signal, 'decoding');
    const saved = copy(g.saveSnapshot()); assert(g.validSave(saved)); g.applySnapshot(saved);
    act(g, 4); assert.strictEqual(g.STATE.sim.signal, 'request');
    const cash = g.STATE.shiplets, kit = g.STATE.repairKits, morale = g.CREW[0].morale;
    if (choice === 'preserved') { g.MAP.here = 'palimpsest-site'; }
    else { g.MAP.here = 'tolme'; g.STATE.docked = true; }
    g.finishSignal({}, choice); assert.strictEqual(g.STATE.sim.signal, choice);
    if (choice === 'preserved') {
      assert.strictEqual(g.STATE.repairKits, kit - 1); assert(g.findById(g.SURVEY, 'silicon-marrow'));
      assert(g.CREW[0].morale > morale); assert.strictEqual(g.simBond('rell', 'jax'), 5);
    } else { assert.strictEqual(g.STATE.shiplets, cash + 900); assert(g.CREW[0].morale < morale); }
    const snapshot = copy(g.saveSnapshot()); assert(g.validSave(snapshot)); g.applySnapshot(snapshot);
    const action = g.actionCounter, credits = g.STATE.shiplets; g.finishSignal({}, choice);
    assert.strictEqual(g.actionCounter, action); assert.strictEqual(g.STATE.shiplets, credits);
  }
});

test('malformed simulation saves are rejected without mutating the voyage', () => {
  const g = fresh(), before = JSON.stringify(g.saveSnapshot()), d = JSON.parse(before);
  d.state.sim.memories[0].text = {not:'text'};
  g.window.localStorage.setItem('spaceship-command-save', JSON.stringify(d));
  assert(g.loadGame().includes('Could not load')); assert.strictEqual(JSON.stringify(g.saveSnapshot()), before);
  const bad = JSON.parse(before); bad.state.sim.signal = 'decoding';
  assert(!g.validSave(bad), 'a missing decode task must not strand the chain');
  bad.state.sim.signal = 'request'; bad.state.sim.bonds['rell~jax'] = -1000;
  assert(!g.validSave(bad));
});

test('the observer selects distinct memories and earns combination-specific outcomes', () => {
  const g = fresh();
  g.simRemember('Discovery', 'A new world was scanned.');
  g.simRemember('World', 'A regional report was heard.');
  let a = g.STATE.sim.memories[1], b = g.STATE.sim.memories[2];
  g.startProject({}, 'dream', [a.id, a.id]); assert.strictEqual(g.actionCounter, 0);
  g.startProject({}, 'dream', [a.id, b.id]);
  assert(!g.STATE.sim.memories[0].processed && a.processed && b.processed);
  assert(g.validSave(copy(g.saveSnapshot()))); act(g, 4); assert.strictEqual(g.STATE.sim.insight, 2);
  g.simRemember('Conflict', 'A disagreement.', 'rell', 'jax');
  g.simRemember('Connection', 'A reconciliation.', 'jax', 'rell');
  a = g.STATE.sim.memories[g.STATE.sim.memories.length - 2]; b = g.STATE.sim.memories[g.STATE.sim.memories.length - 1];
  const morale = g.CREW[0].morale; g.startProject({}, 'dream', [a.id, b.id]); act(g, 4);
  assert.strictEqual(g.simBond('rell', 'jax'), 4); assert.strictEqual(g.CREW[0].morale, morale + 2);
  assert.strictEqual(g.STATE.sim.insight, 3);
});

test('desire-driven rest waits for action time, reserves a person and yields a memory', () => {
  const g = fresh(), c = g.findById(g.CREW, 'jax');
  c.fatigue = 80; g.actionCounter = 29; g.performAction('A real commitment');
  const rest = g.findOngoing('cw-jax'); assert(rest && rest.orders.rest && g.crewBusy(c));
  assert(g.STATE.sim.memories.some(x => x.kind === 'Need' && x.who === c.id));
  act(g, 3); assert(!g.crewBusy(c)); assert(c.fatigue <= 56);
  assert(g.STATE.sim.memories.some(x => x.kind === 'Rest' && x.who === c.id));
  const h = fresh(), injured = h.findById(h.CREW, 'jax'); injured.fatigue = 80; injured.health = 50;
  h.actionCounter = 29; h.performAction('Another commitment'); assert(!h.findOngoing('cw-jax'));
  assert.strictEqual(h.needKey(injured), 'ill');
});

test('all production bays deliver their stated output and exclude competing work', () => {
  for (const id of ['fuel', 'brace', 'food', 'cooling']) {
    const g = fresh(), p = g.SHIP_PROJECTS[id];
    g.STATE.filterLife = 150; g.STATE.engineCondition = 60; g.MESS.rack = 10;
    g.statValue('hull').value = 50;
    g.GOODS.push({id:'recovery-input',name:'Recovered salvage',vol:3,value:30,kind:'Salvage',risk:false});
    const fuel = g.STATE.fuelCells, hull = g.statValue('hull').value, food = g.MESS.rack;
    g.startProject({}, id); assert(g.findOngoing('project-' + id), id + ' did not start');
    if (id === 'food') { const n = g.actionCounter; g.startCookSelf({}); assert.strictEqual(g.actionCounter, n); }
    if (id === 'cooling') { const n = g.actionCounter; g.startMaintSelf({}); assert.strictEqual(g.actionCounter, n); }
    act(g, p.work);
    if (id === 'fuel') assert.strictEqual(g.STATE.fuelCells, fuel + 1);
    if (id === 'brace') assert.strictEqual(g.statValue('hull').value, hull + 15);
    if (id === 'food') assert.strictEqual(g.MESS.rack, food + 8);
    if (id === 'cooling') assert(g.STATE.engineCondition >= 73 && g.STATE.engineCondition <= 75);
  }
});

test('loss interrupts shared work without giving a posthumous reward', () => {
  const g = fresh(); g.startSocial({}, 'rell', 'jax');
  g.findById(g.CREW, 'jax').health = 0; g.checkLoss();
  assert(!g.findOngoing('project-social')); assert(!g.findById(g.CREW, 'jax'));
  assert.strictEqual(g.simBond('rell', 'jax'), 0); assert(!g.crewBusy(g.findById(g.CREW, 'rell')));
});

test('powered work pauses on blackout and O2 reserves account for possible returns', () => {
  const g = fresh(); g.simRemember('World', 'Traffic report.'); g.startProject({}, 'dream');
  const task = g.findOngoing('project-dream'), left = task.left;
  g.clampStat('energy', -100); act(g, 3); assert.strictEqual(task.left, left);
  g.clampStat('energy', 20); act(g, 4); assert.strictEqual(g.STATE.sim.insight, 1);
  const h = fresh(); h.LIFE.canisters = h.LIFE.capacity - 1;
  h.GOODS.push({id:'recovery-o2',name:'Recovered salvage',vol:3,value:30,kind:'Salvage',risk:false});
  const install = h.addOngoing('o2s0', 'Seating', 2, 'o2slot', h.LIFE.slots[0].id);
  install.slot = 0; install.by = 'Bo Ferreira';
  assert(h.projectBlock('oxygen').includes('reserved')); h.startProject({}, 'oxygen');
  assert(h.findById(h.GOODS, 'recovery-o2'));
});

test('saved projects cannot duplicate reserved work or strand a story stage', () => {
  const g = fresh(); g.revealScan('halvan'); g.STATE.sim.insight = 1; g.startProject({}, 'decode');
  let d = copy(g.saveSnapshot()); assert(g.validSave(d));
  d.state.ongoing.push(copy(d.state.ongoing.find(x => x.target === 'decode')));
  assert(!g.validSave(d));
  d = copy(g.saveSnapshot()); d.state.sim.signal = 'request'; assert(!g.validSave(d));
  d = copy(g.saveSnapshot()); d.state.ongoing.find(x => x.target === 'decode').id = 'wrong-id'; assert(!g.validSave(d));
});

test('incident work reserves a real available crew member and releases them on resolution', () => {
  const g = fresh(); g.actionCounter = 45; g.tickIncidents();
  const ev = g.EVENTS.find(x => x.type === 'fracture'), hand = g.incidentHand(ev);
  assert(hand); g.beginIncidentWork({}, ev.id);
  const task = g.findOngoing('incident-' + ev.id); assert(task && task.by === hand.name);
  assert.strictEqual(g.crewBusy(hand), task); assert(g.validSave(copy(g.saveSnapshot())));
  g.removeIncident(ev.id); assert(!g.findOngoing('incident-' + ev.id)); assert(!g.crewBusy(hand));
});

test('missing optional old compartments are restored without requiring a contacts readout', () => {
  const g = fresh(), d = copy(g.saveSnapshot());
  delete d.state.sim; delete d.state.medkits;
  d.rooms = d.rooms.filter(r => !/^r\d+$/.test(r.id) && r.id !== 'bridge' && r.id !== 'simulacron');
  d.crew.forEach(c => { if (c.room === 'bridge') c.room = 'quarters'; });
  d.infos = d.infos.filter(x => x.id !== 'contacts');
  assert(g.validSave(d)); g.applySnapshot(d);
  assert(g.findById(g.ROOMS, 'r13') && g.findById(g.ROOMS, 'r1') && g.findById(g.ROOMS, 'bridge'));
  g.arriveAt('halvan'); assert.strictEqual(g.MAP.here, 'halvan');
  g.startSocial({}, 'rell', 'jax'); assert.strictEqual(g.crewWhere(g.CREW[0]), 'Recreation');
  assert(g.validSave(copy(g.saveSnapshot())));
});

test('a real incident assignment completes and a signal choice cannot be taken from the wrong location', () => {
  const g = fresh(); g.actionCounter = 45; g.tickIncidents();
  const ev = g.EVENTS.find(x => x.type === 'fracture'), hand = g.incidentHand(ev);
  g.beginIncidentWork({}, ev.id); assert(g.crewBusy(hand)); act(g, 6);
  assert(!g.findById(g.EVENTS, ev.id) && !g.crewBusy(hand));
  g.STATE.sim.signal = 'request'; const n = g.actionCounter, kits = g.STATE.repairKits;
  g.finishSignal({}, 'preserved'); g.finishSignal({}, 'sold'); g.finishSignal({}, 'unknown');
  assert.strictEqual(g.STATE.sim.signal, 'request'); assert.strictEqual(g.STATE.repairKits, kits); assert.strictEqual(g.actionCounter, n);
});

test('navigation training reduces fuel, not travel time, and a busy navigator cannot help', () => {
  const g = fresh(), target = g.findById(g.SECTOR_BODIES, 'sec-vance'), c = g.findById(g.CREW, 'rell');
  const baseline = g.navMetrics(target); g.applyJob(c, 'navigation'); c.jobAt = 5;
  const skilled = g.navMetrics(target); assert.strictEqual(skilled.dist, baseline.dist);
  assert.strictEqual(skilled.fuel, Math.ceil(baseline.dist * 0.8)); assert(skilled.fuel < baseline.fuel);
  const busy = g.addOngoing('cw-rell', 'Orders', 2, 'crewJob', 'rell'); busy.who = 'rell'; busy.orders = {rest:true};
  assert.strictEqual(g.navMetrics(target).fuel, baseline.fuel); g.clearOngoing('cw-rell');
  g.STATE.upgrades.drive = true; assert.strictEqual(g.navMetrics(target).fuel, Math.ceil(baseline.dist * 0.8 * 0.8));
  assert(g.jobThreshold(c).includes('20%'));
});

test('a cleaner improves crew morale on the action clock and training accelerates upkeep', () => {
  const g = fresh(), cleaner = g.findById(g.CREW, 'rell'), other = g.findById(g.CREW, 'jax');
  g.applyJob(cleaner, 'cleaner'); assert.strictEqual(g.cleanerInterval(cleaner), 40);
  const m = other.morale; g.actionCounter = 39; g.performAction('Working'); assert.strictEqual(other.morale, m + 1);
  cleaner.jobAt = 5; assert.strictEqual(g.cleanerInterval(cleaner), 20);
  const busy = g.addOngoing('cw-rell', 'Orders', 4, 'crewJob', 'rell'); busy.who = 'rell'; busy.orders = {};
  g.actionCounter = 59; const before = other.morale; g.performAction('Working'); assert.strictEqual(other.morale, before);
  assert(g.jobThreshold(cleaner).includes('20 actions'));
});

test('reduced-motion preference validates, persists and defaults for old saves', () => {
  const g = fresh(); g.STATE.reducedMotion = true;
  const d = copy(g.saveSnapshot()); assert(g.validSave(d)); g.STATE.reducedMotion = false; g.applySnapshot(d);
  assert.strictEqual(g.STATE.reducedMotion, true);
  d.state.reducedMotion = 'yes'; assert(!g.validSave(d));
  delete d.state.reducedMotion; assert(g.validSave(d)); g.applySnapshot(d); assert.strictEqual(g.STATE.reducedMotion, false);
});
