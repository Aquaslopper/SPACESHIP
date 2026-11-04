/* Run with: node tests/galaxy.test.cjs. No browser or npm packages required. */
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const hta = fs.readFileSync(path.join(__dirname, '..', 'Spaceship.hta'), 'utf8');
const script = hta.match(/<script type="text\/javascript">([\s\S]*?)<\/script>/);
assert(script, 'game script not found');

function fresh() {
  let stored = '';
  const context = {
    document: {getElementById: () => null},
    window: {location: {protocol: 'http:'}, localStorage: {
      setItem: (key, value) => { stored = value; },
      getItem: () => stored
    }}
  };
  vm.createContext(context);
  vm.runInContext(script[1], context, {filename: 'Spaceship.hta'});
  return context;
}
function test(name, fn) { fn(); console.log('PASS: ' + name); }

test('catalogue identifiers, ports, routes and contract destinations', () => {
  const g = fresh(), ids = new Set();
  assert(g.GALAXY_BODIES.length >= 7 && g.SECTOR_BODIES.length >= 36 && g.BODIES.length >= 111);
  for (const b of [...g.GALAXY_BODIES, ...g.SECTOR_BODIES, ...g.BODIES]) {
    assert(!ids.has(b.id), 'duplicate id: ' + b.id); ids.add(b.id);
    assert(b.x >= 0 && b.x <= g.MAP.w && b.y >= 0 && b.y <= g.MAP.h, b.id);
  }
  for (const sys of g.SECTOR_BODIES) {
    assert(g.findById(g.GALAXY_BODIES, sys.region), sys.id + ' has no region');
    const entry = g.findById(g.BODIES, sys.entry);
    assert(entry && g.bodySystem(entry) === sys.id, sys.id + ' has no local entry');
  }
  for (const id of Object.keys(g.PORTS)) {
    const b = g.findById(g.BODIES, id);
    assert(b && b.type === 'station' && g.PORTS[id].name === b.name, id);
  }
  for (const link of g.SECTOR_LINKS) {
    const a = g.findById(g.SECTOR_BODIES, link[0]), b = g.findById(g.SECTOR_BODIES, link[1]);
    assert(a && b && a.region === b.region, 'bad sector lane: ' + link);
  }
  for (const q of g.CONTRACTS) {
    assert(g.findById(g.BODIES, q.target) || g.findById(g.SECTOR_BODIES, q.target), q.id);
    if (q.origin) assert(g.PORTS[q.origin], q.id + ' has no origin port');
    if (q.goal === 'freight' || q.goal === 'passenger') assert(g.PORTS[q.target], q.id + ' has no destination port');
  }
});

test('the expanded chart adds outer worlds and a mapped natural moon to every system', () => {
  const g = fresh();
  assert.strictEqual(g.GALAXY_BODIES.length, 7);
  assert.strictEqual(g.SECTOR_BODIES.length, 48);
  assert.strictEqual(g.BODIES.length, 243);
  assert.strictEqual(Object.keys(g.PORTS).length, 30);
  assert.strictEqual(g.BODIES.filter(b => b.type === 'star').length, 50);
  assert.strictEqual(g.BODIES.filter(b => b.type === 'planet').length, 93);
  assert.strictEqual(g.BODIES.filter(b => b.type === 'moon').length, 48);
  for (const sys of g.SECTOR_BODIES) {
    const prefix = sys.id.substring(4), world = g.findById(g.BODIES, prefix + '-outer');
    const moon = g.findById(g.BODIES, prefix + '-moon');
    assert(world && world.type === 'planet' && world.system === sys.id, sys.id + ' outer world');
    assert(moon && moon.type === 'moon' && moon.system === sys.id, sys.id + ' natural moon');
    assert.strictEqual(moon.details.find(row => row[0] === 'Satellite of')[1], world.name);
    assert(world.details.some(row => row[0] === 'Atmosphere'));
    assert(moon.scan.length === 2 && moon.scan[0].includes('satellite'));
  }
  g.revealScan('kessler-moon');
  assert(g.STATE.scanned['kessler-moon']);
  assert(g.validSave(JSON.parse(JSON.stringify(g.saveSnapshot()))));
});

test('all charted systems can be reached within a full fuel tank', () => {
  const g = fresh();
  for (const from of g.SECTOR_BODIES) {
    g.MAP.here = from.entry;
    for (const to of g.SECTOR_BODIES) {
      const distance = g.navMetrics(to).dist;
      assert(from.id === to.id ? distance === 0 : distance > 0 && distance <= 100,
        from.id + ' -> ' + to.id + ': ' + distance);
    }
  }
});

test('a jump costs fuel and an action, then changes the local chart on acknowledgement', () => {
  const g = fresh();
  g.fireAction = () => { g.actionCounter++; }; // isolate navigation from the world clock
  g.MAP.layer = 'sector'; g.STATE.navTarget = 'sec-vance';
  const distance = g.navMetrics(g.findBody('sec-vance')).dist, fuel = g.statValue('fuel').value;
  g.startTravel({});
  const trip = g.findOngoing('travel');
  assert(trip && trip.target === 'sec-vance' && trip.left === distance);
  assert.strictEqual(g.actionCounter, 1); assert.strictEqual(g.statValue('fuel').value, fuel - distance);
  g.advanceTasks(distance); assert.strictEqual(trip.state, 'done');
  assert.strictEqual(g.MAP.here, 'kessler', 'arrival must await acknowledgement');
  g.TASK_TYPES.travel.onAcknowledge(trip); g.clearOngoing('travel');
  assert.strictEqual(g.MAP.here, 'vance-star');
  assert.strictEqual(g.currentSystem().id, 'sec-vance');
  g.MAP.layer = 'system';
  assert(g.curBodies().some(b => b.id === 'vance-port'));
  assert(!g.curBodies().some(b => b.id === 'tolme'));
  assert(!g.inRange(g.findById(g.BODIES, 'tolme')));
});

test('old saves migrate, in-flight jumps survive, and invalid regions are rejected', () => {
  const g = fresh();
  let old = JSON.parse(JSON.stringify(g.saveSnapshot())); delete old.map.sector;
  assert(g.validSave(old)); g.applySnapshot(old);
  assert.strictEqual(g.MAP.sector, 'gal-inner'); assert.strictEqual(g.MAP.here, 'kessler');
  g.MAP.layer = 'sector'; g.MAP.sector = 'gal-core'; g.STATE.navTarget = 'sec-lumen';
  g.fireAction = () => { g.actionCounter++; }; g.startTravel({});
  const saved = JSON.parse(JSON.stringify(g.saveSnapshot()));
  assert(g.validSave(saved));
  g.applySnapshot(saved); assert.strictEqual(g.findOngoing('travel').target, 'sec-lumen');
  const before = JSON.stringify(g.saveSnapshot()), bad = JSON.parse(before);
  bad.map.sector = 'gal-unknown'; g.window.localStorage.setItem('spaceship-command-save', JSON.stringify(bad));
  assert(g.loadGame().indexOf('Could not load') >= 0);
  assert.strictEqual(JSON.stringify(g.saveSnapshot()), before);
});

test('regional passenger delivery and remote scan acknowledgement', () => {
  const g = fresh();
  g.STATE.contracts['core-passenger'] = 'active';
  g.STATE.passengers.push({id:'core-passenger', name:'Station passenger'});
  g.arriveAt('lumen-port');
  assert.strictEqual(g.STATE.contracts['core-passenger'], 'ready');
  assert.strictEqual(g.STATE.passengers.length, 0); assert(g.STATE.docked);
  g.MAP.layer = 'sector'; g.STATE.navTarget = 'sec-lumen';
  g.revealScan('vance-world');
  assert(g.STATE.scanned['vance-world']);
  assert.strictEqual(g.STATE.navTarget, 'sec-lumen', 'remote scan should not replace a sector selection');
  assert(g.findById(g.SURVEY, 'sv-vance-world'));
});

test('regional markets offer a genuine trade margin', () => {
  const g = fresh(), water = g.findById(g.MARKET, 'water');
  g.MAP.here = 'fallow-port'; const purchase = Math.ceil(g.lotPrice(water) * 1.2);
  g.MAP.here = 'frost-port'; const sale = g.lotPrice(water);
  assert(sale > purchase, 'water should sell for more in the halo than it costs in Fallow');
});

test('the efficient drive reduces fuel without reducing travel time', () => {
  const g = fresh(), destination = g.findById(g.SECTOR_BODIES, 'sec-vance');
  const normal = g.navMetrics(destination);
  g.STATE.upgrades.drive = true;
  const upgraded = g.navMetrics(destination);
  assert.strictEqual(upgraded.dist, normal.dist);
  assert.strictEqual(upgraded.fuel, Math.ceil(normal.dist * 0.8));
  g.MAP.layer = 'sector'; g.STATE.navTarget = destination.id;
  g.fireAction = () => { g.actionCounter++; };
  const before = g.statValue('fuel').value; g.startTravel({});
  assert.strictEqual(before - g.statValue('fuel').value, upgraded.fuel);
  assert.strictEqual(g.findOngoing('travel').left, normal.dist);
});
