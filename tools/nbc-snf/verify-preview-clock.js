/* Focused checks of the actual deterministic preview clock.
 * The fixtures model timer order, animation birth and asynchronous assets;
 * actual WebView2 PNG comparisons remain the visual correctness check. */
'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const source = fs.readFileSync(path.resolve(__dirname,
  '../../mods/presentations/NBC_SNF/preview-clock.js'), 'utf8');
let checks = 0;
function equal(actual, expected, label) {
  assert.deepStrictEqual(actual, expected, label); ++checks;
}
function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return { promise, resolve };
}
function fixture(search = '?preview=fixture&frame=0', fontReady = Promise.resolve()) {
  const animations = [], attrs = {}, load = [], nativeDelays = [];
  let paints = 0;
  const context = {
    URLSearchParams, Promise, console, location: { search }, animations,
    document: {
      documentElement: { setAttribute: (name, value) => { attrs[name] = value; } },
      body: { offsetWidth: 10 }, fonts: { ready: fontReady }, images: [],
      getAnimations: () => animations
    },
    setTimeout: (callback, delay) => {
      nativeDelays.push(delay); setImmediate(callback); return 1;
    },
    requestAnimationFrame: callback => {
      ++paints; setImmediate(() => callback(0)); return 1;
    },
    clearTimeout: () => {},
    addEventListener: (name, callback) => { if (name === 'load') load.push(callback); }
  };
  context.window = context;
  vm.createContext(context);
  const originalTimeout = context.setTimeout;
  const originalDateNow = vm.runInContext('Date.now', context);
  vm.runInContext(source, context, { filename: 'preview-clock.js' });
  return { context, animations, attrs, nativeDelays, originalTimeout, originalDateNow,
    load: () => load.forEach(callback => callback()), paints: () => paints };
}

(async function main() {
  const live = fixture('');
  equal(live.context.NFL2K5PreviewClock, undefined, 'Live pages have no preview API');
  equal(live.context.setTimeout, live.originalTimeout, 'Live timers are unchanged');
  equal(vm.runInContext('Date.now', live.context), live.originalDateNow, 'Live Date.now is unchanged');
  const realtime = fixture('?preview=fixture&motion=full');
  equal(realtime.context.NFL2K5PreviewClock, undefined, 'Realtime preview has no deterministic API');
  equal(realtime.context.setTimeout, realtime.originalTimeout, 'Realtime preview keeps native timers');
  equal(realtime.attrs['data-preview-motion'], 'full', 'Full-motion review flag is retained');

  const fonts = deferred(), f = fixture('?preview=fixture&frame=0', fonts.promise);
  vm.runInContext(`
    var seen = [];
    setTimeout(function () {
      seen.push('a:' + Date.now());
      animations.push({ pause: function () {}, currentTime: 0 });
      setTimeout(function () { seen.push('child:' + Date.now()); }, 0);
    }, 10);
    setTimeout(function () { seen.push('b:' + Date.now()); }, 10);
    var cancelled = setTimeout(function () { seen.push('cancelled'); }, 15);
    clearTimeout(cancelled);
    requestAnimationFrame(function (time) { seen.push('raf:' + time); });
  `, f.context);
  f.load();
  await new Promise(resolve => setImmediate(resolve));
  equal(f.nativeDelays.length, 0, 'Initial seek waits for fonts');
  fonts.resolve();
  await f.context.NFL2K5PreviewClock.ready;
  equal(f.nativeDelays, [100], 'Only initialization uses the native timeout');
  equal(f.attrs['data-preview-frame'], '0', 'Initial requested frame is reached');
  equal(vm.runInContext('Date.now', f.context), f.originalDateNow, 'Date.now restored after initialization');

  await f.context.NFL2K5PreviewClock.captureFrame(10);
  equal(Array.from(f.context.seen), ['a:10', 'b:10', 'child:10'], 'Stable timer order and nested zero-delay callback');
  equal(f.animations[0].currentTime, 0, 'Animation is born at its timer callback');
  equal(f.paints(), 2, 'Capture waits for two native paint frames');
  await f.context.NFL2K5PreviewClock.captureFrame(20);
  equal(f.animations[0].currentTime, 10, 'Animation age advances by virtual elapsed time');
  equal(Array.from(f.context.seen), ['a:10', 'b:10', 'child:10', 'raf:16'], 'RAF has virtual timestamp; cancelled timer stays absent');
  equal(f.attrs['data-preview-frame'], '20', 'Batch capture advances the same document');
  await assert.rejects(f.context.NFL2K5PreviewClock.captureFrame(19), /forward/); ++checks;
  await assert.rejects(f.context.NFL2K5PreviewClock.captureFrame(NaN), /forward/); ++checks;
  await f.context.NFL2K5PreviewClock.captureFrame(20);
  equal(f.animations[0].currentTime, 10, 'Repeated frame does not age animations');
  equal(vm.runInContext('Date.now', f.context), f.originalDateNow, 'Date.now restored after rejected and repeated seeks');

  const image = deferred();
  f.context.image = image;
  vm.runInContext(`setTimeout(function () {
    document.images.push({ decode: function () { return image.promise; } });
  }, 5);`, f.context);
  let captured = false;
  const pending = f.context.NFL2K5PreviewClock.captureFrame(25).then(() => { captured = true; });
  await new Promise(resolve => setImmediate(resolve));
  equal(captured, false, 'New images inserted by a virtual callback delay capture');
  image.resolve(); await pending;
  equal(captured, true, 'Capture completes once the inserted image decodes');
  equal(f.animations[0].currentTime, 15, 'Asset loading does not advance virtual animation age');

  const runaway = fixture(); runaway.load();
  await runaway.context.NFL2K5PreviewClock.ready;
  vm.runInContext('setTimeout(function loop() { setTimeout(loop, 0); }, 1);', runaway.context);
  await assert.rejects(runaway.context.NFL2K5PreviewClock.captureFrame(1), /timer loop/); ++checks;
  equal(vm.runInContext('Date.now', runaway.context), runaway.originalDateNow, 'Runaway timer failure also restores Date.now');
  console.log(`${checks} focused preview-clock checks passed`);
})().catch(error => { console.error(error); process.exitCode = 1; });
