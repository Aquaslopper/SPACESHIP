#!/usr/bin/env node
'use strict';
// This gate deliberately never evaluates or compares HTA lines after 2000.
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert/strict');
const cp = require('child_process');
const root = path.resolve(__dirname, '..');
process.chdir(root);
const source = fs.readFileSync('Spaceship.hta', 'utf8').split(/\r?\n/).slice(0, 2000).join('\n');
const css = source.slice(source.indexOf('<style'), source.indexOf('</style>')).replace(/\/\*[\s\S]*?\*\//g, '');
const rules = [...css.matchAll(/([^{}]+)\{([^{}]*)\}/g)].map(m => ({
  selectors: m[1].split(',').map(s => s.trim()),
  props: Object.fromEntries([...m[2].matchAll(/([\w-]+)\s*:\s*([^;]+)/g)].map(p => [p[1], p[2].trim()]))
}));
function cssValue(selector, prop) {
  let value;
  for (const rule of rules) if (rule.selectors.includes(selector) && prop in rule.props) value = rule.props[prop];
  assert.ok(value, `missing bounded CSS: ${selector} ${prop}`);
  return value;
}
const data = {};
// MAP begins inside the range but ends outside it. Do NOT complete/evaluate it.
vm.runInNewContext(source.slice(source.indexOf('var LARGE_SHIP'), source.indexOf('var MAP =')), data);
function subset(expected, actual, label) {
  if (expected && typeof expected === 'object' && !Array.isArray(expected)) {
    for (const key of Object.keys(expected)) subset(expected[key], actual[key], label + '.' + key);
  } else assert.deepEqual(JSON.parse(JSON.stringify(expected)), actual, label);
}
const mappings = {
  ship: {LARGE_SHIP:'ships.kestrel', SHIP:'ships.wren', HULL:'hulls.kestrel', ROOMS:'rooms.kestrel', CORRIDOR:'corridors.kestrel', ROOM_SCENES:'roomScenes'},
  crew: {CREW:'crew', JOBS:'jobs', ORDERS:'orders', DOC_AWAY:'docAway', PAY:'pay'},
  survival: {STATS:'stats', INFOS:'infos', RESOURCES:'resources', LIFE:'life', AIR:'air', AIR_STAGES:'airStages', CARGO:'cargo', GOODS:'goods', SURVEY:'survey', SURVEY_WORTH:'surveyWorth', MESS:'mess', MESS_EVERY:'messEvery', MESS_STARVE:'messStarve', MESS_STARVE_EVERY:'messStarveEvery'}
};
let declarations = 0;
for (const [file, entries] of Object.entries(mappings)) {
  const native = JSON.parse(fs.readFileSync(`assets/data/${file}.json`, 'utf8'));
  for (const [name, field] of Object.entries(entries)) {
    subset(data[name], field.split('.').reduce((obj, key) => obj[key], native), name);
    declarations++;
  }
}
const partialMap = source.slice(source.indexOf('var MAP =')).replace(/\/\*[\s\S]*?\*\//g, '');
const nativeMap = JSON.parse(fs.readFileSync('assets/data/chart.json', 'utf8')).map;
let mapFields = 0;
for (const m of partialMap.matchAll(/\b(\w+)\s*:\s*("[^"]*"|[0-9.]+)/g)) {
  assert.deepEqual(JSON.parse(m[2]), nativeMap[m[1]], 'bounded MAP.' + m[1]);
  mapFields++;
}
const jpeg = source.match(/data:image\/jpeg;base64,([^"\s]+)/)[1];
assert.deepEqual(Buffer.from(jpeg, 'base64'), fs.readFileSync('assets/art/title.jpg'), 'line 1265 title JPEG');
const roles = {
  panel:['.panel','background'], panel2:['.panel-head','background'], panel3:['.tab','background'],
  border:['.panel','border-color'], borderDark:['.act','border-color'], coffee:['.panel-head','color'],
  ink:['','color'], muted:['.task-sub','color'], content:['#content','background'], chrome:['#sidepanel','background'],
  hover:['.act-live:hover','background'], control:['.act','background'], input:['.setting-row select','background'],
  tabInk:['.tab','color'], activeInk:['.tab-active','color'], chart:['#map','background-color'], chartInk:['.maplabel','color'],
  titleBackground:['#title-screen','background'], titleDialog:['#title-dialog','background'],
  titleBorder:['#title-dialog','border-color'], titleInk:['#title-dialog h2','color'],
  titleMenu:['#title-actions button','background'], titleMenuInk:['#title-actions button','color'],
  titleMenuBorder:['#title-actions button','border-right-color'], titleMenuHover:['#title-actions button:hover','background']
};
// Compile the actual native palette and style table, not a duplicate JS model.
const build = path.join(root, 'build', 'first2000-probe');
fs.mkdirSync(build, {recursive:true});
const cpp = `#include <iostream>\n#include <iomanip>\n#include "ui/Theme.h"\n#include "ui/First2000Styles.h"\nint main() {\nfor (const char* id : {"void","aurora","nebula","ember"}) { auto p = ship::Theme::ById(id).palette;\n${Object.keys(roles).map(role => `std::cout << id << "\\t${role}\\t#" << std::hex << std::setfill('0') << std::setw(2) << p.${role}.r << std::setw(2) << p.${role}.g << std::setw(2) << p.${role}.b << "\\n";`).join('\n')}
}\nstd::cout << std::dec;\nfor (const auto& s : ship::kFirst2000SceneStyles) std::cout << "scene\\t" << s.cls << "\\t" << s.background << "\\t" << s.border << "\\t" << s.ink << "\\t" << s.width << "\\t" << s.sides << "\\t" << s.radius << "\\t" << s.dashed << "\\t" << s.align << "\\t" << s.fontPx << "\\t" << s.mono << "\\n";\n}\n`;
fs.writeFileSync(path.join(build,'probe.cpp'), cpp);
const executable = path.join(build, process.platform === 'win32' ? 'probe.exe' : 'probe');
cp.execFileSync(process.env.CXX || 'c++', ['-std=c++17','-Icpp',path.join(build,'probe.cpp'),'cpp/ui/Theme.cpp','-o',executable], {stdio:'inherit'});
let colours = 0, styles = 0;
const expectedStyles = [];
for (const rule of rules) {
  if (rule.selectors.length !== 1 || !/^\.(mach|o2|mess|cq)-[\w-]+$/.test(rule.selectors[0])) continue;
  const p = rule.props;
  const bg = /^#[0-9a-f]{6}$/.test(p.background || '') ? p.background : '';
  let border = '', width = 0, sides = 0, dashed = 0;
  for (const [key, mask] of [['border',15],['border-top',1],['border-right',2],['border-bottom',4],['border-left',8]]) {
    const match = /^(\d+)px (solid|dashed) (#[0-9a-f]{6})$/.exec(p[key] || '');
    if (match) { width = +match[1]; dashed = +(match[2] === 'dashed'); border = match[3]; sides = mask; }
  }
  if (p['border-color']) border = p['border-color'];
  const radius = p['border-radius'] === '50%' ? -1 : /^\d+px$/.test(p['border-radius'] || '') ? parseInt(p['border-radius']) : 0;
  const align = ({center:1,right:2})[p['text-align']] || 0;
  const font = /^\d+px$/.test(p['font-size'] || '') ? parseInt(p['font-size']) : 0;
  const mono = +(p['font-family'] || '').includes('Consolas');
  if (bg || p.color || border || radius || align || font || mono) expectedStyles.push([rule.selectors[0].slice(1),bg,border,p.color || '',width,sides,radius,dashed,align,font,mono].map(String));
}
const actualStyles = [];
for (const line of cp.execFileSync(executable, {encoding:'utf8'}).trimEnd().split('\n')) {
  const [id, key, ...values] = line.split('\t');
  if (id === 'scene') { actualStyles.push([key,...values]); styles++; continue; }
  const [selector, prop] = roles[key];
  assert.equal(values[0], cssValue(`body.theme-${id}${selector ? ' ' + selector : ''}`,prop), `${id}.${key}`);
  colours++;
}
assert.deepEqual(actualStyles, expectedStyles, 'all bounded scene style declarations in source order');
console.log(`PASS: HTA lines 1–2000 only: ${declarations} data declarations plus ${mapFields} MAP fields, byte-identical title JPEG, ${colours} compiled palette roles, ${styles} compiled scene rules. No claim of full visual parity.`);
