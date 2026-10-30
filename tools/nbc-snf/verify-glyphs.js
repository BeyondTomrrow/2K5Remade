#!/usr/bin/env node
'use strict';

/* Exercise the shipped outline renderer and data. These checks cover text,
 * ownership of accessible labels and fallback, not font visual fidelity. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const packageDir = path.resolve(process.argv[2] || path.join(__dirname, '../../mods/presentations/NBC_SNF'));
class Element {
  constructor(tag) { this.tagName = tag; this.attributes = {}; this.children = []; this.style = {}; this.text = ''; }
  setAttribute(k, v) { this.attributes[k] = String(v); }
  getAttribute(k) { return Object.hasOwn(this.attributes, k) ? this.attributes[k] : null; }
  removeAttribute(k) { delete this.attributes[k]; }
  appendChild(el) { this.children.push(el); return el; }
  get textContent() { return this.text + this.children.map(el => el.textContent).join(''); }
  set textContent(value) { this.text = String(value); this.children = []; }
}
function harness(options = {}) {
  const sandbox = { document: options.noSvg ? {} : { createElementNS: (_ns, tag) => new Element(tag) } };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  if (!options.noData) vm.runInContext(fs.readFileSync(path.join(packageDir, 'wide-glyph-data.js'), 'utf8'), sandbox);
  vm.runInContext(fs.readFileSync(path.join(packageDir, 'broadcast-glyphs.js'), 'utf8'), sandbox);
  return sandbox;
}
const h = harness();
const spec = { el: '.away .abbr', anchor: 'right', x: 143, top: 32, cap: 21, ref: ['LAR', 75] };
let passed = 0;
function check(name, fn) { fn(); passed++; console.log('PASS ' + name); }
function render(text, currentSpec = spec, state = h) {
  const el = new Element('div'); el.textContent = text;
  assert.equal(state.SNFBroadcastGlyphs.render(el, currentSpec, text), true);
  assert.equal(el.textContent, text, 'The outlines retain exactly one copy of the requested text');
  assert.equal(el.getAttribute('role'), 'img'); assert.equal(el.getAttribute('aria-label'), text);
  assert.equal(el.children.length, 1); assert.equal(el.children[0].getAttribute('aria-hidden'), 'true');
  assert.equal(el.children[0].children[0].tagName, 'title');
  assert(Number.isFinite(parseFloat(el.style.left))); assert(Number.isFinite(parseFloat(el.style.top)));
  assert(parseFloat(el.style.width) > 0); assert(parseFloat(el.style.height) > 0);
  return el;
}
const teams = 'ARI ATL BAL BUF CAR CHI CIN CLE DAL DEN DET GB HOU IND JAX KC LV LAC LAR MIA MIN NE NO NYG NYJ PHI PIT SF SEA TB TEN WAS'.split(' ');
for (const abbreviation of teams) check('NFL abbreviation ' + abbreviation, () => render(abbreviation));
for (const text of ['AWY', 'HOM', '---', 'STL', 'SD', 'OAK', 'JAC', 'WSH']) check('Fallback/legacy abbreviation ' + text, () => render(text));
for (const digit of '0123456789') check('Supplied digit ' + digit, () => render(digit));
check('All supplied paths are nonempty except the advance-only space', () => {
  for (const [text, glyph] of Object.entries(h.SNFWideGlyphData)) {
    assert(Number.isFinite(glyph.advance)); assert(glyph.advance > 0);
    if (text === ' ') { assert.equal(glyph.bounds, null); continue; }
    assert(glyph.path.length > 0); assert.equal(glyph.bounds.length, 4);
    assert(glyph.bounds[2] > glyph.bounds[0]); assert(glyph.bounds[3] > glyph.bounds[1]);
  }
});
check('Changing abbreviation replaces paths, title and accessible name', () => {
  const el = render('LAR'); const previousSvg = el.children[0];
  assert.equal(h.SNFBroadcastGlyphs.render(el, spec, 'CIN'), true);
  assert.notEqual(el.children[0], previousSvg); assert.equal(el.children.length, 1);
  assert.equal(el.textContent, 'CIN'); assert.equal(el.getAttribute('aria-label'), 'CIN');
  assert.equal(el.children[0].children.length, 4);
  for (let i = 0; i < 4; i++) h.SNFBroadcastGlyphs.render(el, spec, el.textContent);
  assert.equal(el.textContent, 'CIN'); assert.equal(el.children.length, 1);
});
check('Paths follow currentColor without baking a team color into their fill', () => {
  const el = render('SEA');
  for (const glyph of el.children[0].children.slice(1)) {
    assert.equal(glyph.getAttribute('fill'), 'currentColor'); assert.equal(glyph.getAttribute('stroke'), 'currentColor');
    assert.equal(glyph.getAttribute('vector-effect'), 'non-scaling-stroke');
  }
  el.style.color = '#ff0000'; h.SNFBroadcastGlyphs.render(el, spec, 'SEA');
  assert.equal(el.style.color, '#ff0000');
  assert.equal(el.children[0].children[1].getAttribute('fill'), 'currentColor');
});
check('All anchor modes preserve the fitted reference ink width', () => {
  for (const anchor of ['left', 'center', 'right']) {
    const el = render('LAR', { ...spec, anchor }); assert.equal(parseFloat(el.style.width), 75);
    assert.equal(parseFloat(el.style.left), anchor === 'left' ? 143 : anchor === 'center' ? 105.5 : 68);
  }
});
check('Player names and clock remain ordinary text in their original roles', () => {
  for (const [text, selector] of [['MATTHEW', '.drive-player .first-name'], ['STAFFORD', '.drive-player .last-name'], ["D’ANDRE SWIFT", '.pg-player-name'], ['15:00', '.pod .clock']]) {
    const el = new Element('div'); el.textContent = text;
    assert.equal(h.SNFBroadcastGlyphs.render(el, { ...spec, el: selector }, text), false);
    assert.equal(el.textContent, text); assert.equal(el.children.length, 0); assert.equal(el.getAttribute('role'), null);
  }
});
check('Unsupported or empty replacement removes stale image semantics', () => {
  for (const text of ['É', "D'ANDRE", '', ' ']) {
    const el = render('LAR');
    assert.equal(h.SNFBroadcastGlyphs.render(el, spec, text), false);
    assert.equal(el.textContent, text); assert.equal(el.children.length, 0);
    assert.equal(el.getAttribute('aria-label'), null); assert.equal(el.getAttribute('role'), null);
    assert.equal(el.getAttribute('data-snf-glyphs'), null); assert.equal(el.style.width, ''); assert.equal(el.style.height, '');
  }
});
check('Unavailable SVG API or glyph data leaves the requested text intact', () => {
  for (const state of [harness({ noSvg: true }), harness({ noData: true })]) {
    const el = new Element('div'); el.textContent = 'LAR';
    assert.equal(state.SNFBroadcastGlyphs.render(el, spec, 'LAR'), false);
    assert.equal(el.textContent, 'LAR'); assert.equal(el.getAttribute('role'), null);
  }
});
check('Unsupported reference has a plain text fallback', () => {
  const el = render('LAR');
  assert.equal(h.SNFBroadcastGlyphs.render(el, { ...spec, ref: ['É', 75] }, 'CIN'), false);
  assert.equal(el.textContent, 'CIN'); assert.equal(el.getAttribute('aria-label'), null);
});
console.log(passed + '/' + passed + ' outline renderer checks passed.');
