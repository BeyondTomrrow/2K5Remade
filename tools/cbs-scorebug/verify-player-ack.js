/* Execute the real CBS stats lifecycle with a deterministic clock and DOM.
 * No browser, game, build, external library or production-source mutation.
 * This tests page-side ownership/paint acknowledgements, not native drawing.
 * Run: node tools/cbs-scorebug/verify-player-ack.js
 */
'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const source = fs.readFileSync(path.resolve(__dirname,
  '../../mods/presentations/CBS_2026/stats.js'), 'utf8');
const teamSource = fs.readFileSync(path.resolve(__dirname,
  '../../mods/presentations/CBS_2026/stats-team.js'), 'utf8');
let checks = 0;
function equal(actual, expected, label) { assert.deepStrictEqual(actual, expected, label); ++checks; }
function yes(value, label) { assert.ok(value, label); ++checks; }
function copy(value) { return JSON.parse(JSON.stringify(value)); }
function deferred() {
  let resolve, reject;
  const promise = new Promise((done, fail) => { resolve = done; reject = fail; });
  return { promise, resolve, reject };
}
async function microtasks() { await Promise.resolve(); await Promise.resolve(); await Promise.resolve(); }

class Element {
  constructor(tag) {
    this.tagName = tag.toUpperCase(); this.children = []; this.parentNode = null;
    this.attributes = {}; this.className = ''; this.hidden = false; this.textContent = '';
    this.styleValues = {}; this.style = {setProperty: (k, v) => { this.styleValues[k] = String(v); }};
    this.rect = {width: 466, height: 122}; this.visibility = 'visible';
    const list = () => new Set(this.className.split(/\s+/).filter(Boolean));
    this.classList = {
      add: (...names) => { const values = list(); names.forEach(n => values.add(n)); this.className = [...values].join(' '); },
      remove: (...names) => { const values = list(); names.forEach(n => values.delete(n)); this.className = [...values].join(' '); },
      contains: name => list().has(name),
      toggle: (name, force) => { const values = list(), on = force == null ? !values.has(name) : !!force; if (on) values.add(name); else values.delete(name); this.className = [...values].join(' '); return on; }
    };
  }
  appendChild(child) { child.parentNode = this; this.children.push(child); return child; }
  replaceChildren(...children) { this.children.forEach(n => { n.parentNode = null; }); this.children = []; children.forEach(n => this.appendChild(n)); }
  setAttribute(key, value) { this.attributes[key] = String(value); if(key==='class')this.className=String(value); }
  removeAttribute(key) { delete this.attributes[key]; }
  getAttribute(key) { return Object.prototype.hasOwnProperty.call(this.attributes, key) ? this.attributes[key] : null; }
  get firstChild() { return this.children[0] || null; }
  querySelector(selector) {
    const name = selector[0] === '.' ? selector.slice(1) : null;
    for (const child of this.children) {
      if (name ? child.classList.contains(name) : child.tagName.toLowerCase() === selector) return child;
      const found = child.querySelector(selector); if (found) return found;
    }
    return null;
  }
  getBoundingClientRect() { return this.rect; }
}

function all(root, cls) {
  const result = [];
  for (const child of root.children) {
    if (child.classList.contains(cls)) result.push(child);
    result.push(...all(child, cls));
  }
  return result;
}
function contents(root) { return [root.textContent, ...root.children.map(contents)].filter(Boolean).join(' | '); }
function values(root) { return all(root, 'cbs-stat-column').map(column => column.children.map(cell => cell.textContent)); }
function teamEvent(extra = {}) {
  return Object.assign({name: 'TEAM_STAT', scope: 'this_game', source: 'game', category: 'yardage', displaySeconds: 3,
    teams: {away: {rushingYards: 7, passingYards: 11, totalYards: 18}, home: {rushingYards: 0, passingYards: -3, totalYards: -3}}}, extra);
}

function fixture(options = {}) {
  let now = 0, nextId = 1;
  const timers = new Map(), raf = [], messages = [], reports = [];
  const fonts = deferred(), canvas = new Element('div'), bug = new Element('div');
  const elements = {canvas, eyebar: bug};
  const state = {valid: true, gameStatus: 'in_progress', matchId: 'match-A',
    away: {abbreviation: 'DAL', primaryColor: '#002c5f'}, home: {abbreviation: 'NE', primaryColor: '#002244'},
    context: {scorebugVisible: true, playSelection: false, paused: false}};
  const context = {console, Promise,
    Date: {now: () => now},
    document: {fonts: {ready: fonts.promise}, documentElement: {getAttribute: () => null},
      createElement: tag => new Element(tag), createElementNS: (ns,tag) => new Element(tag), getElementById: id => elements[id] || null},
    setTimeout: (fn, delay = 0) => { const id = nextId++; timers.set(id, {fn, due: now + Math.max(0, Number(delay) || 0)}); return id; },
    clearTimeout: id => timers.delete(id),
    requestAnimationFrame: fn => { const id = nextId++; raf.push({id, fn}); return id; },
    getComputedStyle: element => ({visibility: element.visibility, getPropertyValue: key => element.styleValues[key] || ''}),
    matchMedia: () => ({matches: !!options.reduced}),
    NFL2K5: {host: options.host || 'game', log: message => messages.push(message)}
  };
  context.window = context;
  context.CBSMotion = {report: (...args) => reports.push(args), cancel: () => { if (context.CBSStats) context.CBSStats.cancel(); }};
  vm.createContext(context);
  vm.runInContext(teamSource, context, {filename: 'CBS_2026/stats-team.js'});
  vm.runInContext(fs.readFileSync(path.resolve(__dirname, '../../mods/presentations/CBS_2026/stats-receiver.js'), 'utf8'), context, {filename:'CBS_2026/stats-receiver.js'});
  vm.runInContext(source, context, {filename: 'CBS_2026/stats.js'});
  const api = context.CBSStats, root = canvas.children[0];
  if (options.state !== false) api.updateState(copy(state));
  function advance(ms) {
    const target = now + ms; let runs = 0;
    for (;;) {
      let selected = null;
      for (const [id, timer] of timers) if (timer.due <= target && (!selected || timer.due < selected[1].due || timer.due === selected[1].due && id < selected[0])) selected = [id, timer];
      if (!selected) break;
      if (++runs > 1000) throw new Error('Timer recursion did not terminate');
      now = selected[1].due; timers.delete(selected[0]); selected[1].fn();
    }
    now = target;
  }
  function frame() { const batch = raf.splice(0); advance(16); batch.forEach(job => job.fn(now)); }
  function update(change = {}) {
    Object.assign(state, change);
    if (change.context) state.context = Object.assign({scorebugVisible: true, playSelection: false, paused: false}, change.context);
    api.updateState(copy(state));
  }
  function event(token = '17', extra = {}) {
    const base = {name: 'PLAYER_STAT', team: 'away', nativeCardToken: token, displaySeconds: 6,
      player: {name: 'Q. Carter', fullName: 'Quincy Carter', position: 'QB', kind: 0, number: 17,
        completions: 0, attempts: 2, passingYards: -3, passingTouchdowns: 0, interceptions: 0}};
    return Object.assign(base, extra);
  }
  return {api, teamApi: context.CBSTeamStats, root, bug, canvas, fonts, state, messages, reports, advance, frame, update, event,
    pendingFrames: () => raf.length, now: () => now,
    show: token => messages.filter(m => m === 'NFL2K5_PLAYER_CARD_SHOW:' + token),
    hide: token => messages.filter(m => m === 'NFL2K5_PLAYER_CARD_HIDE:' + token)};
}

async function paint(h) { h.fonts.resolve(); h.advance(440); await microtasks(); h.frame(); h.frame(); }
const cases = [];
function test(name, run) { cases.push({name, run}); }

test('SHOW waits for the entrance, resolved fonts and two separate paint frames', async () => {
  const h = fixture(); h.api.handleEvent(h.event());
  equal(h.api.snapshot().phase, 'entrance', 'A requested card starts in entrance');
  h.advance(439); await microtasks(); equal(h.show('17').length, 0, 'No acknowledgement before 440ms');
  h.advance(1); await microtasks(); equal(h.api.snapshot().phase, 'hold', 'Entrance completes at 440ms');
  equal(h.pendingFrames(), 0, 'Unresolved fonts prevent even the first RAF');
  h.fonts.resolve(); await microtasks(); equal(h.pendingFrames(), 1, 'Fonts ready schedules the first frame');
  equal(h.show('17').length, 0, 'Font readiness alone does not imply paint');
  h.frame(); equal(h.show('17').length, 0, 'The first paint frame cannot acknowledge');
  equal(h.pendingFrames(), 1, 'The second frame is a separate callback');
  h.frame(); equal(h.show('17').length, 1, 'SHOW follows the second frame');
  h.frame(); equal(h.show('17').length, 1, 'An extra frame cannot duplicate SHOW');
  equal(h.hide('17').length, 0, 'A live card is not released early');
});

test('fonts resolved early do not bypass the 440ms entrance', async () => {
  const h = fixture(); h.fonts.resolve(); await microtasks(); h.api.handleEvent(h.event('23'));
  h.advance(439); await microtasks(); h.frame(); equal(h.show('23').length, 0, 'A frame crossing 440ms has not completed the two-frame gate');
  await microtasks(); h.frame(); equal(h.show('23').length, 0, 'First actual post-entrance RAF still cannot acknowledge');
  h.frame(); equal(h.show('23').length, 1, 'Only the second actual RAF acknowledges');
});

test('cancellation before or between paint frames prevents stale SHOW and sends HIDE', async () => {
  for (const point of ['entrance', 'fonts', 'first-frame', 'painted']) {
    const h = fixture(); h.api.handleEvent(h.event('31'));
    if (point !== 'entrance') { h.advance(440); h.fonts.resolve(); await microtasks(); }
    if (point === 'first-frame' || point === 'painted') h.frame();
    if (point === 'painted') h.frame();
    const before = h.show('31').length; h.api.cancel();
    equal(h.hide('31').length, 1, point + ': HIDE immediately releases the token');
    h.fonts.resolve(); await microtasks(); h.frame(); h.frame(); h.advance(10000);
    equal(h.show('31').length, before, point + ': queued work cannot acknowledge after cancellation');
    equal(h.api.snapshot().visible, false, point + ': root remains hidden');
    equal(h.hide('31').length, 1, point + ': stale timers do not send duplicate HIDE');
  }
});

test('pause, play selection, hidden scorebug, invalid game, final and match changes release ownership', async () => {
  const changes = [
    {context: {paused: true}}, {context: {playSelection: true}}, {context: {scorebugVisible: false}}, {context: {periodEnding: true}},
    {valid: false}, {gameStatus: 'final'}, {matchId: 'match-B'}, {away: {abbreviation: 'KC'}}
  ];
  for (const change of changes) {
    const h = fixture(); h.api.handleEvent(h.event('41')); h.advance(440); h.fonts.resolve(); await microtasks(); h.frame();
    h.update(change); equal(h.hide('41').length, 1, JSON.stringify(change) + ': ownership released');
    h.frame(); equal(h.show('41').length, 0, JSON.stringify(change) + ': pending second frame suppressed');
    equal(h.api.snapshot().visible, false, JSON.stringify(change) + ': root cleared');
  }
});

test('suppressed or uninitialized state never requests native replacement', async () => {
  for (const change of [null, {context: {paused: true}}, {context: {playSelection: true}}, {context: {scorebugVisible: false}}, {context: {periodEnding: true}}, {valid: false}, {gameStatus: 'final'}]) {
    const h = fixture({state: change !== null}); if (change) h.update(change);
    equal(h.api.handleEvent(h.event('47')), true, 'Known but suppressed event is consumed');
    await paint(h); equal(h.messages, [], 'Suppressed event sends no native SHOW/HIDE');
    equal(h.api.snapshot().visible, false, 'Suppressed root stays hidden');
  }
});

test('a newer token invalidates old pending font and RAF work', async () => {
  const h = fixture(); h.api.handleEvent(h.event('51')); h.advance(440);
  h.api.handleEvent(h.event('52')); h.advance(440);
  equal(h.hide('51').length, 1, 'New owner immediately releases the old token');
  h.fonts.resolve(); await microtasks(); h.frame(); h.frame();
  equal(h.show('51').length, 0, 'Old font completion cannot acknowledge the old card');
  equal(h.show('52').length, 1, 'Only the current owner acknowledges');
  h.api.handleEvent(h.event('52')); h.frame();
  equal(h.show('52').length, 1, 'Duplicate current card does not restart acknowledgement');
  equal(h.hide('52').length, 0, 'Duplicate current card does not release itself');
});

test('matching PLAYER_STAT_HIDE exits; a mismatched or malformed token leaves the current owner alone', async () => {
  const h = fixture(); h.api.handleEvent(h.event('61')); await paint(h);
  for (const token of ['62', '', '061', 61]) {
    equal(h.api.handleEvent({name: 'PLAYER_STAT_HIDE', nativeCardToken: token}), true, 'Hide route consumes the event');
    equal(h.api.snapshot().phase, 'hold', 'A nonmatching token cannot dismiss current artwork');
  }
  h.api.handleEvent({name: 'PLAYER_STAT_HIDE', nativeCardToken: '61'});
  equal(h.api.snapshot().phase, 'exit', 'Matching token begins the measured exit');
  h.advance(929); equal(h.hide('61').length, 0, 'Native HIDE is not sent before exit completion');
  h.advance(1); equal(h.hide('61').length, 1, 'Native HIDE follows exit completion');
  equal(h.api.snapshot().visible, false, 'The completed exit clears artwork');
});

test('an expired or hidden card cannot acknowledge after slow fonts resolve', async () => {
  const h = fixture(); h.api.handleEvent(h.event('71')); h.advance(6440);
  equal(h.api.snapshot().phase, 'exit', 'Timed hold has expired while fonts are still pending');
  h.fonts.resolve(); await microtasks(); h.frame(); h.frame();
  equal(h.show('71').length, 0, 'An exiting card cannot claim native artwork');
  h.advance(930); equal(h.hide('71').length, 1, 'Expiry restores native artwork');
  equal(h.api.snapshot().visible, false, 'Expired artwork is cleared');
});

test('browser/editor previews never acknowledge native artwork', async () => {
  for (const host of ['browser', 'editor']) {
    const h = fixture({host}); h.api.handleEvent(h.event('81')); await paint(h);
    equal(h.api.snapshot().visible, true, host + ': fixture still renders');
    equal(h.messages, [], host + ': paint sends no native SHOW');
    h.api.cancel(); equal(h.messages, [], host + ': cancellation sends no native HIDE');
  }
});

test('only canonical positive uint32 token strings are acknowledged', async () => {
  const invalid = [undefined, null, 0, 1, true, {}, [], '', '0', '01', '+1', '-1', '1.0', '1e1', ' 1', '1 ', '1\n', '4294967296', '18446744073709551615', '999999999999999999999'];
  for (const token of invalid) {
    const h = fixture(); const e = h.event('91'); e.nativeCardToken = token;
    h.api.handleEvent(e); await paint(h); h.api.cancel();
    equal(h.messages, [], 'Invalid token ' + JSON.stringify(token) + ' never reaches native protocol');
  }
  for (const token of ['1', '4294967295']) {
    const h = fixture(); h.api.handleEvent(h.event(token)); await paint(h);
    equal(h.show(token).length, 1, 'Canonical uint32 ' + token + ' is acknowledged as a string');
    h.api.cancel(); equal(h.hide(token).length, 1, 'Canonical uint32 ' + token + ' releases cleanly');
  }
});

test('invalid required player data falls back to native artwork; zero and negative yards remain valid', async () => {
  const changes = [
    {player: null}, {team: 'both'}, {player: {name: '', fullName: ''}},
    {player: {completions: true}}, {player: {completions: []}}, {player: {attempts: ''}},
    {player: {passingYards: null}}, {player: {completions: 3, attempts: 2}}
  ];
  for (const change of changes) {
    const h = fixture(), e = h.event('101');
    if ('team' in change) e.team = change.team;
    if ('player' in change) e.player = change.player === null ? null : Object.assign(e.player, change.player);
    h.api.handleEvent(e); await paint(h);
    equal(h.api.snapshot().visible, false, JSON.stringify(change) + ': invalid fields cannot produce a painted replacement');
    equal(h.messages, [], JSON.stringify(change) + ': invalid fields keep native artwork without SHOW/HIDE');
    yes(h.reports.some(r => r[1] === 'missing-data'), 'Invalid fields report why replacement is unavailable');
  }
  const h = fixture(); h.api.handleEvent(h.event('102')); await paint(h);
  equal(h.show('102').length, 1, '0 completions and -3 passing yards are valid real statistics');
  const fullName = fixture(), e = fullName.event('103'); e.player.name = '';
  fullName.api.handleEvent(e); await paint(fullName);
  equal(fullName.show('103').length, 1, 'A supplied full name is a valid identity fallback');
});

test('zero-area, hidden or rejected-font artwork cannot acknowledge', async () => {
  for (const reason of ['width', 'height', 'visibility', 'font-rejection']) {
    const h = fixture(); h.api.handleEvent(h.event('111')); const plate = h.root.querySelector('.cbs-stat-detail');
    if (reason === 'width') plate.rect.width = 0;
    if (reason === 'height') plate.rect.height = 0;
    if (reason === 'visibility') plate.visibility = 'hidden';
    h.advance(440);
    if (reason === 'font-rejection') h.fonts.reject(new Error('Font initialization failed')); else h.fonts.resolve();
    await microtasks(); h.frame(); h.frame();
    equal(h.show('111').length, 0, reason + ': absent paint cannot hide native artwork');
    h.api.cancel(); equal(h.hide('111').length, 1, reason + ': cancellation releases pending ownership');
  }
});

test('reduced motion still requires fonts and two paint frames', async () => {
  const h = fixture({reduced: true}); h.api.handleEvent(h.event('121')); h.advance(0);
  equal(h.api.snapshot().phase, 'hold', 'Reduced motion skips entrance duration');
  equal(h.messages, [], 'Skipping animation does not skip paint readiness');
  h.fonts.resolve(); await microtasks(); h.frame(); equal(h.show('121').length, 0, 'First reduced-motion paint is insufficient');
  h.frame(); equal(h.show('121').length, 1, 'Second reduced-motion paint acknowledges');
});

test('native-owned cards hold beyond twenty seconds without duplicate entrance or acknowledgement', async () => {
  const h = fixture(), e = h.event('131', {nativeCardOwned: true, displaySeconds: 2});
  h.api.handleEvent(e); await paint(h); h.advance(30000);
  equal(h.api.snapshot().phase, 'hold', 'Native ownership outlives the ordinary displaySeconds timer');
  equal(h.api.snapshot().visible, true, 'An unclosed real popup retains its replacement after thirty seconds');
  equal(h.show('131').length, 1, 'The long hold retains exactly one SHOW');
  equal(h.hide('131').length, 0, 'No independent local timer exposes the native card');
  h.api.handleEvent(copy(e)); equal(h.api.snapshot().phase, 'hold', 'A duplicate does not restart the entrance');
  h.advance(30000); await microtasks(); h.frame(); h.frame();
  equal(h.show('131').length, 1, 'A duplicate cannot create another paint acknowledgement');
  equal(h.hide('131').length, 0, 'A duplicate cannot release native ownership');
});

test('native-owned close is token matched and idempotent through its one measured exit', async () => {
  const h = fixture(); h.api.handleEvent(h.event('141', {nativeCardOwned: true})); await paint(h); h.advance(25000);
  h.api.handleEvent({name: 'PLAYER_STAT_HIDE', nativeCardToken: '140'});
  equal(h.api.snapshot().phase, 'hold', 'An older token cannot close the current native owner');
  const close = {name: 'PLAYER_STAT_HIDE', nativeCardToken: '141'};
  h.api.handleEvent(close); equal(h.api.snapshot().phase, 'exit', 'The real matching close starts exit');
  h.advance(300); h.api.handleEvent(copy(close));
  equal(h.reports.filter(r => r[1] === 'exit').length, 1, 'A duplicate native close does not start a second exit');
  h.advance(629); equal(h.api.snapshot().visible, true, 'The first close owns the 930ms exit deadline');
  equal(h.hide('141').length, 0, 'Native release waits until the measured exit is complete');
  h.advance(1); equal(h.api.snapshot().visible, false, 'The original close clears artwork at 930ms');
  equal(h.hide('141').length, 1, 'Exactly one HIDE is sent');
  h.api.handleEvent(copy(close)); h.advance(30000);
  equal(h.hide('141').length, 1, 'Further duplicate closes cannot release the same owner again');
});

test('invalid tokens or nonboolean ownership flags never create an unbounded hold', async () => {
  const values = [
    {nativeCardToken: '', nativeCardOwned: true},
    {nativeCardToken: '0', nativeCardOwned: true},
    {nativeCardToken: '4294967296', nativeCardOwned: true},
    {nativeCardToken: 151, nativeCardOwned: true},
    {nativeCardToken: '151', nativeCardOwned: 'true'},
    {nativeCardToken: '151', nativeCardOwned: 1},
    {nativeCardToken: '151', nativeCardOwned: false}
  ];
  for (const value of values) {
    const h = fixture(); h.api.handleEvent(h.event('151', value)); await paint(h); h.advance(30000);
    equal(h.api.snapshot().visible, false, JSON.stringify(value) + ': ordinary timed exit remains enabled');
    equal(h.api.snapshot().name, '', JSON.stringify(value) + ': expired owner is cleared');
    if (typeof value.nativeCardToken !== 'string' || value.nativeCardToken !== '151') equal(h.messages, [], 'Invalid ownership token cannot acknowledge or release native artwork');
    else equal(h.hide('151').length, 1, 'A valid but non-owned token follows ordinary timed release');
  }
});

test('native-owned cancellation and match changes release long-lived cards and invalidate pending paint', async () => {
  for (const reason of ['cancel', 'match', 'pause', 'hidden']) {
    const h = fixture(); h.api.handleEvent(h.event('161', {nativeCardOwned: true})); await paint(h); h.advance(30000);
    if (reason === 'cancel') h.api.cancel();
    if (reason === 'match') h.update({matchId: 'another-match'});
    if (reason === 'pause') h.update({context: {paused: true}});
    if (reason === 'hidden') h.update({context: {scorebugVisible: false}});
    equal(h.hide('161').length, 1, reason + ': native ownership is released immediately');
    equal(h.api.snapshot().visible, false, reason + ': long-lived artwork is removed');
    h.frame(); h.frame(); h.advance(30000);
    equal(h.show('161').length, 1, reason + ': no stale work can acknowledge again');
    equal(h.hide('161').length, 1, reason + ': release remains exactly once');
  }
  const h = fixture(); h.api.handleEvent(h.event('162', {nativeCardOwned: true})); h.advance(440);
  h.update({matchId: 'match-C'}); h.fonts.resolve(); await microtasks(); h.frame(); h.frame();
  equal(h.show('162').length, 0, 'A stale native-owned font promise cannot acknowledge a later match');
  equal(h.hide('162').length, 1, 'A pending native-owned card is released on match change');
});

test('typed RB, WR, TE and defense inserts use their live fields instead of irrelevant QB zeros', async () => {
  const players = [
    {position: 'RB', kind: 1, name: 'M. Barber', carries: 0, rushingYards: -5,
      expected: [['RUSH', '0'], ['RUSH YDS', '-5']]},
    {position: 'WR', kind: 2, name: 'K. Johnson', receptions: 3, receivingYards: 41, receivingTouchdowns: 1,
      expected: [['REC', '3'], ['YDS', '41'], ['AVG', '13.7'], ['TD', '1']]},
    {position: 'TE', kind: 2, name: 'J. Witten', receptions: 0, receivingYards: 0,
      expected: [['REC', '0'], ['YDS', '0'], ['AVG', '\u2014'], ['TD', '\u2014']]},
    {position: 'DEF', kind: 3, name: 'D. Ware', tackles: 2, sacks: 0.5, defensiveInterceptions: 0,
      expected: [['TACKLES', '2'], ['SACKS', '0.5'], ['INT', '0']]}
  ];
  for (let index = 0; index < players.length; ++index) {
    const item = players[index], h = fixture(), token = String(170 + index);
    const player = Object.assign({number: 1, completions: 0, attempts: 0, passingYards: 0}, item);
    delete player.expected;
    h.api.handleEvent(h.event(token, {player})); await paint(h);
    equal(values(h.root), item.expected, item.position + ': real typed field labels and values are rendered');
    yes(contents(h.root).includes(item.name.toUpperCase()), item.position + ': actual supplied player identity is rendered');
    equal(h.show(token).length, 1, item.position + ': valid typed artwork completes the paint protocol');
    equal(h.root.getAttribute('aria-hidden'), 'false', item.position + ': artwork is accessible while painted');
    h.api.cancel(); equal(h.hide(token).length, 1, item.position + ': cancellation restores native artwork');
  }
});

test('missing non-QB counters are unavailable rather than synthesized from an unrelated stat family', async () => {
  for (const player of [
    {position: 'RB', kind: 1, carries: 3},
    {position: 'WR', kind: 2, receivingYards: 19},
    {position: 'TE', kind: 2, receptions: 2, receivingYards: null},
    {position: 'DEF', kind: 3, sacks: 0.5, defensiveInterceptions: 0}
  ]) {
    const h = fixture(), p = Object.assign({name: 'Actual Player', completions: 0, attempts: 0, passingYards: 0}, player);
    h.api.handleEvent(h.event('181', {player: p})); await paint(h);
    equal(h.api.snapshot().visible, false, player.position + ': incomplete live fields cannot create a replacement');
    equal(h.messages, [], player.position + ': no SHOW/HIDE for artwork that was never created');
    yes(h.reports.some(report => report[1] === 'missing-data'), player.position + ': missing data is reported');
  }
});

test('the real team renderer preserves independent yardage and unavailable cells', async () => {
  const h = fixture();
  const e = teamEvent({teams: {away: {rushingYards: 7, passingYards: 11}, home: {rushingYards: 0, passingYards: -3, totalYards: -3}}});
  equal(h.teamApi.render(h.root, e, h.state), true, 'A current-game event with available team counters renders');
  equal(all(h.root, 'cbs-team-column-label').map(cell => cell.textContent), ['RUSH', 'PASS', 'TOTAL'], 'Measured columns are created by actual stats-team.js');
  equal(all(h.root, 'cbs-team-value').map(cell => cell.textContent), ['7', '11', '\u2014', '0', '-3', '-3'], 'A missing total stays unavailable, never the inferred sum of rush plus pass');
  equal(all(h.root, 'cbs-team-unavailable').length, 1, 'Only the unknown cell carries unavailable treatment');
  equal(all(h.root, 'cbs-team-unavailable')[0].getAttribute('aria-label'), 'Unavailable', 'Unknown value is accessible as unavailable');
  equal(h.root.querySelector('.cbs-team-table').getAttribute('role'), 'table', 'Comparison has the actual renderer table semantics');
  equal(h.root.querySelector('.cbs-team-table').styleValues['--cbs-team-columns'], '3', 'The renderer supplies its actual column count');
  equal(all(h.root, 'cbs-team-away')[0].styleValues['--cbs-team-color'], '#002c5f', 'Away team uses supplied live team color');
  equal(all(h.root, 'cbs-team-city').map(cell => cell.textContent), ['DAL', 'NE'], 'Live team identities populate both sides');
});

test('team defense supports half sacks and possession formats verified seconds without inventing missing values', async () => {
  const h = fixture();
  h.teamApi.render(h.root, teamEvent({category: 'defense', teams: {
    away: {sacks: 0.5, tackles: 7, defensiveInterceptions: 0},
    home: {sacks: 0.25, tackles: null, defensiveInterceptions: 1}
  }}), h.state);
  equal(all(h.root, 'cbs-team-column-label').map(cell => cell.textContent), ['SACKS', 'TACKLES', 'INT'], 'Defense uses independent verified team counters');
  equal(all(h.root, 'cbs-team-value').map(cell => cell.textContent), ['0.5', '7', '0', '\u2014', '\u2014', '1'], 'Half sacks and real zeros survive; non-half fraction and null stay unavailable');
  h.teamApi.render(h.root, teamEvent({category: 'possession', teams: {
    away: {firstDowns: 1, timeOfPossession: 888}, home: {firstDowns: 0}
  }}), h.state);
  equal(all(h.root, 'cbs-team-value').map(cell => cell.textContent), ['1', '14:48', '0', '\u2014'], 'Verified team TOP is seconds and omitted TOP does not become 0:00');
  h.teamApi.render(h.root, teamEvent({category: 'possession', teams: {
    away: {timeOfPossession: 0}, home: {timeOfPossession: 1184}
  }}), h.state);
  equal(all(h.root, 'cbs-team-value').map(cell => cell.textContent), ['\u2014', '0:00', '\u2014', '19:44'], 'An explicit real zero TOP differs from unknown TOP');
});

test('invalid team event provenance, category or empty counters cannot replace a player card', async () => {
  const invalid = [
    {scope: 'career'}, {scope: undefined}, {source: 'estimated'}, {name: 'PLAYER_STAT'},
    {category: 'not-a-category'}, {category: '__proto__'}, {teams: null},
    {teams: {away: {rushingYards: true, passingYards: '', totalYards: null}, home: {}}},
    {teams: {away: {valid: false, totalYards: 90}, home: {}}}
  ];
  for (const change of invalid) {
    const h = fixture(), sentinel = new Element('div'); h.root.appendChild(sentinel);
    equal(h.teamApi.render(h.root, teamEvent(change), h.state), false, JSON.stringify(change) + ': renderer rejects untrusted or missing counters');
    equal(h.root.firstChild, sentinel, 'Rejected data does not mutate the existing render host');
  }
  const h = fixture();
  equal(h.teamApi.render(h.root, teamEvent({source: 'preview'}), h.state), true, 'Explicit preview fixtures can exercise the same real renderer');
});

test('team comparison lifecycle works without a large-player renderer and never acknowledges native player art', async () => {
  const h = fixture();
  equal(h.api.handleEvent(teamEvent({nativeCardToken: '191', nativeCardOwned: true})), true, 'TEAM_STAT enters the common controller with its real team renderer');
  equal(h.api.snapshot().name, 'team-stat', 'The lifecycle owns the correct team-stat label');
  equal(h.api.snapshot().phase, 'entrance', 'Entrance state is observable before the renderer entrance timer completes');
  yes(h.canvas.classList.contains('cbs-large-stats-active'), 'Large-stat ownership class is active during the insert');
  equal(h.canvas.classList.contains('cbs-team-opening'),h.teamApi.enterMs>0,'The measured team opener owns only its entrance window');
  if (h.teamApi.enterMs > 0) {
    h.advance(h.teamApi.enterMs - 1);
    equal(h.api.snapshot().phase, 'entrance', 'The measured team entrance cannot settle early');
    h.advance(1);
  } else h.advance(0);
  equal(h.api.snapshot().phase, 'hold', 'The actual renderer entrance duration controls the transition');
  yes(!h.canvas.classList.contains('cbs-team-opening'),'The opener releases Eyebar ownership when the table settles');
  yes(h.reports.some(report => report[0] === 'team-stat' && report[1] === 'entrance' && report[3] === h.teamApi.reference), 'Entrance reports the actual team reference, with no CBSLargeStats dependency');
  yes(h.reports.some(report => report[0] === 'team-stat' && report[1] === 'hold' && report[3] === h.teamApi.reference), 'Hold reports the actual team reference');
  equal(h.pendingFrames(), 0, 'Team graphics do not schedule the player-art acknowledgement paint gate');
  equal(h.messages, [], 'Even a supplied player token cannot claim player artwork for a team insert');
  h.advance(2999 - h.teamApi.enterMs); equal(h.api.snapshot().visible, true, 'Configured team lifetime has not expired early');
  h.advance(1); equal(h.api.snapshot().visible, false, 'Measured zero-duration team exit completes at the hold deadline');
  equal(h.canvas.classList.contains('cbs-large-stats-active'), false, 'Exit releases the shared canvas ownership');
  yes(h.reports.some(report => report[0] === 'team-stat' && report[1] === 'settled' && report[3] === h.teamApi.reference), 'Settled report belongs to the actual renderer');
});

test('team-stat cancellation clears pending entrance and hold work across match and presentation changes', async () => {
  for (const reason of ['cancel', 'pause', 'selection', 'match', 'replay', 'play', 'game-end']) {
    const h = fixture(); h.api.handleEvent(teamEvent());
    const before = h.reports.filter(report => report[1] === 'hold').length;
    if (reason === 'cancel') h.api.cancel();
    if (reason === 'pause') h.update({context: {paused: true}});
    if (reason === 'selection') h.update({context: {playSelection: true}});
    if (reason === 'match') h.update({matchId: 'another-match'});
    if (reason === 'replay') h.api.handleEvent({name: 'REPLAY_STARTED'});
    if (reason === 'play') h.api.handleEvent({name: 'PLAY_STARTED'});
    if (reason === 'game-end') h.api.handleEvent({name: 'GAME_ENDED'});
    h.advance(20000); await microtasks(); h.frame(); h.frame();
    equal(h.api.snapshot().visible, false, reason + ': team insert is canceled');
    equal(h.reports.filter(report => report[1] === 'hold').length, before, reason + ': stale entrance never runs after cancellation');
    equal(h.canvas.classList.contains('cbs-large-stats-active'), false, reason + ': no leftover large-stat ownership');
    equal(h.messages, [], reason + ': team cancellation never sends player-card acknowledgements');
  }
  for (const change of [{valid: false}, {context: {paused: true}}, {context: {playSelection: true}}, {context: {periodEnding: true}}]) {
    const h = fixture(); h.update(change); h.api.handleEvent(teamEvent()); h.advance(30000);
    equal(h.api.snapshot().visible, false, 'Disallowed state suppresses the team event before rendering');
    equal(all(h.root, 'cbs-team-table').length, 0, 'Suppressed team event cannot leave hidden stale DOM');
  }
});

test('team replacement invalidates pending player paints and a following player is not canceled by old team timers', async () => {
  const h = fixture(); h.api.handleEvent(h.event('201', {nativeCardOwned: true})); h.advance(440);
  h.api.handleEvent(teamEvent());
  equal(h.hide('201').length, 1, 'Team takeover releases pending native player artwork');
  h.fonts.resolve(); await microtasks(); h.frame(); h.frame();
  equal(h.show('201').length, 0, 'Pending fonts cannot claim a player after the team table replaced it');
  h.advance(0); equal(h.api.snapshot().name, 'team-stat', 'The replacement table owns the lifecycle');
  h.api.handleEvent(h.event('202', {nativeCardOwned: true})); await paint(h); h.advance(10000);
  equal(h.api.snapshot().name, 'player-stat', 'The following native-owned player retains lifecycle ownership');
  equal(h.show('202').length, 1, 'Following player completes normal paint acknowledgement');
  equal(h.hide('202').length, 0, 'A canceled team hold deadline cannot release a following player');
  equal(all(h.root, 'cbs-team-table').length, 0, 'Following player DOM contains no stale team table');
});

test('native DRIVE_SUMMARY requires authoritative plays, yards and elapsed seconds', async () => {
  const h = fixture();
  const e = {name: 'DRIVE_SUMMARY', team: 'away', caption: 'THIS DRIVE', displaySeconds: 2,
    drive: {source: 'game', plays: 7, yards: 61, timeOfPossession: 193, rushPlays: 3, passPlays: 4}};
  equal(h.api.handleEvent(e), true, 'Authoritative native drive is consumed'); await paint(h);
  yes(contents(h.root).includes('THIS DRIVE'), 'The live drive caption is present');
  yes(contents(h.root).includes('3 RUSH, 4 PASS'), 'Complete agreeing play breakdown is shown');
  yes(contents(h.root).includes('61 YARDS, 3:13'), 'Authoritative elapsed seconds render with the actual drive yards');
  equal(h.messages, [], 'Native drive event has no player-art acknowledgement');
  h.advance(3000); equal(h.api.snapshot().visible, false, 'Drive summary follows the common timed lifecycle');
  const zero = fixture();
  zero.api.handleEvent(Object.assign({}, e, {drive: {source: 'game', plays: 1, yards: 0, timeOfPossession: 0}}));
  yes(contents(zero.root).includes('1 PLAYS'), 'Unknown play-type breakdown falls back to the authoritative play count');
  yes(contents(zero.root).includes('0 YARDS, 0:00'), 'Explicit real zero elapsed seconds is permitted');
  const mismatch = fixture();
  mismatch.api.handleEvent(Object.assign({}, e, {drive: Object.assign({}, e.drive, {rushPlays: 2})}));
  yes(contents(mismatch.root).includes('7 PLAYS'), 'A disagreeing play breakdown cannot replace the authoritative play count');
});

test('drive summaries never invent missing time, fake provenance, count or yards', async () => {
  const changes = [
    {timeOfPossession: undefined}, {timeOfPossession: null}, {timeOfPossession: ''}, {timeOfPossession: true},
    {timeOfPossession: -1}, {timeOfPossession: 1.5}, {plays: undefined}, {plays: 0},
    {yards: undefined}, {source: 'estimated'}, {source: undefined}
  ];
  for (const change of changes) {
    const h = fixture(); h.api.handleEvent({name: 'DRIVE_SUMMARY', team: 'away',
      drive: Object.assign({source: 'game', plays: 7, yards: 61, timeOfPossession: 193}, change)});
    await paint(h);
    equal(h.api.snapshot().visible, false, JSON.stringify(change) + ': incomplete native drive is not rendered');
    equal(h.root.children.length, 0, 'Invalid drive does not leave a 0:00 or partial summary');
    equal(h.messages, [], 'Invalid drive never suppresses any native player card');
    yes(h.reports.some(report => report[1] === 'missing-data'), 'Missing authoritative drive data is reported');
  }
});

test('standalone CBS receiver table uses real identity and preserves unknown targets', async () => {
  const h=fixture();
  const e=h.event('',{player:{kind:2,position:'WR',name:'I. Hilliard',fullName:'Ike Hilliard',number:88,receptions:1,receivingYards:17,receivingTouchdowns:0}});
  h.api.handleEvent(e);h.advance(0);
  equal(h.api.snapshot().name,'receiver-stat','WR chooses the separately measured standalone lower-third');
  equal(all(h.root,'cbs-receiver-value').map(n=>n.textContent),['\u2014','1','17','0'],'Missing targets stay unknown while current receiving values survive');
  equal(all(h.root,'cbs-receiver-first')[0].textContent,'Ike','Full first name remains an independent identity field');
  equal(all(h.root,'cbs-receiver-last')[0].textContent,'HILLIARD','Surname treatment follows the source');
  yes(h.canvas.classList.contains('cbs-large-stats-active'),'Standalone table replaces the Eyebar while present');
  equal(h.messages,[],'A normal stat insert never acknowledges ownership of native artwork');
  h.update({context:{scorebugVisible:false}});
  equal(h.api.snapshot().visible,false,'A camera cut suppresses the receiver table');
  yes(!h.canvas.classList.contains('cbs-large-stats-active'),'Cancellation releases the scorebug');
  h.advance(30000);equal(h.api.snapshot().visible,false,'No stale timer revives the table');
});

(async function main() {
  const failures = [];
  for (const item of cases) {
    try { await item.run(); console.log('PASS ' + item.name); }
    catch (error) { failures.push(item.name); console.error('FAIL ' + item.name + '\n' + (error.stack || error)); }
  }
  console.log(checks + ' assertions; ' + (cases.length - failures.length) + '/' + cases.length + ' lifecycle cases passed.');
  if (failures.length) process.exitCode = 1;
})().catch(error => { console.error(error); process.exitCode = 1; });
