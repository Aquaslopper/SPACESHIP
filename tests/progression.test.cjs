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
  assert.strictEqual(g.STATE.options.largeText, false); assert.strictEqual(g.STATE.commissions.active, null);
  assert(g.validSave(copy(g.saveSnapshot())));
});

test('malformed progression or option state is rejected without replacing a voyage', () => {
  const g = fresh(), original = copy(g.saveSnapshot());
  const cases = [
    d => { d.state.options.confirmRisk = 'yes'; },
    d => { d.state.chronicle.systems.fake = true; },
    d => { d.state.chronicle.claimed.breath = true; },
    d => { d.state.chronicle.projects = -1; },
    d => { d.state.commissions.completed = 2; },
    d => { d.state.commissions.active = {kind:'visit',origin:'tolme',target:'fake',progress:1,reward:450}; },
    d => { d.state.commissions.active = {kind:'visit',origin:'tolme',target:'constructor',progress:0,reward:450}; },
    d => { d.state.commissions.active = {kind:'salvage',origin:'constructor',target:'',progress:0,reward:240}; },
    d => { d.state.commissions.active = {kind:'salvage',origin:'tolme',target:'',progress:3,reward:240}; },
    d => { d.state.commissions.active = {kind:'scan',origin:'tolme',target:'halvan',progress:0,reward:9000}; }
  ];
  for (const mutate of cases) {
    const d = copy(original); mutate(d); assert(!g.validSave(d));
    g.storage[g.saveKey(1)] = JSON.stringify(d); assert(/Could not load/.test(g.loadGame()));
    assert.strictEqual(g.STATE.shiplets, original.state.shiplets);
    assert.strictEqual(g.actionCounter, original.actions);
  }
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
