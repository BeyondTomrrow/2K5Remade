#!/usr/bin/env node
'use strict';

/* Static presentation lifecycle checks. This loads the shipped HTML/runtime/JS
 * in a small DOM with a virtual clock; it never builds or starts NFL2K5.exe.
 * It checks state/event behavior, not browser layout or visual fidelity.
 * Usage: node tools/nbc-snf/verify-package.js [package-directory]
 */
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const packageDir = path.resolve(process.argv[2] || path.join(__dirname, '../../mods/presentations/NBC_SNF'));
const html = fs.readFileSync(path.join(packageDir, 'scorebug.html'), 'utf8');
const css = ['scorebug.css', 'extra-inserts.css', 'broadcast-scenes.css', 'pregame-inserts.css', 'layout.css'].map(f => fs.readFileSync(path.join(packageDir, f), 'utf8')).join('\n');
const runtime = fs.readFileSync(path.join(packageDir, '../_runtime/nfl2k5-presentation.js'), 'utf8');
const script = fs.readFileSync(path.join(packageDir, 'scorebug.js'), 'utf8');
const extraScript = fs.readFileSync(path.join(packageDir, 'extra-inserts.js'), 'utf8');
const broadcastScript = fs.readFileSync(path.join(packageDir, 'broadcast-scenes.js'), 'utf8');
const pregameScript = fs.readFileSync(path.join(packageDir, 'pregame-inserts.js'), 'utf8');

class Style {
  constructor() { this.properties = {}; }
  setProperty(k, v) { this.properties[k] = String(v); }
  removeProperty(k) { delete this.properties[k]; }
  getPropertyValue(k) { return this.properties[k] || ''; }
}
class Element {
  constructor(tag = 'div', attrs = {}) {
    this.tagName = tag.toLowerCase(); this.attributes = attrs; this.children = []; this.parentElement = null;
    this.style = new Style(); this._text = ''; this.offsetWidth = 1;
    this.classList = {
      contains: c => this.className.split(/\s+/).includes(c),
      add: (...cs) => { this.className = [...new Set(this.className.split(/\s+/).filter(Boolean).concat(cs))].join(' '); },
      remove: (...cs) => { this.className = this.className.split(/\s+/).filter(c => c && !cs.includes(c)).join(' '); },
      toggle: (c, force) => { const on = force === undefined ? !this.classList.contains(c) : !!force; this.classList[on ? 'add' : 'remove'](c); return on; }
    };
  }
  get className() { return this.attributes.class || ''; }
  set className(v) { this.attributes.class = String(v); }
  get id() { return this.attributes.id || ''; }
  set id(v) { this.attributes.id = String(v); }
  get hidden() { return Object.hasOwn(this.attributes, 'hidden'); }
  set hidden(v) { if (v) this.attributes.hidden = ''; else delete this.attributes.hidden; }
  get src() { return this.attributes.src || ''; }
  set src(v) { this.attributes.src = String(v); }
  get textContent() { return this._text + this.children.map(c => c.textContent).join(''); }
  set textContent(v) { this._text = String(v); this.children = []; }
  get innerHTML() { return this._text; }
  set innerHTML(v) { this.children = []; this._text = ''; parseInto(String(v), this); }
  appendChild(el) { el.parentElement = this; this.children.push(el); return el; }
  removeAttribute(k) { delete this.attributes[k]; }
  setAttribute(k, v) { this.attributes[k] = String(v); }
  getAttribute(k) { return Object.hasOwn(this.attributes, k) ? this.attributes[k] : null; }
  querySelector(s) { return this.querySelectorAll(s)[0] || null; }
  querySelectorAll(s) { return descendants(this).filter(el => matches(el, s)); }
  getContext() { return { font: '', measureText: t => ({ actualBoundingBoxAscent: 70, actualBoundingBoxLeft: 0, actualBoundingBoxRight: Math.max(1, String(t).length * 55), fontBoundingBoxAscent: 80, fontBoundingBoxDescent: 20 }) }; }
}
function parseInto(markup, parent) {
  const stack = [parent], token = /<!--[\s\S]*?-->|<![^>]*>|<\/?[\w-]+\b[^>]*>|[^<]+/g;
  for (const m of markup.matchAll(token)) {
    const t = m[0];
    if (t.startsWith('<!')) continue;
    if (t.startsWith('</')) { if (stack.length > 1) stack.pop(); continue; }
    if (t.startsWith('<')) {
      const tag = /^<([\w-]+)/.exec(t)[1], attrs = {};
      const a = /([\w:-]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g;
      for (const p of t.slice(tag.length + 1, -1).matchAll(a)) attrs[p[1]] = p[2] ?? p[3] ?? p[4] ?? '';
      const el = stack[stack.length - 1].appendChild(new Element(tag, attrs));
      if (!/^(area|base|br|col|embed|hr|img|input|link|meta|param|source|track|wbr)$/i.test(tag) && !t.endsWith('/>')) stack.push(el);
    } else stack[stack.length - 1]._text += t;
  }
}
function descendants(el) { return el.children.flatMap(c => [c, ...descendants(c)]); }
function simple(el, s) {
  if (!el) return false;
  const not = /:not\(([^)]+)\)/.exec(s); if (not && simple(el, not[1])) return false;
  s = s.replace(/:not\([^)]+\)/g, '');
  const nth = /:nth-child\((\d+)\)/.exec(s); if (nth && (!el.parentElement || el.parentElement.children.indexOf(el) + 1 !== +nth[1])) return false;
  s = s.replace(/:nth-child\([^)]+\)/g, '');
  if (s.includes(':')) return false;
  const tag = /^[\w-]+/.exec(s); if (tag && el.tagName !== tag[0].toLowerCase()) return false;
  for (const m of s.matchAll(/#([\w-]+)/g)) if (el.id !== m[1]) return false;
  for (const m of s.matchAll(/\.([\w-]+)/g)) if (!el.classList.contains(m[1])) return false;
  for (const m of s.matchAll(/\[([\w-]+)(?:=["']?([^\]"']*)["']?)?\]/g)) if (m[2] === undefined ? el.getAttribute(m[1]) === null : el.getAttribute(m[1]) !== m[2]) return false;
  return true;
}
function matches(el, selector) {
  return selector.split(',').some(sel => {
    const bits = sel.trim().replace(/\s*>\s*/g, ' > ').split(/\s+/); let i = bits.length - 1, current = el;
    if (!simple(current, bits[i--])) return false;
    while (i >= 0) {
      if (bits[i] === '>') { i--; current = current.parentElement; if (!simple(current, bits[i--])) return false; }
      else { const b = bits[i--]; current = current.parentElement; while (current && !simple(current, b)) current = current.parentElement; if (!current) return false; }
    }
    return true;
  });
}
const rules = [...css.matchAll(/([^{}]+)\{([^{}]*)\}/g)].filter(m => !m[1].includes('@')).map(m => ({ selector: m[1].replace(/\/\*[\s\S]*?\*\//g, '').trim(), body: m[2] }));
function property(el, key, fallback) {
  let result = fallback, priority = -1;
  for (const rule of rules) for (const sel of rule.selector.split(',')) {
    if (!matches(el, sel)) continue;
    const value = new RegExp('(?:^|;)\\s*' + key + '\\s*:\\s*([^;]+)').exec(rule.body); if (!value) continue;
    const score = (sel.match(/#/g) || []).length * 100 + (sel.match(/[.[:]/g) || []).length * 10;
    if (score >= priority) { result = value[1].replace(/!important/g, '').trim(); priority = score; }
  }
  return el.style[key] || el.style.getPropertyValue(key) || result;
}
function createHarness() {
  const root = new Element('document'); parseInto(html, root);
  let now = 0, nextId = 1; const pending = new Map(), errors = [], hostMessages = [];
  const timer = (fn, delay = 0) => { const id = nextId++; pending.set(id, { fn, at: now + Math.max(0, +delay || 0) }); return id; };
  const documentListeners = new Map();
  const document = {
    documentElement: root.querySelector('html'), body: root.querySelector('body'),
    getElementById: id => root.querySelector('#' + id), querySelector: s => root.querySelector(s), querySelectorAll: s => root.querySelectorAll(s),
    createElement: tag => new Element(tag), createElementNS: (_namespace, tag) => new Element(tag), getAnimations: () => [],
    fonts: { ready: { then: fn => fn() } },
    addEventListener(type,fn) { if(!documentListeners.has(type))documentListeners.set(type,[]);documentListeners.get(type).push(fn); },
    dispatchEvent(event) { for(const fn of documentListeners.get(event.type)||[]) fn(event); }
  };
  class VirtualDate extends Date { static now() { return now; } }
  const sandbox = { document, location: { search: '' }, Date: VirtualDate, performance: { now: () => now },
    console: { log: msg => { if (/handler error|script error/.test(msg)) errors.push(msg); }, warn() {}, error: msg => errors.push(msg) },
    setTimeout: timer, clearTimeout: id => pending.delete(id), requestAnimationFrame: fn => timer(() => fn(now), 16), cancelAnimationFrame: id => pending.delete(id),
    getComputedStyle: el => ({ fontWeight: '900', fontFamily: 'Arial', opacity: property(el, 'opacity', '1') }), URLSearchParams,
    CustomEvent: class { constructor(type,options={}){this.type=type;this.detail=options.detail;} },
    innerWidth: 1920, innerHeight: 1080, addEventListener() {}
  };
  sandbox.window = sandbox; sandbox.parent = sandbox;
  const useHost = (host = 'game', hasWebview = true) => {
    sandbox.NFL2K5_HOST = host;
    if (sandbox.NFL2K5) sandbox.NFL2K5.host = host;
    if (!hasWebview) { delete sandbox.chrome; return; }
    sandbox.chrome = { webview: { addEventListener() {}, postMessage(message) {
      hostMessages.push(message);
      if (/handler error|script error/.test(String(message))) errors.push(message);
    } } };
  };
  vm.createContext(sandbox); vm.runInContext(runtime, sandbox, { filename: '_runtime/nfl2k5-presentation.js' }); vm.runInContext(script, sandbox, { filename: 'NBC_SNF/scorebug.js' });
  vm.runInContext(extraScript, sandbox, { filename: 'NBC_SNF/extra-inserts.js' });
  vm.runInContext(broadcastScript, sandbox, { filename: 'NBC_SNF/broadcast-scenes.js' });
  vm.runInContext(pregameScript, sandbox, { filename: 'NBC_SNF/pregame-inserts.js' });
  const el = id => document.getElementById(id);
  const visible = id => { const node = el(id); if (!node) return false; for (let n = node; n; n = n.parentElement) { if (n.getAttribute('hidden') !== null || property(n, 'display', 'block') === 'none' || property(n, 'visibility', 'visible') === 'hidden' || +property(n, 'opacity', '1') === 0) return false; } return true; };
  const h = { el, visible, errors, useHost, hostMessages, dispatch: msg => sandbox.NFL2K5.dispatch(msg), event: e => sandbox.NFL2K5.dispatch({ type: 'event', event: e }), state: s => sandbox.NFL2K5.dispatch({ type: 'state', state: s }),
    advance: ms => { const stop = now + ms; let count = 0; while (true) { const next = [...pending.entries()].filter(([, t]) => t.at <= stop).sort((a,b) => a[1].at - b[1].at || a[0] - b[0])[0]; if (!next) break; if (++count > 10000) throw Error('Virtual timer loop'); pending.delete(next[0]); now = next[1].at; try { next[1].fn(); } catch (e) { errors.push(e.stack || String(e)); } } now = stop; },
    labels: () => el('qb-stat').querySelectorAll('.qs-fields span').map(c => c.querySelector('b')?.textContent),
    values: () => el('qb-stat').querySelectorAll('.qs-fields span').map(c => c.querySelector('strong')?.textContent),
    clean: () => assert.deepEqual(errors, [], 'Presentation handlers must complete without errors') };
  return h;
}
function live(overrides = {}) {
  return { valid: true, gameStatus: 'in_progress', quarter: 1, gameClock: 600, playClock: 30, periodLength: 900, phase: 4, possession: 'home', downDistanceText: '1ST & 10',
    away: { abbreviation: 'SEA', city: 'Seattle', name: 'Seahawks', score: 0, timeouts: 3 }, home: { abbreviation: 'LAR', city: 'Los Angeles', name: 'Rams', score: 0, timeouts: 3 },
    context: { scorebugVisible: true, playSelection: false, paused: false }, ...overrides };
}
const player = { name: 'M. STAFFORD', fullName: 'Matthew Stafford', position: 'QB', kind: 0, number: 9, portrait: 'https://portraits.local/123.png', completions: 10, attempts: 15, passingYards: 150, passingTouchdowns: 1, interceptions: 0 };
const tests = [];
function test(name, fn) { tests.push({ name, fn }); }

/* Clock typography may reconstruct the DOM one glyph at a time, but the
 * football value, colon and quarter must remain intact through state updates.
 * These assertions intentionally avoid font metrics or layout coordinates. */
function checkClock(h, expected) {
  const clock=h.el('bug').querySelector('.pod .clock');
  assert.equal(clock.textContent,expected,'Clock text must retain the supplied game time');
  const glyphs=clock.querySelectorAll('.numeric-glyph');
  assert.equal(glyphs.map(g=>g.textContent).join(''),expected,'Reconstructed glyphs must contain the entire clock exactly once');
  assert.equal(glyphs.length,Array.from(expected).length,'A time change must replace old glyphs rather than append to them');
  assert.equal(clock.querySelectorAll('.clock-colon').length,expected.includes(':')?1:0,'The clock must have one colon and no duplicated colon carrier');
}

test('Per-glyph clock reconstruction preserves 15:00 and the first-quarter label', h => {
  h.state(live({gameClock:900,quarter:1}));checkClock(h,'15:00');
  assert.equal(h.el('bug').querySelector('.pod .quarter .num').textContent,'1');
  assert.equal(h.el('bug').querySelector('.pod .quarter .ord').textContent,'ST');h.clean();
});

test('Clock changes replace every glyph while quarter labels follow the live state', h => {
  for(const [gameClock,quarter,expected,num,ord] of [[900,1,'15:00','1','ST'],[564,2,'9:24','2','ND'],[59,3,'0:59','3','RD'],[5,4,'0:05','4','TH'],[0,4,'0:00','4','TH'],[601,5,'10:01','OT','']]) {
    h.state(live({gameClock,quarter}));checkClock(h,expected);
    assert.equal(h.el('bug').querySelector('.pod .quarter .num').textContent,num);
    assert.equal(h.el('bug').querySelector('.pod .quarter .ord').textContent,ord);
  }
  h.clean();
});

test('Repeated state and unrelated record changes cannot duplicate clock glyphs or colons', h => {
  h.state(live({gameClock:900}));
  for(const record of ['9-7','10-6','11-5','0-0','']) {
    const state=live({gameClock:900,away:{...live().away,record},home:{...live().home,record}});
    h.state(state);h.state(state);checkClock(h,'15:00');
  }
  h.clean();
});

test('A clock cleared as unavailable removes its glyphs and rebuilds on return', h => {
  h.state(live({gameClock:900}));checkClock(h,'15:00');
  h.state(live({gameClock:null}));checkClock(h,'');
  h.state(live({gameClock:121,quarter:2}));checkClock(h,'2:01');
  assert.equal(h.el('bug').querySelector('.pod .quarter .num').textContent,'2');h.clean();
});

test('Off-air reset and return show the new clock without stale digits or colon nodes', h => {
  h.state(live({gameClock:900}));checkClock(h,'15:00');
  h.state({valid:false,gameStatus:'inactive',context:{scorebugVisible:false}});assert.equal(h.visible('bug'),false);h.advance(2000);
  h.state(live({gameClock:5,quarter:4}));assert.equal(h.visible('bug'),true);checkClock(h,'0:05');
  assert.equal(h.el('bug').querySelector('.pod .quarter .num').textContent,'4');assert.equal(h.el('bug').querySelector('.pod .quarter .ord').textContent,'TH');
  h.state(live({gameClock:65,quarter:4,context:{scorebugVisible:false,playSelection:true}}));assert.equal(h.visible('bug'),false);
  h.state(live({gameClock:4,quarter:4}));assert.equal(h.visible('bug'),true);checkClock(h,'0:04');h.clean();
});
test('Initial page is off air before any game data', h => { assert.equal(h.visible('bug'), false); h.clean(); });
test('Valid live broadcast state reveals the scorebug', h => { h.state(live()); h.advance(20); assert.equal(h.visible('bug'), true); h.clean(); });
test('Invalid state without team objects still hides the scorebug', h => { h.state(live()); h.state({ valid: false, gameStatus: 'inactive', context: { scorebugVisible: false } }); assert.equal(h.visible('bug'), false); h.clean(); });
test('Invalid state cancels a scoring sequence and its delayed callbacks', h => {
  h.state(live({ home: { ...live().home, score: 6 } })); h.event({ name: 'SCORE_CHANGED', team: 'home', points: 6, score: 6 }); h.event({ name: 'TOUCHDOWN', team: 'home', drive: { plays: 7, yards: 75, timeOfPossession: 200 } }); h.advance(500);
  h.state(live({ valid: false, gameStatus: 'inactive', context: { scorebugVisible: false } }));
  assert.equal(h.visible('bug'), false, 'Invalid game must hide the bug immediately');
  for (const c of ['split','wordphase','compact','stats']) assert.equal(h.el('bug').classList.contains(c), false, 'Canceled scoring must clear ' + c);
  h.advance(12000); assert.equal(h.visible('bug'), false, 'Old scoring timer must not expose inactive game'); h.clean();
});
test('Play-selection/menu state hides the scorebug during scoring', h => {
  h.state(live()); h.event({ name: 'TOUCHDOWN', team: 'home' }); h.advance(500); h.state(live({ context: { scorebugVisible: false, playSelection: true } })); assert.equal(h.visible('bug'), false); h.advance(12000); assert.equal(h.visible('bug'), false); h.clean();
});
test('Feature timeout does not restore an expired compact stat', h => {
  h.state(live()); h.event({ name: 'PLAYER_STAT', team: 'home', player }); h.advance(1000); h.event({ name: 'PLAYER_FEATURE', team: 'home', player }); h.advance(7000); assert.equal(h.visible('qb-stat'), false); h.clean();
});
test('Delayed lineup exit does not expose the menu', h => {
  h.state(live()); h.event({ name: 'LINEUP', team: 'home', players: [{ name: 'Matthew Stafford', position: 'QB', number: 9 }] }); h.advance(2700);
  h.state(live({ valid: false, context: { scorebugVisible: false } })); h.advance(1000); assert.equal(h.visible('bug'), false); assert.equal(h.visible('lineup'), false); h.clean();
});
for (const [kind, first] of [[0,/^COMP(?:\/ATT)?$/], [1,/^CAR(?:RIES)?$/], [2,/^REC(?:EPTIONS)?$/], [3,/^(?:TKL|TACKLES)$/], [4,/^(?:FG|FIELD GOALS)$/]]) test('Zero-stat numeric kind ' + kind + ' chooses its correct fields', h => {
  h.state(live()); h.event({ name: 'PLAYER_STAT', team: 'home', player: { name: 'Test Player', kind } }); assert.match(h.labels()[0] || '', first); h.clean();
});
test('Receiver data never adopts a stale quarterback identity', h => {
  h.state(live()); h.event({ name: 'PLAYER_STAT', team: 'home', player: { ...player, name: 'T. ATWELL', kind: 2, receptions: 4, receivingYards: 93, receivingTouchdowns: 1 } });
  assert.equal(h.labels()[0], 'REC', 'Explicit receiving family must beat stale QB position/passing values');
  assert.equal(h.el('qb-stat').querySelector('.qs-name').textContent.includes('STAFFORD'), false, 'Mismatched cached QB full name must not be used');
  assert.notEqual(h.el('qb-stat').querySelector('.qs-portrait').src, player.portrait, 'Mismatched cached QB portrait must not be used'); h.clean();
});
test('A live game-stat payload is not labeled THIS SEASON', h => { h.state(live()); h.event({ name: 'PLAYER_STAT', team: 'home', player }); assert.notEqual(h.el('qb-stat').querySelector('.qs-season').textContent, 'THIS SEASON'); h.clean(); });
test('A game-stat feature does not invent playoff or two-season context', h => { h.state(live()); h.event({ name: 'PLAYER_FEATURE', team: 'home', player }); assert.doesNotMatch(h.el('player-feature').textContent, /PLAYOFF MAGIC|OVER THE LAST TWO SEASONS/i); h.clean(); });
test('Jersey number zero is retained', h => { h.state(live()); h.event({ name: 'PLAYER_STAT', team: 'home', player: { name: 'Test Receiver', position: 'WR', kind: 2, number: 0 } }); assert.match(h.el('qb-stat').querySelector('.qs-position').textContent, /(?:^|\s)0(?:$|\s)/); h.clean(); });
test('A player-stat event without player data cannot reveal a stale card', h => { h.state(live()); h.event({ name: 'PLAYER_STAT', team: 'home' }); assert.equal(h.visible('qb-stat'), false); h.clean(); });
test('Off-air player-stat events cannot reappear on the next live state', h => { h.state(live({ valid: false, context: { scorebugVisible: false } })); h.event({ name: 'PLAYER_STAT', team: 'home', player }); h.state(live()); assert.equal(h.visible('qb-stat'), false); h.clean(); });
test('Drive stat folds out and honors its supplied duration', h => {
  h.state(live()); h.event({ name: 'PLAYER_STAT', team: 'home', placement: 'scorebug', scope: 'drive', duration: 2, player }); h.advance(1200);
  assert.equal(h.el('bug').classList.contains('player-drive'), true); assert.equal(h.el('bug').classList.contains('stats'), true);
  assert.equal(h.el('bug').querySelector('.drive-bar .drive-title').textContent, 'THIS DRIVE'); assert.equal(h.visible('qb-stat'), false);
  h.advance(1900); assert.equal(h.el('bug').classList.contains('player-drive'), false); assert.equal(h.el('bug').classList.contains('split'), false); h.clean();
});
test('Off-air reset cancels drive-stat animation and queued scoring', h => {
  h.state(live()); h.event({ name: 'PLAYER_STAT', team: 'home', scope: 'drive', player }); h.advance(1200); h.event({ name: 'TOUCHDOWN', team: 'home' });
  h.state({ valid: false, context: { scorebugVisible: false } }); h.advance(20000);
  assert.equal(h.visible('bug'), false); for (const c of ['player-drive','split','compact','stats']) assert.equal(h.el('bug').classList.contains(c), false); h.clean();
});
test('Replay wipe hides on schedule and survives a repeated replay event', h => {
  h.state(live()); h.event({ name: 'REPLAY_STARTED' }); assert.equal(h.visible('snf-wipe'), true); h.advance(600); h.event({ name: 'REPLAY_ENDED' }); h.advance(600);
  assert.equal(h.visible('snf-wipe'), true, 'Second wipe owns its own completion timer'); h.advance(600); assert.equal(h.visible('snf-wipe'), false); h.clean();
});
test('Inactive state cancels replay wipe and its delayed completion', h => {
  h.state(live()); h.event({ name: 'REPLAY_STARTED' }); h.advance(200); h.state({ valid: false, context: { scorebugVisible: false } }); assert.equal(h.visible('snf-wipe'), false); h.advance(2000); assert.equal(h.visible('snf-wipe'), false); assert.equal(h.visible('bug'), false); h.clean();
});
test('Replay transition works while the native scorebug is hidden', h => {
  const hidden = live({ context: { scorebugVisible: false, playSelection: false, paused: false } });
  h.state(hidden); h.event({ name: 'REPLAY_STARTED' });
  assert.equal(h.visible('bug'), false); assert.equal(h.visible('snf-wipe'), true);
  h.advance(500); h.state(hidden); assert.equal(h.visible('snf-wipe'), true, 'A state refresh must not interrupt the replay wipe');
  h.advance(700); assert.equal(h.visible('snf-wipe'), false); h.clean();
});
test('Replay events cannot draw over a paused game or play selection', h => {
  for (const context of [{scorebugVisible:false,paused:true},{scorebugVisible:false,playSelection:true}]) {
    h.state(live({context})); h.event({name:'REPLAY_STARTED'}); assert.equal(h.visible('snf-wipe'),false);
  }
  h.clean();
});
const comparisonEvent = {name:'PLAYER_COMPARISON',duration:2,title:'Comparison',players:[{name:'Player A',position:'WR',number:0},{name:'Player B',position:'RB',number:12}],rows:[{label:'YARDS',values:[0,125]}]};
test('Comparison owns its identity, preserves zero and restores the scorebug', h => {
  h.state(live());h.event(comparisonEvent);
  assert.equal(h.visible('snf-comparison'),true);assert.equal(h.visible('bug'),false);
  assert.match(h.el('snf-comparison').textContent,/WR 0/);assert.match(h.el('snf-comparison').textContent,/125/);
  h.advance(2200);assert.equal(h.visible('snf-comparison'),false);assert.equal(h.visible('bug'),true);h.clean();
});
test('Malformed replacement clears the previous comparison', h => {
  h.state(live());h.event(comparisonEvent);h.event({name:'PLAYER_COMPARISON',players:comparisonEvent.players,rows:[{label:'BAD',values:[null,Infinity]}]});
  assert.equal(h.visible('snf-comparison'),false);assert.equal(h.visible('bug'),true);h.clean();
});
test('Comparison can continue through hidden native scorebug but cannot survive a menu', h => {
  h.state(live());h.event(comparisonEvent);h.advance(500);h.state(live({context:{scorebugVisible:false}}));
  assert.equal(h.visible('snf-comparison'),true);assert.equal(h.visible('bug'),false);
  h.state(live({context:{scorebugVisible:false,playSelection:true}}));assert.equal(h.visible('snf-comparison'),false);h.advance(4000);assert.equal(h.visible('bug'),false);h.clean();
});
test('Team table and normal scorebug coexist; scoring clears the table', h => {
  h.state(live());h.event({name:'TEAM_STATS',teamData:{abbreviation:'LAR'},columns:['REC','YDS'],rows:[{label:'Receiver',values:[0,0]}]});
  assert.equal(h.visible('snf-team-stats'),true);assert.equal(h.visible('bug'),true);
  h.event({name:'TOUCHDOWN',team:'home'});assert.equal(h.visible('snf-team-stats'),false);h.clean();
});
test('Off-air extra events cannot appear on return to gameplay', h => {
  h.state({valid:false,context:{scorebugVisible:false}});h.event(comparisonEvent);h.state(live());assert.equal(h.visible('snf-comparison'),false);h.clean();
});

/* Supplemental scenes share the shipped event runtime with the scorebug.
 * Check ownership and cancellation across those scripts, rather than testing
 * the scene helper in isolation and missing stale cards underneath it. */
const freshMatch = overrides => live({gameClock:900,periodLength:900,...overrides});
const pregame = (stage,overrides={}) => freshMatch({phase:0,gameStatus:'pregame',context:{scorebugVisible:false,playSelection:false,paused:false,pregameStage:stage},...overrides});
const profileEvent = {name:'PLAYER_PROFILE',team:'home',duration:2,player:{name:'Test Center',position:'C',number:0,height:'6\' 3"',weight:'305 LB',age:0,yearsPro:0,college:'Test College',portrait:'https://portraits.local/center.png'}};

test('Corner watermark starts hidden and appears only in an active match', h => {
  assert.equal(h.visible('snf-watermark'),false);assert.equal(h.el('snf-watermark').getAttribute('aria-hidden'),'true');
  h.state(live());assert.equal(h.visible('snf-watermark'),true);assert.equal(h.el('snf-watermark').getAttribute('aria-hidden'),'false');h.clean();
});

test('Corner watermark survives a hidden native scorebug during a live camera', h => {
  h.state(live({context:{scorebugVisible:false,playSelection:false,paused:false}}));
  assert.equal(h.visible('bug'),false);assert.equal(h.visible('snf-watermark'),true);h.clean();
});

test('Corner watermark is hidden in menus, pause and invalid game states', h => {
  for(const state of [live({context:{scorebugVisible:false,playSelection:true}}),live({context:{scorebugVisible:true,paused:true}}),live({gameStatus:'inactive'}),{valid:false,gameStatus:'inactive',context:{scorebugVisible:false}}]) {
    h.state(live());h.state(state);assert.equal(h.visible('snf-watermark'),false);h.advance(2000);assert.equal(h.visible('snf-watermark'),false);
  }
  h.clean();
});

test('Corner branding uses the supplied peacock and survives missing player photos', h => {
  h.state(live());h.event({name:'PLAYER_FEATURE',team:'home',player:{name:'Test Receiver',position:'WR'}});
  assert.equal(h.visible('snf-watermark'),true);assert.equal(h.el('snf-watermark').querySelector('.nbc-brand').src,'assets/branding/nbc-peacock-user.png');
  assert.equal(h.el('snf-watermark').querySelector('.snf-wordmark').getAttribute('alt'),'SNF');
  assert.equal(h.el('snf-watermark').querySelector('.snf-wordmark').src,'assets/branding/snf-wordmark.svg');h.clean();
});

test('Replay briefly owns the screen and then restores corner branding', h => {
  h.state(live());h.event({name:'REPLAY_STARTED'});assert.equal(h.visible('snf-watermark'),false);h.advance(500);h.event({name:'REPLAY_ENDED'});
  h.advance(700);assert.equal(h.visible('snf-watermark'),false);h.advance(500);assert.equal(h.visible('snf-watermark'),true);h.clean();
});

test('Timeout score panel owns both teams, current clock and zero timeout marks', h => {
  h.state(live({quarter:4,gameClock:5,away:{...live().away,score:17,timeouts:0},home:{...live().home,score:35,timeouts:2}}));h.event({name:'TIMEOUT',team:'home',duration:2});
  assert.equal(h.visible('snf-timeout'),true);assert.equal(h.visible('bug'),false);assert.equal(h.visible('snf-watermark'),true);
  const panel=h.el('snf-timeout');assert.equal(panel.querySelector('.timeout-title').textContent,'TIMEOUT');assert.equal(panel.querySelector('.timeout-detail').textContent,'LOS ANGELES RAMS');
  assert.equal(panel.querySelector('.timeout-clock').textContent,'4TH  0:05');
  assert.equal(panel.querySelector('.timeout-team.away .timeout-name').textContent,'SEA');assert.equal(panel.querySelector('.timeout-team.away .timeout-score').textContent,'17');
  assert.equal(panel.querySelector('.timeout-team.home .timeout-score').textContent,'35');
  assert.equal(panel.querySelectorAll('.timeout-team.away .timeout-marks i.used').length,3);assert.equal(panel.querySelectorAll('.timeout-team.home .timeout-marks i.used').length,1);
  h.advance(2100);assert.equal(h.visible('snf-timeout'),false);assert.equal(h.visible('bug'),true);h.clean();
});

test('Timeout score panel follows live score and clock updates', h => {
  h.state(live());h.event({name:'TIMEOUT',team:'away',duration:4});h.advance(500);
  h.state(live({quarter:3,gameClock:189,away:{...live().away,score:10,timeouts:1},home:{...live().home,score:14,timeouts:0}}));
  const panel=h.el('snf-timeout');assert.equal(h.visible('snf-timeout'),true);assert.equal(panel.querySelector('.timeout-clock').textContent,'3RD  3:09');
  assert.equal(panel.querySelector('.timeout-team.away .timeout-score').textContent,'10');assert.equal(panel.querySelector('.timeout-team.home .timeout-score').textContent,'14');
  assert.equal(panel.querySelectorAll('.timeout-team.home .timeout-marks i.used').length,3);h.clean();
});

test('Unknown timeout counts and scores are not fabricated as zero', h => {
  h.state(live({away:{...live().away,score:null,timeouts:null},home:{...live().home,score:0,timeouts:0}}));h.event({name:'TIMEOUT',duration:2});
  assert.equal(h.el('snf-timeout').querySelector('.timeout-team.away .timeout-score').textContent,'\u2014');
  assert.equal(h.el('snf-timeout').querySelectorAll('.timeout-team.away .timeout-marks i').length,0);
  assert.equal(h.el('snf-timeout').querySelector('.timeout-team.home .timeout-score').textContent,'0');h.clean();
});

test('Timeout continues while native bug is hidden and cannot restore it incorrectly', h => {
  h.state(live());h.event({name:'TIMEOUT',duration:1});h.state(live({context:{scorebugVisible:false}}));
  assert.equal(h.visible('snf-timeout'),true);h.advance(1200);assert.equal(h.visible('snf-timeout'),false);assert.equal(h.visible('bug'),false);assert.equal(h.visible('snf-watermark'),true);h.clean();
});

test('Menu and invalid-state transitions cancel timeout and its completion timer', h => {
  for(const state of [live({context:{scorebugVisible:false,playSelection:true}}),{valid:false,context:{scorebugVisible:false}}]) {
    h.state(live());h.event({name:'TIMEOUT',duration:2});h.advance(500);h.state(state);
    assert.equal(h.visible('snf-timeout'),false);h.advance(4000);assert.equal(h.visible('snf-timeout'),false);assert.equal(h.visible('bug'),false);
  }
  h.clean();
});

test('GAME_STARTED waits through crowd shots and shows intro only when warmup begins', h => {
  h.state(pregame('crowd'));h.event({name:'GAME_STARTED',duration:2,venue:'Test Stadium'});
  assert.equal(h.visible('snf-intro'),false);h.advance(10000);assert.equal(h.visible('snf-intro'),false,'A timer must not guess that the crowd shot ended');
  h.state(pregame('warmup'));assert.equal(h.visible('snf-intro'),true);assert.equal(h.visible('bug'),false);assert.equal(h.visible('snf-watermark'),false);
  assert.equal(h.el('snf-intro').querySelector('.intro-team.away .intro-city').textContent,'SEATTLE');
  assert.equal(h.el('snf-intro').querySelector('.intro-team.home .intro-name').textContent,'RAMS');assert.equal(h.el('snf-intro').querySelector('.intro-venue').textContent,'Test Stadium');
  h.advance(2100);assert.equal(h.visible('snf-intro'),false);h.state(live({context:{scorebugVisible:true,pregameStage:'kickoff'}}));assert.equal(h.visible('bug'),true);assert.equal(h.visible('snf-watermark'),true);h.clean();
});

test('An authoritative warmup or panning state can open intro without a startup notification', h => {
  h.state(pregame('panning'));assert.equal(h.visible('snf-intro'),true);h.clean();
});

test('Automatic intro runs once per match despite repeated startup and camera updates', h => {
  h.state(pregame('crowd'));h.event({name:'GAME_STARTED',duration:1});h.state(pregame('warmup'));h.advance(1100);assert.equal(h.visible('snf-intro'),false);
  h.event({name:'GAME_STARTED',duration:1});h.state(pregame('warmup'));h.state(pregame('panning'));assert.equal(h.visible('snf-intro'),false);h.clean();
});

test('Unknown pregame stage, phase or quarter fail closed even at a full game clock', h => {
  for(const state of [pregame(null),pregame('unknown'),pregame('warmup',{phase:null}),pregame('warmup',{phase:undefined}),pregame('panning',{phase:NaN}),pregame('warmup',{quarter:null}),pregame('warmup',{quarter:undefined}),pregame('warmup',{quarter:NaN}),pregame('warmup',{quarter:'1'}),pregame('warmup',{quarter:-1})]) {
    h.state({valid:false,context:{scorebugVisible:false}});h.state(state);h.event({name:'GAME_STARTED'});h.event({name:'MATCHUP_INTRO'});h.advance(10000);assert.equal(h.visible('snf-intro'),false,'Unknown pregame state must remain hidden: stage='+state.context?.pregameStage+', phase='+state.phase+', quarter='+state.quarter);
  }
  h.clean();
});

test('Unknown period length or unavailable clock cannot authorize a matchup intro', h => {
  const bad=[{periodLength:undefined},{periodLength:null},{periodLength:NaN},{periodLength:Infinity},{periodLength:0},{periodLength:-1},{gameClock:undefined},{gameClock:null},{gameClock:NaN},{gameClock:Infinity},{gameClock:-1},{gameClock:901}];
  for(const overrides of bad) {
    h.state({valid:false,context:{scorebugVisible:false}});h.state(pregame('warmup',overrides));h.event({name:'GAME_STARTED'});h.event({name:'BROADCAST_INTRO'});h.advance(10000);
    assert.equal(h.visible('snf-intro'),false,'Intro requires an available full game clock: length='+overrides.periodLength+', clock='+overrides.gameClock);
  }
  h.state({valid:false,context:{scorebugVisible:false}});h.state(pregame('panning',{quarter:0}));assert.equal(h.visible('snf-intro'),true,'Known phase0, quarter0 and full clock remain valid pregame');h.clean();
});

test('Intro cancels older player cards when its warmup camera takes ownership', h => {
  h.state(pregame('crowd',{context:{scorebugVisible:true,pregameStage:'crowd'}}));h.event({name:'PLAYER_FEATURE',team:'home',player,duration:8});
  h.event({name:'GAME_STARTED',duration:1});assert.equal(h.visible('snf-intro'),false);h.state(pregame('warmup'));
  assert.equal(h.visible('snf-intro'),true);assert.equal(h.visible('player-feature'),false,'A prior feature must not remain on top of the matchup intro');
  h.advance(1100);assert.equal(h.visible('player-feature'),false,'Old card completion must not restore a stale feature');h.clean();
});

test('Fresh-clock GAME_STARTED during kickoff cannot open a pregame intro', h => {
  for(const state of [freshMatch({phase:2}),pregame('kickoff'),pregame('warmup',{phase:2}),live(),pregame('warmup',{quarter:3}),pregame('warmup',{gameClock:899})]) {
    h.state({valid:false,context:{scorebugVisible:false}});h.state(state);h.event({name:'GAME_STARTED'});h.event({name:'BROADCAST_INTRO'});assert.equal(h.visible('snf-intro'),false,'Intro must remain hidden: stage='+state.context?.pregameStage+', phase='+state.phase+', quarter='+state.quarter+', clock='+state.gameClock);
  }
  h.clean();
});

test('Explicit intro events remain restricted to the authorized pregame camera', h => {
  h.state(pregame('crowd'));for(const name of ['MATCHUP_INTRO','BROADCAST_INTRO','PREGAME_INTRO_READY']) { h.event({name});assert.equal(h.visible('snf-intro'),false); }
  h.state(pregame('warmup'));assert.equal(h.visible('snf-intro'),true);
  h.state(live());for(const name of ['MATCHUP_INTRO','BROADCAST_INTRO','PREGAME_INTRO_READY']) { h.event({name});assert.equal(h.visible('snf-intro'),false); }h.clean();
});

test('Skipping from crowd to kickoff discards the pending intro permanently for that match', h => {
  h.state(pregame('crowd'));h.event({name:'GAME_STARTED'});h.state(pregame('kickoff'));h.advance(10000);assert.equal(h.visible('snf-intro'),false);
  h.state(pregame('warmup'));h.event({name:'GAME_STARTED'});assert.equal(h.visible('snf-intro'),false,'A late warmup marker must not revive a skipped intro');h.clean();
});

test('Coin-toss stage clears pending and visible matchup intros', h => {
  h.state(pregame('crowd'));h.event({name:'GAME_STARTED'});h.state(pregame('coin_toss'));assert.equal(h.visible('snf-intro'),false);
  h.state(pregame('warmup'));assert.equal(h.visible('snf-intro'),false,'The toss consumes the intro opportunity');
  h.state({valid:false,context:{scorebugVisible:false}});h.state(pregame('warmup'));assert.equal(h.visible('snf-intro'),true);h.state(pregame('coin_toss'));assert.equal(h.visible('snf-intro'),false);h.clean();
});

test('A new matchup starts its own crowd-to-warmup intro sequence', h => {
  h.state(pregame('crowd'));h.event({name:'GAME_STARTED',duration:1});h.state(pregame('warmup'));h.advance(1100);
  const away={...live().away,abbreviation:'DAL',city:'Dallas',name:'Cowboys'};h.state(pregame('crowd',{away}));h.event({name:'GAME_STARTED',duration:1});assert.equal(h.visible('snf-intro'),false);h.state(pregame('panning',{away}));
  assert.equal(h.visible('snf-intro'),true);assert.equal(h.el('snf-intro').querySelector('.intro-team.away .intro-name').textContent,'COWBOYS');h.clean();
});

test('Menu cancels pending or active intro without delayed cards leaking back', h => {
  for(const stage of ['crowd','warmup']) {
    h.state({valid:false,context:{scorebugVisible:false}});h.state(pregame(stage));h.event({name:'GAME_STARTED',duration:2});h.advance(500);
    h.state(pregame(null,{context:{scorebugVisible:false,playSelection:true}}));assert.equal(h.visible('snf-intro'),false);h.advance(5000);assert.equal(h.visible('snf-intro'),false);assert.equal(h.visible('snf-watermark'),false);assert.equal(h.visible('bug'),false);
  }
  h.clean();
});

test('Matchup intro chooses one actual top-rated player per team without quarterback bias', h => {
  const away={...live().away,featuredPlayers:[{fullName:'Away Quarterback',position:'QB',number:7,overall:80,portrait:'https://portraits.local/away-qb.png'},{fullName:'Away Receiver',position:'WR',number:0,overall:97,portrait:'https://portraits.local/away-wr.png'},{fullName:'Unrated Away Star',position:'RB',number:9}]};
  const home={...live().home,featuredPlayers:[{fullName:'Home Runner',position:'RB',number:20,overall:90},{fullName:'Home Kicker',position:'K',number:12,overall:96},{fullName:'Bad Rating',position:'QB',number:1,overall:NaN}]};
  h.state(pregame('warmup',{away,home}));const intro=h.el('snf-intro');
  assert.deepEqual(intro.querySelectorAll('.intro-team.away .intro-player-name').map(n=>n.textContent.toUpperCase()),['AWAY RECEIVER']);
  assert.deepEqual(intro.querySelectorAll('.intro-team.home .intro-player-name').map(n=>n.textContent.toUpperCase()),['HOME KICKER']);
  assert.match(intro.querySelector('.intro-team.away .intro-player-position').textContent,/WR/);assert.match(intro.querySelector('.intro-team.away .intro-player-position').textContent,/(?:^|\D)0(?:\D|$)/);h.clean();
});

test('Equal overall ratings keep roster order and invalid identities never win player selection', h => {
  const away={...live().away,featuredPlayers:[{fullName:'',position:'QB',overall:99},{fullName:'First Tie',position:'WR',number:3,overall:95},{fullName:'Second Tie',position:'QB',number:7,overall:95}]};
  const home={...live().home,featuredPlayers:[{fullName:'Only Valid',position:'TE',overall:0},{fullName:'Infinity Player',position:'WR',overall:Infinity}]};
  h.state(pregame('warmup',{away,home}));
  assert.deepEqual(h.el('snf-intro').querySelectorAll('.intro-team.away .intro-player-name').map(n=>n.textContent.toUpperCase()),['FIRST TIE']);
  assert.deepEqual(h.el('snf-intro').querySelectorAll('.intro-team.home .intro-player-name').map(n=>n.textContent.toUpperCase()),['ONLY VALID']);h.clean();
});

test('Missing player ratings omit featured identities instead of choosing arbitrary roster names', h => {
  h.state(pregame('warmup',{away:{...live().away,featuredPlayers:[{fullName:'Unrated QB',position:'QB'},{fullName:'Unrated Receiver',position:'WR',overall:null}]},home:{...live().home,featuredPlayers:[]}}));
  const intro=h.el('snf-intro');assert.equal(intro.querySelectorAll('.intro-player-name').filter(n=>n.textContent.trim()).length,0);
  assert.doesNotMatch(intro.textContent,/UNRATED QB|UNRATED RECEIVER/i);h.clean();
});

test('A rated player without a portrait keeps its real name without borrowing another face', h => {
  h.state(pregame('warmup',{away:{...live().away,featuredPlayers:[{fullName:'Named Leader',position:'RB',number:22,overall:98},{fullName:'Lower Player',position:'QB',number:7,overall:90,portrait:'https://portraits.local/loser.png'}]}}));
  const team=h.el('snf-intro').querySelector('.intro-team.away');assert.deepEqual(team.querySelectorAll('.intro-player-name').map(n=>n.textContent.toUpperCase()),['NAMED LEADER']);
  assert.equal(team.querySelectorAll('img').filter(n=>!n.classList.contains('intro-logo')&&n.getAttribute('src')).length,0,'A missing portrait must not be guessed or borrowed from a teammate');h.clean();
});

test('Featured players retain their own supplied renders and cannot come from the opposing team', h => {
  h.state(pregame('warmup',{away:{...live().away,featuredPlayers:[{fullName:'Wrong Team Leader',position:'QB',overall:99,teamAbbreviation:'LAR',portrait:'https://portraits.local/wrong.png'},{fullName:'Right Team Leader',position:'WR',overall:94,teamAbbreviation:'SEA',render:'https://renders.local/right.png',portrait:'https://portraits.local/right.png'}]}}));
  const team=h.el('snf-intro').querySelector('.intro-team.away');assert.deepEqual(team.querySelectorAll('.intro-player-name').map(n=>n.textContent.toUpperCase()),['RIGHT TEAM LEADER']);
  assert.equal(team.querySelector('.intro-player-portrait').src,'https://renders.local/right.png');assert.doesNotMatch(team.textContent,/WRONG TEAM LEADER/);h.clean();
});

const factEvent={name:'PLAYER_FACT',team:'home',player:{fullName:'Test Receiver',position:'WR',number:0,portrait:'https://portraits.local/receiver.png'},caption:'CAREER',text:'Exact native fact: 0 touchdowns in this example.'};
const tossEvent={name:'COIN_TOSS',team:'away',source:'native',text:'Seattle will receive.'};

test('The shipped page loads pregame replacement scripts and styles', h => {
  assert.match(html,/<script[^>]+src=["']pregame-inserts\.js["']/);assert.match(html,/<link[^>]+href=["']pregame-inserts\.css["']/);h.clean();
});

test('Native player fact owns exact text, identity and jersey zero during warmups', h => {
  h.state(pregame('warmup'));h.event(factEvent);const card=h.el('snf-player-fact');
  assert.equal(h.visible('snf-player-fact'),true);assert.equal(h.visible('snf-intro'),false);assert.equal(card.querySelector('.pg-player-name').textContent,'Test Receiver');assert.equal(card.querySelector('.pg-position').textContent,'WR\n0');
  assert.equal(card.querySelector('.pg-fact-text').textContent,factEvent.text);assert.equal(card.querySelector('.pg-portrait').src,factEvent.player.portrait);
  h.advance(20000);assert.equal(h.visible('snf-player-fact'),true,'Missing native duration must not invent a five-second timeout');h.event({name:'PLAYER_FACT_HIDE'});h.advance(350);assert.equal(h.visible('snf-player-fact'),false);
  h.state(pregame('panning'));assert.equal(h.visible('snf-intro'),false,'A native card replacement must not reopen the matchup intro');h.clean();
});

test('An explicitly visible native fact may hold in unknown-stage phase-zero pregame without guessing an intro', h => {
  h.state(pregame(null));h.event({...factEvent,source:'native',nativeVisible:true});
  assert.equal(h.visible('snf-player-fact'),true);assert.equal(h.visible('snf-intro'),false);
  assert.equal(h.el('snf-player-fact').querySelector('.pg-fact-text').textContent,factEvent.text);
  h.state(pregame(null));h.advance(20000);assert.equal(h.visible('snf-player-fact'),true,'Owned native visibility must survive state refresh without an invented timeout');
  h.event({name:'PLAYER_FACT_HIDE'});h.advance(350);assert.equal(h.visible('snf-player-fact'),false);h.state(pregame('warmup'));assert.equal(h.visible('snf-intro'),false,'A native fact must not infer or restart the matchup introduction');h.clean();
});

test('Unknown-stage native fact fallback rejects missing ownership, forbidden stages and gameplay', h => {
  for(const event of [{...factEvent,source:'native'},{...factEvent,source:'native',nativeVisible:false},{...factEvent,source:'browser',nativeVisible:true},{...factEvent,native:true,nativeVisible:true}]) {
    h.state(pregame(null));h.event(event);assert.equal(h.visible('snf-player-fact'),false);
  }
  const owned={...factEvent,source:'native',nativeVisible:true};
  for(const state of [pregame('crowd'),pregame('coin_toss'),pregame('kickoff'),pregame(null,{gameStatus:'in_progress'}),pregame(null,{phase:null}),pregame(null,{phase:2})]) {
    h.state(state);h.event(owned);assert.equal(h.visible('snf-player-fact'),false,'An owning native fact must not override explicit camera/football phase restrictions');
  }
  h.state(pregame(null));h.event(owned);assert.equal(h.visible('snf-player-fact'),true);h.state(pregame(null,{phase:2}));assert.equal(h.visible('snf-player-fact'),false);h.clean();
});

test('Pregame fact team data must match its owning side or uniquely match a loaded team', h => {
  h.state(pregame(null));const owned={...factEvent,source:'native',nativeVisible:true,team:undefined,teamData:{abbreviation:'LAR'}};
  h.event(owned);assert.equal(h.visible('snf-player-fact'),true);assert.equal(h.el('snf-player-fact').querySelector('.pg-team-logo').src,'assets/logos/LAR.png');
  for(const event of [{...owned,teamData:{abbreviation:'DAL'}},{...owned,teamData:{}},{...owned,team:'away'},{...owned,team:'home',teamData:{abbreviation:'SEA'}}]) {
    h.event(event);assert.equal(h.visible('snf-player-fact'),false,'Unrelated or conflicting team metadata must clear the old native fact');
  }
  h.state(pregame(null,{home:{...live().home,abbreviation:'SEA'}}));h.event({...owned,teamData:{abbreviation:'SEA'}});assert.equal(h.visible('snf-player-fact'),false,'Ambiguous team-only ownership must not choose one side');h.clean();
});

test('Repeated fact identity still updates native visibility ownership before the next state', h => {
  h.state(pregame('warmup'));h.event(factEvent);h.event({...factEvent,source:'native',nativeVisible:true});h.state(pregame(null));
  assert.equal(h.visible('snf-player-fact'),true,'A native ownership update must not be discarded just because the visible text did not change');
  h.state(pregame('warmup'));h.event({...factEvent,source:'browser',nativeVisible:false});h.state(pregame(null));assert.equal(h.visible('snf-player-fact'),false,'An old native flag must not authorize a later unowned card');h.clean();
});

test('Player fact duration and owning hide cancel its delayed completion correctly', h => {
  h.state(pregame('panning'));h.event({...factEvent,duration:1});h.advance(1350);assert.equal(h.visible('snf-player-fact'),false);
  h.event({...factEvent,duration:2});h.advance(500);h.event({name:'PLAYER_FACT_HIDE'});h.event({...factEvent,text:'Replacement fact',duration:4});h.advance(1700);
  assert.equal(h.visible('snf-player-fact'),true);assert.equal(h.el('snf-player-fact').querySelector('.pg-fact-text').textContent,'Replacement fact');h.clean();
});

test('Player facts fail closed for crowd, toss, kickoff, live gameplay and unknown phase', h => {
  for(const state of [pregame('crowd'),pregame('coin_toss'),pregame('kickoff'),live(),pregame('warmup',{phase:null}),pregame('panning',{phase:undefined})]) {
    h.state(state);h.event(factEvent);assert.equal(h.visible('snf-player-fact'),false);
  }
  h.clean();
});

test('Malformed or conflicting player fact clears the old card instead of reusing cached text', h => {
  const invalid=[{...factEvent,text:''},{...factEvent,player:{name:'Test Receiver'}},{...factEvent,player:{...factEvent.player,name:'M. STAFFORD'}},{...factEvent,teamData:{abbreviation:'DAL'}}];
  h.state(pregame('warmup'));for(const e of invalid){h.event(factEvent);assert.equal(h.visible('snf-player-fact'),true);h.event(e);assert.equal(h.visible('snf-player-fact'),false);assert.equal(h.el('snf-player-fact').querySelector('.pg-fact-text').textContent,'');}h.clean();
});

test('Player fact text remains literal and absent photo or number are omitted', h => {
  h.state(pregame('panning'));h.event({...factEvent,text:'Native says <b>0</b> & keeps its punctuation.',player:{fullName:'Test Center',position:'C',number:-1}});
  const card=h.el('snf-player-fact');assert.equal(card.querySelector('.pg-fact-text').textContent,'Native says <b>0</b> & keeps its punctuation.');assert.equal(card.querySelector('.pg-fact-text').children.length,0);
  assert.equal(card.querySelector('.pg-position').textContent,'C');assert.equal(card.querySelector('.pg-portrait').getAttribute('src'),null);assert.equal(card.classList.contains('no-photo'),true);h.clean();
});

test('Coin toss displays exact native messages until the owning hide event', h => {
  h.state(pregame('coin_toss'));h.event(tossEvent);const card=h.el('snf-coin-toss');
  assert.equal(h.visible('snf-coin-toss'),true);assert.equal(card.querySelector('.pg-toss-text').textContent,tossEvent.text);h.advance(30000);assert.equal(h.visible('snf-coin-toss'),true);
  h.event({...tossEvent,text:'Seattle will defend the north goal.'});assert.equal(h.visible('snf-coin-toss'),true);assert.equal(card.querySelector('.pg-toss-text').textContent,'Seattle will defend the north goal.');
  h.event({name:'COIN_TOSS_HIDE'});h.advance(350);assert.equal(h.visible('snf-coin-toss'),false);assert.equal(card.querySelector('.pg-toss-text').textContent,'');h.clean();
});

test('Legacy native toss permission requires native source and explicit phase-zero pregame', h => {
  h.state(pregame(null));h.event(tossEvent);assert.equal(h.visible('snf-coin-toss'),true);h.event({name:'COIN_TOSS_HIDE'});
  h.event({...tossEvent,source:'browser'});assert.equal(h.visible('snf-coin-toss'),false);
  h.state(pregame(null,{gameStatus:'in_progress'}));h.event(tossEvent);assert.equal(h.visible('snf-coin-toss'),false);
  h.state(pregame('coin_toss',{phase:null}));h.event(tossEvent);assert.equal(h.visible('snf-coin-toss'),false);h.clean();
});

test('Coin toss replacement duration updates cannot let an old timer hide newer text', h => {
  h.state(pregame('coin_toss'));h.event({...tossEvent,duration:1});h.advance(500);h.event({...tossEvent,text:'New toss choice',duration:3});h.advance(700);
  assert.equal(h.visible('snf-coin-toss'),true);assert.equal(h.el('snf-coin-toss').querySelector('.pg-toss-text').textContent,'New toss choice');h.advance(2700);assert.equal(h.visible('snf-coin-toss'),false);h.clean();
});

test('Native replacement is acknowledged after two frames and releases its exact token on hide', h => {
  h.useHost();h.state(pregame(null));const nativeFact={...factEvent,source:'native',nativeVisible:true,replacementToken:23};
  h.event(nativeFact);assert.equal(h.visible('snf-player-fact'),true);assert.deepEqual(h.hostMessages,[]);
  h.advance(31);assert.deepEqual(h.hostMessages,[],'Native fallback must remain until the replacement reaches both frames');
  h.advance(1);assert.deepEqual(h.hostMessages,['SNF_PREGAME_SHOW:23']);
  h.event(nativeFact);h.advance(32);assert.deepEqual(h.hostMessages,['SNF_PREGAME_SHOW:23'],'Repeated native refresh must not duplicate its visibility claim');
  h.event({name:'PLAYER_FACT_HIDE'});assert.deepEqual(h.hostMessages,['SNF_PREGAME_SHOW:23','SNF_PREGAME_HIDE:23']);
  assert.equal(h.el('snf-player-fact').classList.contains('out'),true);h.advance(350);assert.equal(h.visible('snf-player-fact'),false);h.clean();
});

test('Acknowledgment requires a valid native token, accepted payload and available game host', h => {
  h.useHost();h.state(pregame('warmup'));
  const nativeFact={...factEvent,source:'native',nativeVisible:true};
  for(const token of [undefined,0,-1,1.5,'23',Infinity,4294967296]) {h.event({...nativeFact,replacementToken:token});h.advance(32);}
  for(const event of [{...nativeFact,replacementToken:1,nativeVisible:false},{...nativeFact,replacementToken:1,nativeVisible:undefined},{...nativeFact,replacementToken:1,source:'browser'},{...nativeFact,replacementToken:1,text:''},{...nativeFact,replacementToken:1,player:{name:'Missing Full Name'}},{...nativeFact,replacementToken:1,teamData:{abbreviation:'DAL'}}]) {h.event(event);h.advance(32);}
  h.useHost('browser');h.event({...nativeFact,replacementToken:2});h.advance(32);
  h.useHost('game',false);h.event({...nativeFact,replacementToken:3});h.advance(32);
  assert.deepEqual(h.hostMessages,[],'Unowned, rejected or unrendered data must leave the native graphic as fallback');h.clean();
});

test('Coin toss acknowledges the maximum uint32 token and cancels a claim hidden before paint', h => {
  h.useHost();h.state(pregame('coin_toss'));const nativeToss={...tossEvent,nativeVisible:true,replacementToken:4294967295};
  h.event(nativeToss);h.advance(32);assert.deepEqual(h.hostMessages,['SNF_PREGAME_SHOW:4294967295']);
  h.event({name:'COIN_TOSS_HIDE'});assert.deepEqual(h.hostMessages,['SNF_PREGAME_SHOW:4294967295','SNF_PREGAME_HIDE:4294967295']);h.advance(350);
  h.event({...nativeToss,replacementToken:14,text:'New native toss message'});h.advance(16);h.event({name:'COIN_TOSS_HIDE'});h.advance(350);
  assert.equal(h.hostMessages.includes('SNF_PREGAME_SHOW:14'),false,'A native hide before the second frame must cancel the pending suppression claim');
  assert.equal(h.hostMessages.includes('SNF_PREGAME_HIDE:14'),true);assert.equal(h.visible('snf-coin-toss'),false);h.clean();
});

test('Replacement, menu and ownership changes cancel stale native acknowledgments', h => {
  h.useHost();h.state(pregame(null));const nativeFact={...factEvent,source:'native',nativeVisible:true,replacementToken:31};
  h.event(nativeFact);h.advance(16);h.event({...nativeFact,replacementToken:32,text:'Replacement fact'});h.advance(16);
  assert.equal(h.hostMessages.includes('SNF_PREGAME_SHOW:31'),false);assert.equal(h.hostMessages.includes('SNF_PREGAME_HIDE:31'),true);
  h.state({valid:false,context:{scorebugVisible:false}});h.advance(40);
  assert.equal(h.hostMessages.includes('SNF_PREGAME_SHOW:32'),false);assert.equal(h.hostMessages.includes('SNF_PREGAME_HIDE:32'),true);assert.equal(h.visible('snf-player-fact'),false);
  h.state(pregame('warmup'));h.event({...nativeFact,replacementToken:33});h.advance(32);assert.equal(h.hostMessages.includes('SNF_PREGAME_SHOW:33'),true);
  h.event({...factEvent,source:'browser',nativeVisible:false});assert.equal(h.hostMessages.includes('SNF_PREGAME_HIDE:33'),true);
  h.state(pregame(null));assert.equal(h.visible('snf-player-fact'),false);h.clean();
});

test('Native hide and duration retain exact identity through the exit, but cannot erase a replacement', h => {
  h.state(pregame('panning'));h.event(factEvent);const card=h.el('snf-player-fact');h.event({name:'PLAYER_FACT_HIDE'});
  assert.equal(h.visible('snf-player-fact'),true);assert.equal(card.classList.contains('out'),true);
  h.advance(349);assert.equal(card.querySelector('.pg-player-name').textContent,'Test Receiver');assert.equal(card.querySelector('.pg-fact-text').textContent,factEvent.text);
  h.advance(1);assert.equal(h.visible('snf-player-fact'),false);assert.equal(card.querySelector('.pg-fact-text').textContent,'');
  h.event({...factEvent,duration:1});h.advance(1000);assert.equal(card.classList.contains('out'),true);assert.equal(card.querySelector('.pg-fact-text').textContent,factEvent.text);
  h.advance(100);h.event({...factEvent,text:'New owner after old exit',player:{fullName:'Replacement Center',position:'C'}});h.advance(300);
  assert.equal(h.visible('snf-player-fact'),true);assert.equal(card.classList.contains('out'),false);assert.equal(card.querySelector('.pg-player-name').textContent,'Replacement Center');assert.equal(card.querySelector('.pg-fact-text').textContent,'New owner after old exit');
  h.event({name:'PLAYER_FACT_HIDE'});h.state(pregame('panning',{phase:2}));assert.equal(h.visible('snf-player-fact'),false);assert.equal(card.querySelector('.pg-fact-text').textContent,'');h.clean();
});

test('Coin toss and player fact replace each other without overlapping the intro', h => {
  h.state(pregame('warmup'));h.event(factEvent);assert.equal(h.visible('snf-player-fact'),true);assert.equal(h.visible('snf-intro'),false);
  h.state(pregame('coin_toss'));h.event(tossEvent);assert.equal(h.visible('snf-player-fact'),false);assert.equal(h.visible('snf-coin-toss'),true);assert.equal(h.visible('snf-intro'),false);
  h.state(pregame('panning'));h.event(factEvent);assert.equal(h.visible('snf-coin-toss'),false);assert.equal(h.visible('snf-player-fact'),true);assert.equal(h.visible('snf-intro'),false);h.clean();
});

test('Kickoff clears all pregame graphics even if a stale warmup marker remains', h => {
  h.state(pregame('warmup'));h.event(factEvent);h.state(pregame('warmup',{phase:2}));
  for(const id of ['snf-intro','snf-player-fact','snf-coin-toss'])assert.equal(h.visible(id),false);h.event(factEvent);h.event(tossEvent);h.event({name:'BROADCAST_INTRO'});h.advance(30000);
  for(const id of ['snf-intro','snf-player-fact','snf-coin-toss'])assert.equal(h.visible(id),false);h.clean();
});

test('Menus and pause cancel pregame native cards and prevent delayed restoration', h => {
  for(const context of [{scorebugVisible:false,paused:true,pregameStage:'warmup'},{scorebugVisible:false,playSelection:true,pregameStage:'warmup'}]) {
    h.state(pregame('warmup'));h.event({...factEvent,duration:2});h.state(pregame('warmup',{context}));h.advance(10000);assert.equal(h.visible('snf-player-fact'),false);assert.equal(h.visible('snf-intro'),false);
  }
  h.state(pregame('coin_toss'));h.event({...tossEvent,duration:2});h.state({valid:false,context:{scorebugVisible:false}});h.advance(10000);assert.equal(h.visible('snf-coin-toss'),false);h.clean();
});

test('Malformed coin toss clears exact text and never invents a result', h => {
  h.state(pregame('coin_toss'));h.event(tossEvent);h.event({...tossEvent,text:null});
  assert.equal(h.visible('snf-coin-toss'),false);assert.equal(h.el('snf-coin-toss').querySelector('.pg-toss-text').textContent,'');h.clean();
});

test('Profile accepts non-QB biography, jersey zero and supplied zero-valued metadata', h => {
  h.state(live());h.event(profileEvent);assert.equal(h.visible('snf-player-profile'),true);assert.equal(h.visible('bug'),false);
  const panel=h.el('snf-player-profile');assert.equal(panel.querySelector('.profile-name').textContent,'TEST CENTER');assert.equal(panel.querySelector('.profile-position').textContent,'C\n0');
  const rows=panel.querySelectorAll('.profile-row');assert.deepEqual(rows.map(r=>r.querySelector('span').textContent),['HEIGHT','WEIGHT','AGE','EXPERIENCE','COLLEGE']);
  assert.deepEqual(rows.map(r=>r.querySelector('strong').textContent),['6\' 3"','305 LB','0','0 YEARS','Test College']);
  h.advance(2100);assert.equal(h.visible('snf-player-profile'),false);assert.equal(h.visible('bug'),true);h.clean();
});

test('Unknown profile metadata is omitted rather than fabricated', h => {
  h.state(live());h.event({name:'PLAYER_BIO',team:'home',player:{name:'Test Player',position:'WR',number:null,height:null,weight:undefined,age:NaN,yearsPro:Infinity,college:'',fields:[{label:'BAD',value:Infinity},{label:'ZERO',value:0}]}});
  const panel=h.el('snf-player-profile');assert.equal(panel.querySelector('.profile-position').textContent,'WR');
  assert.deepEqual(panel.querySelectorAll('.profile-row').map(r=>r.textContent),['ZERO0']);h.clean();
});

test('The native unknown jersey sentinel is omitted from a player profile', h => {
  h.state(live());h.event({name:'PLAYER_PROFILE',team:'home',player:{name:'Test Player',position:'WR',number:-1}});
  assert.equal(h.el('snf-player-profile').querySelector('.profile-position').textContent,'WR','Unknown jersey -1 is not a player number');h.clean();
});

test('Profile uses a supplied in-game render before falling back to a portrait', h => {
  h.state(live());h.event({name:'PLAYER_PROFILE',team:'home',player:{name:'Test Player',position:'TE',render:'https://renders.local/player.png',portrait:'https://portraits.local/player.png'}});
  assert.equal(h.el('snf-player-profile').querySelector('.profile-portrait').src,'https://renders.local/player.png');h.clean();
});

test('Identity-only profile remains usable without a photo or biography', h => {
  h.state(live());h.event({name:'PLAYER_PROFILE',team:'home',player:{name:'Test Punter',position:'P'}});
  const panel=h.el('snf-player-profile');assert.equal(h.visible('snf-player-profile'),true);assert.equal(panel.classList.contains('identity-only'),true);assert.equal(panel.classList.contains('no-photo'),true);
  assert.equal(panel.querySelector('.profile-portrait').getAttribute('src'),null);assert.equal(panel.querySelectorAll('.profile-row').length,0);h.clean();
});

const pagedProfileEvent = {name:'PLAYER_PROFILE',team:'home',duration:9,player:{name:'Test Player',position:'WR'},rows:Array.from({length:24},(_,i)=>({label:'FIELD '+(i+1),value:i}))};
test('A full player profile reveals all 24 supplied fields across three pages', h => {
  h.state(live());h.event(pagedProfileEvent);const panel=h.el('snf-player-profile');
  const labels=()=>panel.querySelectorAll('.profile-row span').map(r=>r.textContent);
  assert.deepEqual(labels(),Array.from({length:8},(_,i)=>'FIELD '+(i+1)));assert.match(panel.querySelector('.profile-caption').textContent,/1 \/ 3/);
  assert.equal(panel.querySelector('.profile-row strong').textContent,'0');h.advance(3181);
  assert.deepEqual(labels(),Array.from({length:8},(_,i)=>'FIELD '+(i+9)));assert.match(panel.querySelector('.profile-caption').textContent,/2 \/ 3/);
  h.advance(3000);assert.deepEqual(labels(),Array.from({length:8},(_,i)=>'FIELD '+(i+17)));assert.match(panel.querySelector('.profile-caption').textContent,/3 \/ 3/);
  h.advance(3000);assert.equal(h.visible('snf-player-profile'),false);assert.equal(h.visible('bug'),true);h.clean();
});

test('Menu cancels profile page swaps even during their delayed reveal', h => {
  h.state(live());h.event(pagedProfileEvent);h.advance(3010);const rows=h.el('snf-player-profile').querySelector('.profile-rows');
  assert.equal(rows.classList.contains('swap'),true);const labels=rows.querySelectorAll('span').map(r=>r.textContent);
  h.state(live({context:{scorebugVisible:false,playSelection:true}}));h.advance(12000);
  assert.equal(h.visible('snf-player-profile'),false);assert.equal(rows.classList.contains('swap'),false);assert.deepEqual(rows.querySelectorAll('span').map(r=>r.textContent),labels,'Canceled page timers must not mutate the hidden profile');
  h.state(live());assert.equal(h.visible('snf-player-profile'),false);h.clean();
});

test('Replacing a paged profile cancels its pending page and completion timers', h => {
  h.state(live());h.event(pagedProfileEvent);h.advance(3010);h.event({...profileEvent,duration:4});h.advance(1000);
  const panel=h.el('snf-player-profile');assert.equal(h.visible('snf-player-profile'),true);assert.equal(panel.querySelector('.profile-name').textContent,'TEST CENTER');
  assert.equal(panel.querySelector('.profile-rows').classList.contains('swap'),false);assert.equal(panel.querySelector('.profile-row span').textContent,'HEIGHT');
  h.advance(3200);assert.equal(h.visible('snf-player-profile'),false);h.advance(5000);assert.equal(panel.querySelector('.profile-row span').textContent,'HEIGHT');h.clean();
});

test('Malformed profile replacement clears the previous profile and its timer', h => {
  h.state(live());h.event(profileEvent);h.event({name:'PLAYER_PROFILE',team:'home',player:{name:''}});
  assert.equal(h.visible('snf-player-profile'),false);assert.equal(h.visible('bug'),true);h.advance(8000);assert.equal(h.visible('snf-player-profile'),false);h.clean();
});

test('A mismatched cached profile identity cannot attach the wrong portrait or number', h => {
  h.state(live());h.event({name:'PLAYER_PROFILE',team:'home',player:{name:'T. ATWELL',fullName:'Matthew Stafford',position:'QB',number:9,portrait:player.portrait}});
  const panel=h.el('snf-player-profile');assert.equal(panel.querySelector('.profile-name').textContent,'T. ATWELL');assert.equal(panel.querySelector('.profile-portrait').getAttribute('src'),null);assert.equal(panel.querySelector('.profile-position').textContent,'');h.clean();
});

test('Profiles hide stale stat, feature and comparison inserts when taking ownership', h => {
  for(const prior of [{name:'PLAYER_STAT',team:'home',player},{name:'PLAYER_FEATURE',team:'home',player},comparisonEvent,{name:'TEAM_STATS',teamData:{abbreviation:'LAR'},columns:['REC'],rows:[{label:'Receiver',values:[0]}]},{name:'LINEUP',team:'home',players:[{name:'Test Player',position:'QB',number:9}]}]) {
    h.state(live());h.event(prior);h.event(profileEvent);assert.equal(h.visible('snf-player-profile'),true);
    for(const id of ['qb-stat','player-feature','snf-comparison','snf-team-stats','lineup'])assert.equal(h.visible(id),false,id+' must not overlap a profile');
    h.advance(2100);for(const id of ['qb-stat','player-feature','snf-comparison','snf-team-stats','lineup'])assert.equal(h.visible(id),false,id+' must not return from an old timer');
  }
  h.clean();
});

test('An ordinary player card can replace a profile without leaving it underneath', h => {
  h.state(live());h.event(profileEvent);h.event({name:'PLAYER_STAT',team:'home',duration:4,player});
  assert.equal(h.visible('snf-player-profile'),false);assert.equal(h.visible('qb-stat'),true);h.advance(2200);assert.equal(h.visible('qb-stat'),true);h.clean();
});

test('Profile and timeout replacement cancel each other in both directions', h => {
  h.state(live());h.event(profileEvent);h.event({name:'TIMEOUT',duration:4});assert.equal(h.visible('snf-player-profile'),false);assert.equal(h.visible('snf-timeout'),true);
  h.advance(500);h.event(profileEvent);assert.equal(h.visible('snf-timeout'),false);assert.equal(h.visible('snf-player-profile'),true);h.clean();
});

test('Pause and menu cancel profiles and prevent their delayed restoration', h => {
  for(const context of [{scorebugVisible:true,paused:true},{scorebugVisible:false,playSelection:true}]) {
    h.state(live());h.event(profileEvent);h.state(live({context}));assert.equal(h.visible('snf-player-profile'),false);h.advance(8000);assert.equal(h.visible('snf-player-profile'),false);assert.equal(h.visible('bug'),false);
  }
  h.clean();
});

test('Off-air supplemental events cannot return with a later gameplay state', h => {
  h.state({valid:false,context:{scorebugVisible:false}});h.event(profileEvent);h.event({name:'TIMEOUT'});h.event({name:'MATCHUP_INTRO'});h.state(live());
  for(const id of ['snf-player-profile','snf-timeout','snf-intro'])assert.equal(h.visible(id),false);h.clean();
});

test('Unknown quarterback statistics remain unavailable while explicit zeroes remain zero', h => {
  h.state(live());h.event({name:'PLAYER_STAT',team:'home',player:{name:'Test QB',position:'QB',kind:0,completions:0,attempts:null,passingYards:undefined,passingTouchdowns:0,interceptions:null}});
  assert.deepEqual(h.values(),['0','\u2014','\u2014','0/\u2014','\u2014']);h.clean();
});

test('Rate calculation requires all inputs; a complete zero-stat line is preserved', h => {
  h.state(live());h.event({name:'PLAYER_STAT',team:'home',player:{name:'Test QB',kind:0,completions:0,attempts:0,passingYards:0,passingTouchdowns:0,interceptions:0}});assert.equal(h.values()[4],'0.0');
  h.event({name:'PLAYER_STAT',team:'home',player:{name:'Test Runner',position:'RB',carries:0,rushingYards:null}});assert.equal(h.values()[3],'\u2014');
  h.event({name:'PLAYER_STAT',team:'home',player:{name:'Test Runner',position:'RB',carries:0,rushingYards:0}});assert.equal(h.values()[3],'0.0');h.clean();
});

test('Supplied stat fields retain labels, zeroes and unavailable values', h => {
  h.state(live());h.event({name:'PLAYER_STAT',team:'home',player:{name:'Test Lineman',position:'OL',fields:[{label:'PANCAKES',value:0},{label:'PRESSURES',value:null},{label:'SACKS ALLOWED',value:Infinity},{label:'',value:17}]}});
  assert.deepEqual(h.labels(),['PANCAKES','PRESSURES','SACKS ALLOWED']);assert.deepEqual(h.values(),['0','\u2014','\u2014']);h.clean();
});

test('Offensive linemen are not misclassified as defensive stat cards', h => {
  h.state(live());h.event({name:'PLAYER_STAT',team:'home',player:{name:'Test Tackle',position:'LT',kind:3}});
  assert.equal(h.el('qb-stat').querySelector('.qs-position').textContent,'LT');assert.deepEqual(h.labels(),['STATS']);assert.deepEqual(h.values(),['\u2014']);
  h.event({name:'PLAYER_FEATURE',team:'home',player:{name:'Test Tackle',position:'LT',kind:3}});assert.doesNotMatch(h.el('player-feature').textContent,/TACKLES|DEFENSIVE TOUCHDOWNS|FORCED FUMBLES/);h.clean();
});

test('A supplied single-column feature keeps its actual labels and historical values', h => {
  h.state(live());h.event({name:'PLAYER_FEATURE',team:'home',title:'Special Teams',valueCaption:'2024',player:{name:'Test Punter',position:'P'},rows:[{label:'NET AVERAGE',value:'41.2'},{label:'BLOCKED',values:[0]},{label:'UNAVAILABLE',value:null}]});
  const panel=h.el('player-feature'),rows=panel.querySelectorAll('.pf-table > div:not(.pf-head)');assert.equal(panel.classList.contains('single-column'),true);
  assert.equal(panel.querySelector('.pf-head b').textContent,'2024');assert.deepEqual(rows.slice(0,3).map(r=>[r.querySelector('span').textContent,r.querySelector('b').textContent]),[['NET AVERAGE','41.2'],['BLOCKED','0'],['UNAVAILABLE','\u2014']]);
  assert.equal(rows[0].querySelector('em').textContent,'');h.clean();
});

test('A supplied two-column feature preserves both values and does not borrow unrelated game stats', h => {
  h.state(live());h.event({name:'PLAYER_FEATURE',team:'home',player,columns:['2023','2024'],rows:[{label:'STARTS',values:[0,15]},{label:'MISSING',values:[null,NaN]}]});
  const panel=h.el('player-feature'),rows=panel.querySelectorAll('.pf-table > div:not(.pf-head)');assert.equal(panel.classList.contains('single-column'),false);
  assert.deepEqual(rows.slice(0,2).map(r=>[r.querySelector('span').textContent,r.querySelector('b').textContent,r.querySelector('em').textContent]),[['STARTS','0','15'],['MISSING','\u2014','\u2014']]);
  assert.equal(rows[2].querySelector('span').textContent,'\u2014','Unspecified custom rows must not borrow the player\'s current QB stats');h.clean();
});

test('Context card topic and footer are supplied facts and clear on replacement', h => {
  h.state(live()); h.event({name:'PLAYER_STAT',team:'home',player,topic:'PASSING VS BLITZ',note:'A supplied context note'});
  const card=h.el('qb-stat');assert.equal(card.classList.contains('with-topic'),true);assert.equal(card.classList.contains('with-note'),true);
  assert.equal(card.querySelector('.qs-topic').textContent,'PASSING VS BLITZ');assert.equal(card.querySelector('.qs-note').textContent,'A supplied context note');
  h.event({name:'PLAYER_STAT',team:'home',player:{name:'Receiver',position:'WR',kind:2,receptions:0}});
  assert.equal(card.classList.contains('with-topic'),false);assert.equal(card.classList.contains('with-note'),false);assert.equal(card.querySelector('.qs-note').textContent,'');h.clean();
});
test('Malformed topic or footer cannot introduce fabricated context', h => {
  h.state(live());h.event({name:'PLAYER_STAT',team:'home',player,topic:{label:'BAD'},note:Infinity});
  assert.equal(h.el('qb-stat').classList.contains('with-topic'),false);assert.equal(h.el('qb-stat').classList.contains('with-note'),false);h.clean();
});

test('Reference logo crops clear on team changes and fall back without repeated failed loads', h => {
  const s=live({away:{abbreviation:'LAR',score:0},home:{abbreviation:'CIN',score:0}});
  h.state(s);
  const away=h.el('bug').querySelector('.half.away .logo-window'),home=h.el('bug').querySelector('.half.home .logo-window');
  assert.equal(away.querySelector('img').src,'assets/plates/logo_LAR_away.png');
  assert.equal(home.querySelector('img').src,'assets/plates/logo_CIN_home.png');
  h.state(live());
  assert.equal(away.classList.contains('reference-logo'),false);assert.equal(home.classList.contains('reference-cin'),false);
  assert.equal(away.querySelector('img').src,'assets/logos/SEA.png');assert.equal(home.querySelector('img').src,'assets/logos/LAR.png');
  h.state(s);away.querySelector('img').onerror();h.state(s);
  assert.equal(away.classList.contains('reference-logo'),false);assert.equal(away.querySelector('img').src,'assets/logos/LAR.png');
  h.event({name:'PLAYER_STAT',team:'home',player});
  assert.equal(h.el('qb-stat').querySelector('.qs-team-logo').src,'assets/logos/CIN.png','Player cards must retain the complete logo');h.clean();
});

let passed = 0;
for (const t of tests) {
  try { t.fn(createHarness()); console.log('PASS ' + t.name); passed++; }
  catch (e) { console.log('FAIL ' + t.name + '\n     ' + String(e.message).replace(/\n/g, '\n     ')); }
}
console.log('\n' + passed + '/' + tests.length + ' static lifecycle checks passed. No game build or launch performed.');
console.log('Scope: these checks validate presentation package behavior. They do not prove native trigger/data extraction or in-game rendering; no game build or launch was performed.');
process.exitCode = passed === tests.length ? 0 : 1;
