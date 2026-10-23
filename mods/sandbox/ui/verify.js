/* Run with node mods/sandbox/ui/verify.js. Exercises the real controller UI
 * against a tiny DOM double; no game or WebView needs to be launched. */
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
class Element {
  constructor(tag) { this.tagName = tag; this.children = []; this.handlers = {}; this.attributes = {}; this.style = {}; this.scrollTop = 0; this.clientHeight = 495; this.offsetTop = 0; this.offsetHeight = 70; this.textContent = ''; this.classes = new Set(); this.classList = { add: x => this.classes.add(x), remove: x => this.classes.delete(x) }; }
  appendChild(child) { child.offsetTop = this.children.length * 70; child.parentElement = this; this.children.push(child); return child; }
  replaceChildren(...children) { this.children = children; }
  setAttribute(key, value) { this.attributes[key] = value; }
  getAttribute(key) { return this.attributes[key]; }
  addEventListener(key, callback) { this.handlers[key] = callback; }
  contains(child) { return this.children.includes(child) || this.children.some(c => c.contains(child)); }
  closest(tag) { return this.tagName === tag ? this : this.parentElement && this.parentElement.closest(tag); }
  click() { if (this.handlers.click) this.handlers.click(); if (this.onclick) this.onclick(); }
  dispatchEvent(event) { if (this.handlers[event.type]) this.handlers[event.type](event); }
  scrollIntoView() { this.scrolled = true; }
}
const elements = new Map();
const sent = [], events = {}; let hitTarget = null, lastHit = null;
const window = {
  document: { getElementById(id) { if (!elements.has(id)) elements.set(id, new Element('div')); return elements.get(id); }, createElement(tag) { return new Element(tag); }, elementFromPoint(x,y) { lastHit = [x,y]; return hitTarget; } },
  innerWidth: 1920, innerHeight: 1080,
  addEventListener(key, callback) { events[key] = callback; },
  chrome: { webview: { postMessage(text) { sent.push(JSON.parse(text)); }, addEventListener(key, callback) { events['webview:' + key] = callback; } } },
  MouseEvent: class { constructor(type) { this.type = type; } }
};
vm.runInNewContext(fs.readFileSync(path.join(__dirname, 'sandbox.js'), 'utf8'), { window, console, Math, String, Array, Object, isFinite });
const ui = window.NFL2K5SandboxUI, el = id => elements.get(id);
assert.equal(el('sandbox').hidden, true, 'inactive mode must draw nothing');
ui.render({ active: true, menuOpen: true, tab: 0, selection: 1, paused: true, style: 'playstation', rows: [
  { id: 12, label: 'Player tools', enabled: false, reason: 'Needs an active player' },
  { id: 33, label: 'Pause', enabled: true, command: 'toggle_pause' }
] });
assert.equal(el('browser').hidden, false);
assert.equal(el('rows').children[1].className, 'selected');
assert.equal(el('item-title').textContent, 'Pause');
assert.equal(el('activate').disabled, false);
el('activate').onclick();
assert.deepEqual(sent.pop(), { cmd: 'toggle_pause', value: 33, generation: 0 });
assert.equal(el('prompts').children[0].children[0].textContent, '×');
ui.render({ active: true, menuOpen: true, tab: 0, selection: 0, rows: [{ label: 'Unsupported tool', enabled: false, reason: 'Unavailable' }] });
assert.equal(el('activate').disabled, true, 'unsupported tools must not pretend to work');
assert.equal(el('capability-note').hidden, false);
const n = sent.length; el('activate').onclick(); assert.equal(sent.length, n);
ui.render({ active: true, menuOpen: true, tab: 1, selection: 0, rows: [{ label: 'First', enabled: true }, { label: 'Second', enabled: true }] });
el('rows').children[1].handlers.click();
assert.deepEqual(sent.pop(), { cmd: 'navigate', value: 1, generation: 0 });
el('tabs').children[3].handlers.click();
assert.deepEqual(sent.pop(), { cmd: 'tab', value: 2, generation: 0 });
events.keydown({ key: 'ArrowLeft', preventDefault() {} });
assert.deepEqual(sent.pop(), { cmd: 'adjust', value: -1, generation: 0 });
ui.render({ active: true, menuOpen: true, generation: 7, rows: [{ label: 'Step', enabled: true, command: 'step' }] });
el('activate').onclick();
assert.equal(sent.pop().generation, 7, 'page actions must carry the scene generation');
ui.render({ active: true, menuOpen: false, paused: false, speed: 25, rows: [] });
assert.equal(el('browser').hidden, true); assert.equal(el('quick-status').hidden, false);
assert.equal(el('quick-sim').textContent, '0.25× SPEED');
assert.equal(el('quick-prompt').textContent.includes('LB Spawn'), false, 'do not advertise unimplemented spawn shortcuts');
assert.equal(el('quick-prompt').textContent.includes('LB/RB Zoom'), true, 'free-camera hints must expose the implemented bumper zoom control');
ui.render({ active: true, menuOpen: false, paused: false, speed: 100, rows: [] });
assert.equal(el('quick-sim').textContent, 'PLAYING');
ui.render({ active: true, menuOpen: false, reticle: false, rows: [] });
assert.equal(el('quick-prompt').textContent.includes('Zoom'), false, 'normal play must not advertise free-camera controls');
ui.render({ active: true, menuOpen: false, paused: true, grabMode: true, selectedPlayer: 'Test Player', style: 'xbox' });
assert.equal(el('mode-label').textContent, 'PHOTO POSING');
assert.equal(el('quick-sim').textContent, 'Test Player · PAUSED');
assert.equal(el('quick-prompt').textContent.includes('A Drop · B Cancel'), true);
assert.equal(el('quick-prompt').textContent.includes('Zoom'), false, 'posing controls must replace camera controls');
ui.render({ active: true, menuOpen: false, paused: true, grabMode: true, selectedPlayer: 'Test Player', style: 'playstation' });
assert.equal(el('quick-prompt').textContent.includes('× Drop · ○ Cancel'), true);
ui.render({ active: true, photoMode: true }); assert.equal(el('sandbox').hidden, true);
ui.render({ active: true, phase: 'setup', stadiums: [{ id: 4, name: 'Stadium', enabled: true, command: 'stadium' }] });
assert.equal(el('title').textContent, 'CHOOSE A STADIUM');
assert.equal(el('activate').textContent, 'Load stadium');
const badCount = sent.length;
ui.send('../../bad', 0); ui.send('select', Infinity); ui.send('select', 2000000);
assert.equal(sent.length, badCount, 'invalid values must be rejected instead of becoming another valid command');
assert.equal(sent.filter(x => x.cmd === '../../bad').length, 0);
assert.equal(sent.filter(x => x.value === 2000000).length, 0);
assert.equal(ui.rows({ rows: [{ label: '<img src=x onerror=alert(1)>', enabled: true }] })[0].label, '<img src=x onerror=alert(1)>');
ui.render({ active: true, menuOpen: true, generation: 9, rows: [{ label: 'Clock', value: '1:25', enabled: true, adjustable: true, command: 'clock', commandValue: 5 }] });
assert.equal(el('decrease').disabled, false, 'relative adjustment must support formatted clock values');
assert.equal(el('activate').textContent, 'Next value');
el('decrease').onclick(); assert.deepEqual(sent.pop(), { cmd: 'adjust', value: -1, generation: 9 });
el('increase').onclick(); assert.deepEqual(sent.pop(), { cmd: 'adjust', value: 1, generation: 9 });
assert.equal(el('value').textContent, '1:25');
ui.render({ active: true, menuOpen: true, generation: 9, rows: [{ label: 'Old numeric row', value: '6', enabled: true, command: 'timescale', min: 0, max: 10, step: 2 }] });
el('increase').onclick(); assert.deepEqual(sent.pop(), { cmd: 'timescale', value: 8, generation: 9 }, 'explicit older numeric rows retain absolute behavior');
ui.render({ active: true, menuOpen: true, generation: 9, rows: [{ label: 'Unavailable numeric row', value: '25%', enabled: false, adjustable: true, command: 'speed' }] });
const beforeAdjust = sent.length;
assert.equal(el('decrease').disabled, true); el('decrease').onclick(); assert.equal(sent.length, beforeAdjust);
ui.render({ active: true, menuOpen: true, generation: 12, rows: [{ label: 'First', enabled: true, command: 'step' }, { label: 'Second', enabled: false, command: 'step' }] });
el('browser').children = [el('rows'), el('activate')];
hitTarget = el('rows').children[0];
let before = sent.length;
assert.equal(ui.pointer({ generation: 11, sequence: 1, visible: true, x: 100, y: 100, pressed: 1 }), false);
assert.equal(sent.length, before, 'stale scene must not execute pointer commands');
assert.equal(ui.pointer({ generation: 12, sequence: 1, visible: true, x: 100, y: 100, pressed: 1 }), true);
assert.deepEqual(sent.pop(), { cmd: 'navigate', value: 0, generation: 12 });
assert.equal(ui.pointer({ generation: 12, sequence: 1, visible: true, x: 100, y: 100, pressed: 1 }), false, 'duplicate pointer events must be dropped');
ui.pointer({ generation: 12, sequence: 2, visible: true, x: 100, y: 100, pressed: 1, doubleClick: true });
assert.deepEqual(sent.pop(), { cmd: 'step', value: 0, generation: 12 });
hitTarget = el('rows').children[1]; before = sent.length;
ui.pointer({ generation: 12, sequence: 3, visible: true, x: 100, y: 200, pressed: 1 });
assert.deepEqual(sent.pop(), { cmd: 'navigate', value: 1, generation: 12 }, 'unavailable rows can be inspected without activating them');
ui.pointer({ generation: 12, sequence: 4, visible: true, x: 100, y: 200, pressed: 1, doubleClick: true });
assert.equal(sent.length, before, 'disabled tool must reject native pointer activation');
window.innerWidth = 960; window.innerHeight = 540;
ui.pointer({ generation: 12, sequence: 5, visible: true, x: 200, y: 400, pressed: 0 });
assert.deepEqual(lastHit, [100,200], 'hit tests must use actual scaled browser coordinates');
ui.pointer({ generation: 12, sequence: 6, visible: true, x: 200, y: 400, pressed: 2 });
assert.deepEqual(sent.pop(), { cmd: 'back', value: 0, generation: 12 });
ui.pointer({ generation: 12, sequence: 7, visible: false });
assert.equal(el('mouse-pointer').hidden, true);
ui.render({ active: true, menuOpen: false, generation: 12 }); before = sent.length;
assert.equal(ui.pointer({ generation: 12, sequence: 8, visible: true, x: 100, y: 100, pressed: 1 }), false);
assert.equal(sent.length, before, 'closed browser must not react to page pointer events');
ui.render({ active: true, menuOpen: true, generation: 21, tab: 2, rows: [{ label: 'Bookmark', value: 'A', enabled: true, command: 'store_camera', commandValue: 0 }] });
const stableRow = el('rows').children[0], stableTab = el('tabs').children[2];
for (let frame = 1; frame <= 20; ++frame)
  ui.render({ active: true, menuOpen: true, generation: 21, tab: 2, rows: [{ label: 'Bookmark', value: String(frame), enabled: true, command: 'store_camera', commandValue: 1 }] });
assert.equal(el('rows').children[0], stableRow, 'camera snapshots must preserve actual row nodes for pointer hit testing');
assert.equal(el('tabs').children[2], stableTab, 'unchanged categories must retain their DOM nodes');
stableRow.handlers.dblclick();
assert.deepEqual(sent.pop(), { cmd: 'store_camera', value: 1, generation: 21 }, 'retained callbacks must resolve the current row rather than stale command arguments');
ui.render({ active: true, menuOpen: true, generation: 22, tab: 2, rows: [{ label: 'FOV', value: '60 degrees', enabled: true, command: 'fov', commandValue: 1, adjustable: true }] });
el('browser').children = [el('rows'), el('increase')];
el('increase').tagName = 'button';
hitTarget = el('increase');
ui.pointer({ generation: 22, sequence: 1, visible: true, x: 100, y: 100, pressed: 1 });
assert.deepEqual(sent.pop(), { cmd: 'adjust', value: 1, generation: 22 });
ui.pointer({ generation: 22, sequence: 2, visible: true, x: 100, y: 100, pressed: 1, doubleClick: true });
assert.deepEqual(sent.pop(), { cmd: 'adjust', value: 1, generation: 22 }, 'rapid repeated +/- clicks must each adjust; only catalog rows use double-click activation');
console.log('Sandbox UI passed: visibility, actions, prompts, navigation, generations, pointer scaling/disabled/stale events, and stable DOM/current callbacks over camera snapshots.');
