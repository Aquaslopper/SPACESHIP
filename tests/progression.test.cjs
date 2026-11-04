/* Run with: node tests/progression.test.cjs. No browser or npm packages required. */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const hta = fs.readFileSync(path.join(__dirname, '..', 'Spaceship.hta'), 'utf8');
const script = hta.match(/<script type="text\/javascript">([\s\S]*?)<\/script>/);
assert(script, 'game script not found');
function fresh() {
  const storage = {};
  const g = {document:{getElementById:()=>null}, window:{location:{protocol:'http:'}, confirm:()=>true,
    localStorage:{setItem:(k,v)=>{storage[k]=v;}, getItem:k=>storage[k] || null}}};
  vm.createContext(g); vm.runInContext(script[1], g);
  g.repaintCurrent = () => {}; g.redrawAfterLoad = () => {};
  g.storage = storage;
  return g;
}
const copy = x => JSON.parse(JSON.stringify(x));
const test = (name, fn) => { fn(); console.log('PASS: ' + name); };
function mockButtons(g) {
  g.make = (tag, cls, html) => {
    const el = {tag, className:cls || '', innerHTML:html || '', style:{}, children:[], events:{}, attrs:{}, title:'',
      appendChild(child) { this.children.push(child); },
      setAttribute(name, value) { this.attrs[name] = value; },
      removeAttribute(name) { delete this.attrs[name]; },
      getElementsByTagName(name) { return name === 'span' && this.costSpan ? [this.costSpan] : []; }};
    const match = (html || '').match(/<span[^>]*>([\s\S]*?)<\/span>/i);
    if (match) el.costSpan = {innerHTML:match[1]};
    return el;
  };
  g.on = (el, type, fn) => { el.events[type] = fn; };
  g.tickWorld = () => {};
  g.logLine = () => {};
  g.checkCommendations = () => {};
  g.setStatus = () => {};
  g.repaintCurrent = () => {};
  g.redrawAfterLoad = () => {};
  g.findOngoing = () => null;
  g.fireAction = (e, detail) => g.performAction(detail);
  return () => ({prevented:0, stopped:0, preventDefault() { this.prevented++; },
    stopPropagation() { this.stopped++; }, clientX:0, clientY:0});
}

test('the searchable handbook has unique entries and valid free shortcuts', () => {
  const g = fresh(), ids = new Set();
  assert(g.WIKI.length >= 20);
  for (const e of g.WIKI) {
    assert(!ids.has(e.id)); ids.add(e.id);
    assert(e.title && e.group && e.text);
    if (e.link && e.link !== 'nav') assert(g.findById(g.ROOMS, e.link), e.link);
  }
  assert(g.wikiMatches(g.findById(g.WIKI, 'air'), 'CANISTERS'));
  assert(!g.wikiMatches(g.findById(g.WIKI, 'air'), 'impossible search'));
  const n = g.actionCounter;
  assert.strictEqual(g.describeTarget('wiki', 'main').title, "Ship's Handbook");
  assert(g.describeTarget('awards', 'main').title.includes('Rewards'));
  assert.strictEqual(g.actionCounter, n);
});

test('offers at every port are deterministic, accessible and never send a visit to the current port', () => {
  const g = fresh();
  for (const port of Object.keys(g.PORTS)) {
    g.arriveAt(port);
    for (let serial = 0; serial < 12; serial++) {
      g.STATE.commissions.serial = serial;
      for (const kind of ['visit','scan','salvage']) {
        const a = copy(g.commissionOffer(kind));
        assert.deepStrictEqual(copy(g.commissionOffer(kind)), a);
        assert.strictEqual(a.origin, port); assert.strictEqual(a.progress, 0);
        assert.strictEqual(a.reward, g.commissionPrice(kind));
        if (kind === 'visit') assert(g.PORTS[a.target] && a.target !== port, port);
        if (kind === 'scan') {
          const target = g.findById(g.BODIES, a.target);
          assert(target && g.bodySystem(target) !== g.currentSystem().id, port);
        }
        g.STATE.commissions.active = a;
        assert(g.validCommissions(g.STATE.commissions));
        g.STATE.commissions.active = null;
      }
    }
    g.STATE.commissions.serial = 0;
    const firstVisit = g.commissionOffer('visit').target;
    g.STATE.commissions.serial = 1;
    assert.notStrictEqual(g.commissionOffer('visit').target, firstVisit, 'visit offers did not rotate at '+port);
  }
  g.arriveAt('tolme'); g.STATE.commissions.serial = 0;
  const visit0 = g.commissionOffer('visit').target, scan0 = g.commissionOffer('scan').target;
  g.STATE.commissions.serial = 1;
  assert.notStrictEqual(g.commissionOffer('visit').target, visit0);
  assert.notStrictEqual(g.commissionOffer('scan').target, scan0);
});

test('a visit commission requires acceptance, reaches its target and pays exactly once', () => {
  const g = fresh(); g.arriveAt('tolme');
  const offer = copy(g.commissionOffer('visit')), before = g.STATE.shiplets, actions = g.actionCounter;
  g.commissionProgress('visit', offer.target); assert(!g.STATE.commissions.active);
  g.acceptCommission({}, 'visit'); assert.strictEqual(g.actionCounter, actions + 1);
  assert.strictEqual(g.STATE.commissions.active.progress, 0);
  g.collectCommission({}); assert.strictEqual(g.STATE.shiplets, before);
  g.arriveAt(offer.target); assert.strictEqual(g.STATE.commissions.active.progress, 1);
  assert(g.STATE.sim.memories.some(m => m.kind === 'Work' && m.text.includes('completed a commission')));
  g.collectCommission({}); assert.strictEqual(g.actionCounter, actions + 2);
  assert.strictEqual(g.STATE.shiplets, before + offer.reward);
  assert.strictEqual(g.STATE.commissions.completed, 1); assert.strictEqual(g.STATE.commissions.serial, 1);
  g.collectCommission({}); assert.strictEqual(g.STATE.shiplets, before + offer.reward);
  assert(g.validSave(copy(g.saveSnapshot())));
});

test('scan commissions track the target only and persist while in progress', () => {
  const g = fresh(); g.arriveAt('tolme'); g.acceptCommission({}, 'scan');
  const c = copy(g.STATE.commissions.active), other = g.BODIES.find(b => b.id !== c.target);
  g.revealScan(other.id); assert.strictEqual(g.STATE.commissions.active.progress, 0);
  assert(/saved/i.test(g.saveGame(2))); g.STATE.commissions.active = null;
  assert(/restored/i.test(g.loadGame(2))); assert.strictEqual(g.STATE.commissions.active.target, c.target);
  g.revealScan(c.target); g.revealScan(c.target);
  assert.strictEqual(g.STATE.commissions.active.progress, 1);
  const before = g.STATE.shiplets; g.collectCommission({});
  assert.strictEqual(g.STATE.shiplets, before + c.reward);
});

test('salvage commissions count completed recoveries, including pro-action work', () => {
  const g = fresh(); g.arriveAt('tolme'); g.acceptCommission({}, 'salvage');
  const start = g.actionCounter;
  function recover(id) { const t = g.addOngoing(id, 'Recovery', 2, 'salvage', null); t.vol = 3; g.proAction(2); }
  recover('salvage-0'); assert.strictEqual(g.STATE.commissions.active.progress, 1);
  recover('salvage-1'); recover('salvage-2'); assert.strictEqual(g.STATE.commissions.active.progress, 2);
  assert.strictEqual(g.actionCounter, start);
  g.collectCommission({}); assert.strictEqual(g.STATE.commissions.completed, 1);
});

test('abandoning rotates offers, costs one action and respects optional confirmations', () => {
  const g = fresh(); g.arriveAt('tolme'); g.acceptCommission({}, 'visit');
  g.STATE.options.confirmRisk = true; g.window.confirm = () => false;
  const n = g.actionCounter, target = g.STATE.commissions.active.target;
  g.abandonCommission({}); assert.strictEqual(g.STATE.commissions.active.target, target); assert.strictEqual(g.actionCounter, n);
  g.window.confirm = () => true; g.abandonCommission({});
  assert.strictEqual(g.actionCounter, n + 1); assert.strictEqual(g.STATE.commissions.active, null);
  assert.strictEqual(g.STATE.commissions.serial, 1); assert.strictEqual(g.STATE.commissions.completed, 0);
});

test('commendations unlock from real progress, claim once for free and survive a save', () => {
  const g = fresh(); g.LIFE.slots.forEach(s => { s.state = 'installed'; });
  const n = g.actionCounter, fuel = g.STATE.fuelCells;
  g.runBank('The bank started.'); assert(g.STATE.chronicle.earned.breath); assert.strictEqual(g.readyCommendations(), 1);
  assert(g.claimCommendation('breath')); assert.strictEqual(g.STATE.fuelCells, fuel + 1);
  assert.strictEqual(g.actionCounter, n); assert(!g.claimCommendation('breath'));
  g.revealScan('halvan'); assert(g.STATE.chronicle.earned.eyes);
  g.arriveAt('tolme'); assert(g.STATE.chronicle.earned.berth); assert.strictEqual(g.captainRank(), 'Keeper');
  g.saveGame(3); g.STATE.chronicle.claimed.breath = false;
  assert(/restored/i.test(g.loadGame(3))); assert(g.STATE.chronicle.claimed.breath);
  assert(!g.claimCommendation('breath')); assert.strictEqual(g.STATE.fuelCells, fuel + 1);
});

test('the log counts only real scanned bodies and real resolved incidents', () => {
  const g = fresh(); g.STATE.scanned.fake = true; assert.strictEqual(g.chronicleValue('scans'), 0);
  g.revealScan('sirrus'); g.revealScan('sirrus'); assert.strictEqual(g.chronicleValue('scans'), 1);
  const ev = {id:'fracture-1',type:'fracture',kind:'Mechanical',source:'Hull sensors',stage:0,left:5};
  g.EVENTS.push(ev); g.removeIncident(ev.id); g.removeIncident(ev.id);
  assert.strictEqual(g.STATE.chronicle.incidents, 1);
});

test('older saves reconstruct provable progress without granting supplies', () => {
  const g = fresh(), d = copy(g.saveSnapshot());
  delete d.state.chronicle; delete d.state.options; delete d.state.commissions;
  d.state.scanned.halvan = true; d.state.contracts.arrival = 'complete';
  d.state.filterLife = 40; d.state.sim.dreams = [{at:0, text:'A retained dream.'}];
  const fuel = d.state.fuelCells, cash = d.state.shiplets;
  assert(g.validSave(d)); g.applySnapshot(d);
  assert(g.STATE.chronicle.earned.breath && g.STATE.chronicle.earned.eyes && g.STATE.chronicle.earned.berth);
  assert.strictEqual(g.STATE.chronicle.dreams, 1); assert(g.STATE.chronicle.systems['sec-kessler']);
  assert.strictEqual(g.STATE.fuelCells, fuel); assert.strictEqual(g.STATE.shiplets, cash);
  assert.strictEqual(g.STATE.options.largeText, false); assert.strictEqual(g.STATE.options.theme, 'classic');
  assert.strictEqual(g.STATE.options.soundEffects, true); assert.strictEqual(g.STATE.options.music, true);
  assert.strictEqual(g.STATE.options.audioLevel, 0.4); assert.strictEqual(g.STATE.commissions.active, null);
  assert(g.validSave(copy(g.saveSnapshot())));
});

test('palette and audio preferences persist, validate, and migrate from older option objects', () => {
  const g = fresh();
  assert.strictEqual(g.rasterInk(0.5), 'rgba(139,111,78,0.5)');
  g.STATE.options.theme = 'nebula'; g.STATE.options.soundEffects = false;
  assert.strictEqual(g.rasterInk(0.5), 'rgba(155,121,208,0.5)');
  g.STATE.options.music = false; g.STATE.options.audioLevel = 0.65;
  assert(g.validSave(copy(g.saveSnapshot()))); g.saveGame(2);
  g.STATE.options.theme = 'classic'; g.STATE.options.soundEffects = true;
  g.STATE.options.music = true; g.STATE.options.audioLevel = 0.18;
  assert(/restored/i.test(g.loadGame(2)));
  assert.strictEqual(g.STATE.options.theme, 'nebula'); assert.strictEqual(g.STATE.options.soundEffects, false);
  assert.strictEqual(g.STATE.options.music, false); assert.strictEqual(g.STATE.options.audioLevel, 0.65);

  const old = copy(g.saveSnapshot());
  old.state.options = {largeText:true, confirmRisk:true, hideCrewLabels:false};
  assert(g.validSave(old)); g.applySnapshot(old);
  assert.strictEqual(g.STATE.options.theme, 'classic'); assert.strictEqual(g.STATE.options.soundEffects, true);
  assert.strictEqual(g.STATE.options.music, true); assert.strictEqual(g.STATE.options.audioLevel, 0.4);
  assert(g.STATE.options.largeText && g.STATE.options.confirmRisk);
});

test('malformed progression or option state is rejected without replacing a voyage', () => {
  const g = fresh(), original = copy(g.saveSnapshot());
  const cases = [
    d => { d.state.options.confirmRisk = 'yes'; },
    d => { d.state.options.theme = 'sunshine'; },
    d => { d.state.options.soundEffects = 1; },
    d => { d.state.options.music = 'off'; },
    d => { d.state.options.audioLevel = 1.01; },
    d => { d.state.chronicle.systems.fake = true; },
    d => { d.state.chronicle.claimed.breath = true; },
    d => { d.state.chronicle.projects = -1; },
    d => { d.state.commissions.completed = 2; },
    d => { d.state.commissions.active = {kind:'visit',origin:'tolme',target:'fake',progress:1,reward:450}; },
    d => { d.state.commissions.active = {kind:'visit',origin:'tolme',target:'constructor',progress:0,reward:450}; },
    d => { d.state.commissions.active = {kind:'salvage',origin:'constructor',target:'',progress:0,reward:240}; },
    d => { d.state.commissions.active = {kind:'salvage',origin:'tolme',target:'',progress:3,reward:240}; },
    d => { d.state.commissions.active = {kind:'scan',origin:'tolme',target:'halvan',progress:0,reward:9000}; },
    d => { d.state.actionCooldowns = []; },
    d => { d.state.actionCooldowns = {bad:{category:'unknown',remaining:1}}; },
    d => { d.state.actionCooldowns = {bad:{category:'critical',remaining:21}}; }
  ];
  for (const mutate of cases) {
    const d = copy(original); mutate(d); assert(!g.validSave(d));
    g.storage[g.saveKey(1)] = JSON.stringify(d); assert(/Could not load/.test(g.loadGame()));
    assert.strictEqual(g.STATE.shiplets, original.state.shiplets);
    assert.strictEqual(g.actionCounter, original.actions);
  }
});

test('each action button has its own cooldown and every other committed action advances it', () => {
  const g = fresh(), event = mockButtons(g), host = {children:[], appendChild(el) { this.children.push(el); }};
  let timerCalls = 0;
  g.STATE.activeTab = 'probe';
  g.window.setTimeout = () => { timerCalls++; throw new Error('cooldowns must not use wall-clock timers'); };
  g.commandRow(host, 'Routine control', true, () => g.performAction('routine action'), '1 action', 'routine', 'routine-control');
  g.commandRow(host, 'Systems control', true, () => g.performAction('systems action'), '1 action', 'systems', 'systems-control');
  const routine = host.children[0], systems = host.children[1];
  const routineKey = g.actionCooldownKey('routine-control'), systemsKey = g.actionCooldownKey('systems-control');
  const before = g.actionCounter;
  routine.events.click(event());
  assert.strictEqual(g.actionCounter, before + 1);
  assert.strictEqual(g.STATE.actionCooldowns[routineKey].remaining, 5);
  assert.strictEqual(routine.costSpan.innerHTML, 'COOLDOWN 5');
  assert(routine.className.includes('act-cooldown'));

  const beforeSystems = g.actionCounter;
  let worldObservation = null;
  g.tickWorld = () => { worldObservation = {actions:g.actionCounter,
    routine:g.STATE.actionCooldowns[routineKey] && g.STATE.actionCooldowns[routineKey].remaining,
    systems:g.STATE.actionCooldowns[systemsKey]}; };
  systems.events.click(event());
  assert.deepStrictEqual(worldObservation, {actions:beforeSystems + 1, routine:4, systems:undefined},
    'the action counter increments, then existing cooldowns tick, before the world tick; the source lock starts afterward');
  assert.strictEqual(g.STATE.actionCooldowns[routineKey].remaining, 4, 'the other button’s action advances the first cooldown');
  assert.strictEqual(g.STATE.actionCooldowns[systemsKey].remaining, 10);
  const lockedActions = g.actionCounter, repeat = event();
  routine.events.click(repeat);
  assert.strictEqual(g.actionCounter, lockedActions, 'a blocked repeat is not a committed action');
  assert.strictEqual(repeat.prevented, 1); assert.strictEqual(repeat.stopped, 1);
  assert.strictEqual(g.STATE.actionCooldowns[routineKey].remaining, 4, 'a blocked click does not tick cooldowns');

  for (let i = 0; i < 4; i++) g.performAction('another committed action');
  assert.strictEqual(g.STATE.actionCooldowns[routineKey], undefined, 'five other actions clear the routine lock');
  assert.strictEqual(g.STATE.actionCooldowns[systemsKey].remaining, 6);
  routine.events.click(event());
  assert.strictEqual(g.STATE.actionCooldowns[routineKey].remaining, 5, 'the same control can be used again after its cooldown');
  assert.deepStrictEqual(JSON.parse(JSON.stringify(g.ACTION_COOLDOWN_RULES)), {
    routine:{actions:5,label:'Routine'}, systems:{actions:10,label:'Ship systems'},
    space:{actions:15,label:'Space operations'}, critical:{actions:20,label:'Critical'}
  });
  const criticalHost = {children:[], appendChild(el) { this.children.push(el); }};
  g.commandRow(criticalHost, 'Irreversible signal choice', true, () => g.performAction('critical action'),
    '1 action', 'critical', 'critical-control');
  criticalHost.children[0].events.click(event());
  const criticalCooldown = g.STATE.actionCooldowns[g.actionCooldownKey('critical-control')];
  assert.strictEqual(criticalCooldown.category, 'critical');
  assert.strictEqual(criticalCooldown.remaining, 20);
  assert.strictEqual(timerCalls, 0);
});

test('cooldowns survive redraw and save/load, and older saves migrate cleanly', () => {
  const g = fresh(), event = mockButtons(g), host = {children:[], appendChild(el) { this.children.push(el); }};
  g.STATE.activeTab = 'persistent-screen';
  g.commandRow(host, 'Persistent service', true, () => g.performAction('service'), '1 action', 'systems', 'persistent-service');
  const button = host.children[0], key = g.actionCooldownKey('persistent-service');
  button.events.click(event());
  assert.strictEqual(g.STATE.actionCooldowns[key].remaining, 10);
  assert(/saved/i.test(g.saveGame(2)));

  /* Rebuilding a screen reattaches DOM refs to the persistent STATE record. */
  g.ACTION_COOLDOWN_CONTROLS = {};
  const redrawn = {children:[], appendChild(el) { this.children.push(el); }};
  g.commandRow(redrawn, 'Persistent service', true, () => g.performAction('service'), '1 action', 'systems', 'persistent-service');
  assert(redrawn.children[0].className.includes('act-cooldown'));
  assert.strictEqual(redrawn.children[0].costSpan.innerHTML, 'COOLDOWN 10');

  g.STATE.actionCooldowns = {};
  assert(/restored/i.test(g.loadGame(2)));
  assert.strictEqual(g.STATE.actionCooldowns[key].remaining, 10);
  const afterLoad = {children:[], appendChild(el) { this.children.push(el); }};
  g.commandRow(afterLoad, 'Persistent service', true, () => g.performAction('service'), '1 action', 'systems', 'persistent-service');
  const loadedButton = afterLoad.children[0], count = g.actionCounter, blocked = event();
  loadedButton.events.click(blocked);
  assert.strictEqual(g.actionCounter, count);
  assert.strictEqual(blocked.prevented, 1);
  assert(loadedButton.className.includes('act-cooldown'));

  const legacy = copy(g.saveSnapshot()); delete legacy.state.actionCooldowns;
  assert(g.validSave(legacy), 'saves predating cooldown state remain loadable');
  g.applySnapshot(legacy);
  assert.deepStrictEqual(copy(g.STATE.actionCooldowns), {});
  const malformed = copy(g.saveSnapshot());
  malformed.state.actionCooldowns = {bad:{category:'routine',remaining:0}};
  assert(!g.validSave(malformed), 'invalid cooldown records are rejected');
});

test('progress actions and Navigation Stand watch obey their own category cooldowns', () => {
  const g = fresh(), event = mockButtons(g);
  let starts = 0;
  g.STATE.activeTab = 'nav:main';
  const progress = g.buildProgressButton('cooldown-probe', 'Start test work', () => {
    starts++; g.performAction('start test work');
  }, true, 'Complete', 'systems');
  const progressKey = g.actionCooldownKey('progress:cooldown-probe');
  progress.events.click(event());
  assert.strictEqual(starts, 1);
  assert.strictEqual(g.STATE.actionCooldowns[progressKey].remaining, 10);
  assert(progress.className.includes('pbtn-cooldown'));
  const count = g.actionCounter, blocked = event();
  progress.events.click(blocked);
  assert.strictEqual(starts, 1); assert.strictEqual(g.actionCounter, count);
  assert.strictEqual(blocked.prevented, 1);
  g.performAction('another action');
  assert.strictEqual(g.STATE.actionCooldowns[progressKey].remaining, 9);

  const nav = {children:[], appendChild(el) { this.children.push(el); }};
  g.commandRow(nav, 'Stand watch - advance ongoing work', true, g.standWatch);
  const watch = nav.children[0]; watch.events.click(event());
  const watchKey = Object.keys(g.STATE.actionCooldowns).find(k => k.indexOf('stand-watch') >= 0);
  assert(watchKey, 'Stand watch has a persistent per-control record');
  assert.strictEqual(g.STATE.actionCooldowns[watchKey].category, 'space');
  assert.strictEqual(g.STATE.actionCooldowns[watchKey].remaining, 15);
  const afterWatch = g.actionCounter;
  watch.events.click(event());
  assert.strictEqual(g.actionCounter, afterWatch, 'Stand watch cannot be spammed');
});

test('offline audio cues generate valid WAV data and distinguish scans, travel and completion', () => {
  const g = fresh(), uri = g.waveDataUri('fx-ping'), bytes = Buffer.from(uri.split(',')[1], 'base64');
  assert(uri.startsWith('data:audio/wav;base64,'));
  assert.strictEqual(bytes.toString('ascii', 0, 4), 'RIFF');
  assert.strictEqual(bytes.toString('ascii', 8, 12), 'WAVE');
  assert.strictEqual(bytes.readUInt32LE(24), 8000);
  assert.strictEqual(g.cueKind('Deep scan started'), 'ping');
  assert.strictEqual(g.cueKind('Course locked'), 'thrust');
  assert.strictEqual(g.cueKind('Arrival complete'), 'chime');
  assert.notStrictEqual(g.waveDataUri('music'), uri);
});

test('three save slots are independent and a missing slot leaves the current game intact', () => {
  const g = fresh();
  g.STATE.shiplets = 101; g.saveGame();
  g.STATE.shiplets = 202; g.STATE.options.largeText = true; g.saveGame(2);
  g.STATE.shiplets = 303; g.STATE.options.largeText = false; g.saveGame(3);
  assert(/restored/i.test(g.loadGame(2))); assert.strictEqual(g.STATE.shiplets, 202); assert(g.STATE.options.largeText);
  assert(/restored/i.test(g.loadGame(3))); assert.strictEqual(g.STATE.shiplets, 303);
  assert(/restored/i.test(g.loadGame())); assert.strictEqual(g.STATE.shiplets, 101);
  assert.strictEqual(g.saveFilename(1), 'Spaceship_save.txt'); assert.strictEqual(g.saveFilename(2), 'Spaceship_save_2.txt');
  delete g.storage[g.saveKey(2)]; assert(/Could not load/.test(g.loadGame(2))); assert.strictEqual(g.STATE.shiplets, 101);
});

test('wiki and rewards tabs are valid in saved voyages', () => {
  const g = fresh(), d = copy(g.saveSnapshot());
  d.state.tabs = ['wiki','awards'].map(kind => ({kind,id:'main',key:kind+':main',kindLabel:kind,title:kind}));
  d.state.activeTab = 'wiki:main'; assert(g.validSave(d)); g.applySnapshot(d);
  assert.strictEqual(g.STATE.activeTab, 'wiki:main'); assert(g.validSave(copy(g.saveSnapshot())));
});

test('every commendation pays its documented reward once and ranks follow earned progress', () => {
  const g = fresh();
  g.runBank('Bank running.'); g.arriveAt('tolme');
  for (const id of ['halvan','sirrus','coldwell','relay','vance-world']) g.revealScan(id);
  const regions = new Set();
  for (const sys of g.SECTOR_BODIES) {
    if (!regions.has(sys.region)) { regions.add(sys.region); g.arriveAt(sys.id); }
  }
  const dream = {target:'dream',payload:[{kind:'Arrival',text:'One',who:'',other:''},{kind:'World',text:'Two',who:'',other:''}]};
  g.TASK_TYPES.shipProject.onDone(dream); g.TASK_TYPES.shipProject.onDone(dream);
  const meal = {target:'social',who:'rell',other:'jax'};
  g.TASK_TYPES.shipProject.onDone(meal); g.TASK_TYPES.shipProject.onDone(meal);
  for (let i=0; i<3; i++) { const ev={id:'x'+i,type:'fracture',kind:'Mechanical',source:'Hull',stage:0,left:5}; g.EVENTS.push(ev); g.removeIncident(ev.id); }
  for (const q of g.CONTRACTS.slice(0,3)) g.STATE.contracts[q.id]='complete';
  g.STATE.commissions.completed=3; g.STATE.commissions.serial=3;
  g.actionCounter=200; g.STATE.sim.signal='sold'; g.checkCommendations();
  assert.strictEqual(g.captainRank(), 'Constellation'); assert.strictEqual(g.readyCommendations(), g.COMMENDATIONS.length);
  const actions=g.actionCounter;
  for (const a of g.COMMENDATIONS) {
    const before={cash:g.STATE.shiplets,fuel:g.STATE.fuelCells,kits:g.STATE.repairKits,med:g.STATE.medkits};
    assert(g.claimCommendation(a.id), a.id);
    assert.strictEqual(g.STATE.shiplets, before.cash+(a.reward.cash||0), a.id);
    assert.strictEqual(g.STATE.fuelCells, before.fuel+(a.reward.fuel||0), a.id);
    assert.strictEqual(g.STATE.repairKits, before.kits+(a.reward.kits||0), a.id);
    assert.strictEqual(g.STATE.medkits, before.med+(a.reward.med||0), a.id);
    assert(!g.claimCommendation(a.id), a.id);
  }
  assert.strictEqual(g.actionCounter,actions); assert.strictEqual(g.readyCommendations(),0);
  assert(g.validSave(copy(g.saveSnapshot())));
});
