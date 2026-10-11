/* Deterministic preview frames for the existing WebView2 PNG renderer.
 * Only ?preview=...&frame=ms enables this. The live game never changes its
 * timers. Normal preview pages play the exact package in real time. */
(function () {
  'use strict';
  if (!/[?&]preview=/.test(location.search)) return;
  if (new URLSearchParams(location.search).get('motion') === 'full')
    document.documentElement.setAttribute('data-preview-motion', 'full');
  var frame = /[?&]frame=(\d+)/.exec(location.search);
  if (!frame) return;
  var now = 0, next = 1, tasks = new Map(), births = new Map();
  var nativeTimeout = window.setTimeout.bind(window), realDateNow = Date.now;
  window.setTimeout = function (fn, delay) { var id = next++; tasks.set(id,{fn:fn,at:now + Math.max(0,Number(delay) || 0)}); return id; };
  window.clearTimeout = function (id) { tasks.delete(id); };
  window.requestAnimationFrame = function (fn) { return window.setTimeout(function () { fn(now); },16); };
  window.cancelAnimationFrame = window.clearTimeout;
  Date.now = function () { return now; };
  function animate(delta) {
    void document.body.offsetWidth;
    document.getAnimations().forEach(function (a) {
      if (!births.has(a)) births.set(a,now - delta);
      a.pause(); a.currentTime = Math.max(0,now - births.get(a));
    });
  }
  function seek(end) {
    animate(0);
    var count = 0;
    for (;;) {
      var pending = Array.from(tasks.entries()).filter(function (t) { return t[1].at <= end; }).sort(function (a,b) { return a[1].at - b[1].at || a[0] - b[0]; })[0];
      if (!pending) break;
      if (++count > 10000) throw new Error('Preview timer loop');
      var delta = pending[1].at - now; now = pending[1].at; animate(delta);
      tasks.delete(pending[0]); pending[1].fn(); animate(0);
    }
    var tail = end - now; now = end; animate(tail);
    document.documentElement.setAttribute('data-preview-frame',String(end));
    Date.now = realDateNow;
  }
  window.addEventListener('load',function () {
    Promise.all([document.fonts.ready].concat(Array.from(document.images).map(function (img) { return img.decode ? img.decode().catch(function () {}) : Promise.resolve(); }))).then(function () {
      nativeTimeout(function () { seek(Number(frame[1])); },100);
    });
  });
})();
