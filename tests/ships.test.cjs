/* Run with: node tests/ships.test.cjs. No browser or npm packages required. */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const hta = fs.readFileSync(path.join(__dirname, '..', 'Spaceship.hta'), 'utf8');
const script = hta.match(/<script type="text\/javascript">([\s\S]*?)<\/script>/);
assert(script, 'game script not found');
function fresh() {
  const storage = {}, dialog = {accept:true, calls:0};
  const g = {document:{getElementById:()=>null}, window:{location:{protocol:'http:'},
    confirm:()=>{ dialog.calls++; return dialog.accept; }, localStorage:{
      setItem:(k,v)=>{storage[k]=v;}, getItem:k=>storage[k] || null
    }}};
  vm.createContext(g); vm.runInContext(script[1], g);
  g.repaintCurrent = () => {}; g.redrawAfterLoad = () => {};
  g.storage = storage; g.dialog = dialog; return g;
}
const copy = x => JSON.parse(JSON.stringify(x));
const test = (name, fn) => { fn(); console.log('PASS: ' + name); };

test('a new voyage begins on a compact ship with working facilities and constrained cargo and berths', () => {
  const g = fresh();
  assert.strictEqual(g.STATE.shipClass, 'wren'); assert.strictEqual(g.SHIP.name, 'SC Wren');
  assert.strictEqual(g.SHIP.berths, '6'); assert.strictEqual(g.CARGO.capacity, 40);
  assert.strictEqual(g.cargoUsed(), 32); assert.strictEqual(g.ROOMS.length, 21);
  assert.strictEqual(g.HULL.length, 6); assert(g.findById(g.ROOMS, 'simulacron'));
  for (const r of g.ROOMS) assert(r.x + r.w <= 600 && r.y + r.h <= 365, r.name);
  assert(g.validSave(copy(g.saveSnapshot())));
});

test('each ship has staged refits, with no capacity lost on transfer', () => {
  const g = fresh();
  assert.deepStrictEqual([0,1,2].map(i=>g.shipCargoCapacity('wren',i)), [40,60,90]);
  assert.deepStrictEqual([0,1,2].map(i=>g.shipCargoCapacity('kestrel',i)), [60,90,120]);
  for (let level=0; level<=2; level++) {
    const h = fresh(); h.arriveAt('tolme'); h.STATE.shiplets = 6000;
    h.STATE.upgrades.bay = level >= 1; h.STATE.upgrades.bay2 = level >= 2;
    h.CARGO.capacity = h.shipCargoCapacity('wren', level);
    assert(h.buyLargerShip({}));
    assert.strictEqual(h.CARGO.capacity, h.shipCargoCapacity('kestrel', level));
    assert.strictEqual(h.cargoRefitLevel(), level);
    assert(h.validSave(copy(h.saveSnapshot())));
  }
});

test('purchase is blocked in space, without funds, at game over, or when confirmation is declined', () => {
  const g = fresh(); g.STATE.shiplets = 5000;
  const initial = g.actionCounter;
  assert.strictEqual(g.buyLargerShip({}), false); assert.strictEqual(g.dialog.calls, 0);
  g.arriveAt('tolme'); g.STATE.shiplets = 3999;
  assert.strictEqual(g.buyLargerShip({}), false); assert.strictEqual(g.dialog.calls, 0);
  g.STATE.shiplets = 5000; g.STATE.gameOver = 'Lost';
  assert.strictEqual(g.buyLargerShip({}), false); assert.strictEqual(g.dialog.calls, 0);
  g.STATE.gameOver = ''; g.dialog.accept = false;
  assert.strictEqual(g.buyLargerShip({}), false); assert.strictEqual(g.dialog.calls, 1);
  assert.strictEqual(g.STATE.shiplets, 5000); assert.strictEqual(g.actionCounter, initial);
  assert.strictEqual(g.STATE.shipClass, 'wren');
});

test('buying the larger ship transfers the live voyage and ticks an ongoing task exactly once', () => {
  const g = fresh(); g.arriveAt('tolme'); g.STATE.shiplets = 5000;
  g.STATE.upgrades.bay = true; g.CARGO.capacity = 60; g.STATE.upgrades.drive = true;
  g.STATE.options.largeText = true; g.statValue('hull').value = 64;
  g.STATE.contracts.freight = 'active';
  g.GOODS.push({id:'contract-freight', name:'Medical consignment', vol:8, value:0,
    kind:'Contract freight', risk:false, contractId:'freight'});
  g.STATE.contracts.passage = 'active'; g.STATE.passengers.push({id:'passage', name:'Station passenger'});
  g.STATE.commissions.active = copy(g.commissionOffer('salvage'));
  g.startSocial({}, 'rell', 'jax');
  const task = g.findOngoing('project-social'), left = task.left, n = g.actionCounter;
  const crew = g.CREW.map(c=>c.id), goods = copy(g.GOODS), survey = copy(g.SURVEY), passengers = copy(g.STATE.passengers);
  const money = g.STATE.shiplets, fuel = g.STATE.fuelCells, location = g.MAP.here;
  assert(g.buyLargerShip({}));
  assert.strictEqual(g.actionCounter, n + 1); assert.strictEqual(g.STATE.shiplets, money - 4000);
  assert.strictEqual(g.STATE.shipClass, 'kestrel'); assert.strictEqual(g.SHIP.name, 'SC Tessellate');
  assert.strictEqual(g.SHIP.berths, '14'); assert.strictEqual(g.CARGO.capacity, 90);
  assert.strictEqual(g.HULL.length, 10); assert.strictEqual(g.findOngoing('project-social').left, left - 1);
  assert.strictEqual(g.statValue('hull').value, 64); assert.strictEqual(g.STATE.fuelCells, fuel);
  assert.strictEqual(g.MAP.here, location); assert.strictEqual(g.STATE.contracts.freight, 'active');
  assert.strictEqual(g.STATE.commissions.active.kind, 'salvage'); assert(g.STATE.upgrades.drive);
  assert(g.STATE.options.largeText); assert.deepStrictEqual(g.CREW.map(c=>c.id), crew);
  assert.deepStrictEqual(copy(g.GOODS), goods); assert.deepStrictEqual(copy(g.SURVEY), survey);
  assert.deepStrictEqual(copy(g.STATE.passengers), passengers);
  assert(g.STATE.sim.memories.some(m=>m.text.includes('transferred from SC Wren')));
  assert(g.validSave(copy(g.saveSnapshot())));
  assert.strictEqual(g.buyLargerShip({}), false); assert.strictEqual(g.STATE.shiplets, money - 4000);
});

test('independent slots restore either hull and its matching deck plan and refits', () => {
  const g = fresh(); assert(/saved/i.test(g.saveGame(1)));
  g.arriveAt('tolme'); g.STATE.shiplets = 5000; assert(g.buyLargerShip({}));
  g.STATE.upgrades.bay = true; g.CARGO.capacity = 90; assert(/saved/i.test(g.saveGame(2)));
  assert(/restored/i.test(g.loadGame(1))); assert.strictEqual(g.STATE.shipClass, 'wren');
  assert.strictEqual(g.CARGO.capacity, 40); assert.strictEqual(g.findById(g.ROOMS, 'engine').x, 35);
  assert(/restored/i.test(g.loadGame(2))); assert.strictEqual(g.STATE.shipClass, 'kestrel');
  assert.strictEqual(g.CARGO.capacity, 90); assert.strictEqual(g.findById(g.ROOMS, 'engine').x, 100);
  assert(g.STATE.upgrades.bay); assert(g.validSave(copy(g.saveSnapshot())));
});

test('pre-class saves migrate as the established larger ship without losing their capacity or identity', () => {
  const g = fresh();
  for (let level=0; level<=2; level++) {
    const h = fresh(), old = copy(h.saveSnapshot());
    delete old.state.shipClass; old.ship = copy(h.LARGE_SHIP); old.ship.name = 'SC Étoile';
    old.rooms = copy(h.ROOM_BLUEPRINTS); old.state.upgrades.bay = level >= 1;
    old.state.upgrades.bay2 = level >= 2; old.cargo.capacity = [60,90,120][level];
    assert(h.validSave(old)); h.applySnapshot(old);
    assert.strictEqual(h.STATE.shipClass, 'kestrel'); assert.strictEqual(h.SHIP.name, 'SC Étoile');
    assert.strictEqual(h.CARGO.capacity, [60,90,120][level]); assert.strictEqual(h.SHIP.berths, '14');
    assert.strictEqual(h.findById(h.ROOMS, 'engine').x, 100); assert(h.validSave(copy(h.saveSnapshot())));
  }
});

test('unknown ship classes are rejected without replacing the live voyage', () => {
  const g = fresh(), before = JSON.stringify(g.saveSnapshot()), bad = JSON.parse(before);
  for (const kind of ['constructor', 'giant', null, 2]) {
    bad.state.shipClass = kind; assert(!g.validSave(bad));
    g.storage['spaceship-command-save-3'] = JSON.stringify(bad);
    assert(/could not load/i.test(g.loadGame(3))); assert.strictEqual(JSON.stringify(g.saveSnapshot()), before);
  }
});

test('restarting restores the small ship and world while keeping preferences and saved slots', () => {
  const g = fresh(); g.BOOT_SNAPSHOT = JSON.stringify(g.saveSnapshot());
  g.STATE.shiplets = 777; g.saveGame(2); g.arriveAt('tolme');
  g.STATE.shiplets = 5000; assert(g.buyLargerShip({}));
  g.STATE.options.largeText = true; g.STATE.options.confirmRisk = true; g.STATE.reducedMotion = true;
  g.restartVoyage();
  assert.strictEqual(g.STATE.shipClass, 'wren'); assert.strictEqual(g.SHIP.name, 'SC Wren');
  assert.strictEqual(g.CARGO.capacity, 40); assert.strictEqual(g.STATE.shiplets, 340);
  assert.strictEqual(g.MAP.here, 'kessler'); assert.strictEqual(g.actionCounter, 0);
  assert(g.STATE.options.largeText && g.STATE.options.confirmRisk && g.STATE.reducedMotion);
  assert(/restored/i.test(g.loadGame(2))); assert.strictEqual(g.STATE.shiplets, 777);
});

test('the title menu hides the dashboard, resumes safely and confirms a fresh voyage', () => {
  const g = fresh(), nodes = {};
  function element() { return {style:{}, events:{}, setAttribute:()=>{}, focus:()=>{}}; }
  const shell = nodes['game-shell'] = element();
  g.document.documentElement = {clientWidth:1024, clientHeight:768};
  g.document.body = {appendChild:e=>{e.parentNode=g.document.body;nodes[e.id]=e;},
    removeChild:e=>{delete nodes[e.id];e.parentNode=null;}};
  g.$ = id => nodes[id] || null;
  g.make = (tag, cls, html) => {
    const e = element(), re = /id="([^"]+)"/g; let match;
    while ((match = re.exec(html || ''))) nodes[match[1]] = element();
    return e;
  };
  g.on = (e, type, fn) => { e.events[type] = fn; };
  g.removeFloatingPopups = () => {}; g.switchTab = () => {};
  g.BOOT_SNAPSHOT = JSON.stringify(g.saveSnapshot());
  g.showTitleMenu(false); assert.strictEqual(shell.style.display, 'none');
  assert(nodes['title-start'] && nodes['title-load'] && nodes['title-options'] && nodes['title-guide']);
  nodes['title-start'].events.click(); assert(!nodes['title-screen']);
  assert.strictEqual(shell.style.display, 'block'); assert.strictEqual(g.actionCounter, 0);
  g.STATE.shiplets = 777; g.actionCounter = 5;
  g.showTitleMenu(true); nodes['title-resume'].events.click();
  assert.strictEqual(g.STATE.shiplets, 777); assert.strictEqual(g.actionCounter, 5);
  g.showTitleMenu(true); g.dialog.accept = false; nodes['title-start'].events.click();
  assert(nodes['title-screen']); assert.strictEqual(g.STATE.shiplets, 777);
  g.dialog.accept = true; nodes['title-start'].events.click();
  assert(!nodes['title-screen']); assert.strictEqual(g.STATE.shiplets, 340);
  assert.strictEqual(g.actionCounter, 0); assert.strictEqual(g.STATE.shipClass, 'wren');
});

test('standing watch advances a crossing and the world without spending inventory', () => {
  const g = fresh(); g.STATE.navTarget = 'tolme'; g.startTravel({});
  const trip = g.findOngoing('travel'), left = trip.left, n = g.actionCounter;
  const goods = JSON.stringify(g.GOODS), cells = g.STATE.fuelCells, air = g.STATE.airHold;
  g.standWatch({});
  assert.strictEqual(g.actionCounter, n + 1); assert.strictEqual(trip.left, left - 1);
  assert.strictEqual(g.STATE.airHold, air - 1); assert.strictEqual(g.STATE.fuelCells, cells);
  assert.strictEqual(JSON.stringify(g.GOODS), goods);
  g.STATE.gameOver = 'Lost'; g.standWatch({}); assert.strictEqual(g.actionCounter, n + 1);
});
