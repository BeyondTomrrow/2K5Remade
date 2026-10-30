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
  var nativeTimeout = window.setTimeout.bind(window), nativeRaf = window.requestAnimationFrame.bind(window), realDateNow = Date.now;
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
    if (!Number.isFinite(end) || end < now) throw new Error('Preview frames must move forward');
    Date.now = function () { return now; };
    try {
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
    } finally { Date.now = realDateNow; }
  }
  function assetsReady() {
    return Promise.all([document.fonts.ready].concat(Array.from(document.images).map(function (img) {
      return img.decode ? img.decode().catch(function () {}) : Promise.resolve();
    })));
  }
  var resolveReady, rejectReady;
  var ready = new Promise(function (resolve, reject) { resolveReady = resolve; rejectReady = reject; });
  /* This API exists only on deterministic preview pages. Batch captures
     advance the original virtual timers and actual Web Animations rather
     than inventing intermediate images. The real RAF only flushes paint. */
  window.NFL2K5PreviewClock = {
    ready:ready,
    captureFrame:function (end) {
      return ready.then(function () {
        seek(Number(end));
        return assetsReady();
      }).then(function () {
        animate(0);
        return new Promise(function (resolve) {
          nativeRaf(function () { nativeRaf(resolve); });
        });
      });
    }
  };
  window.addEventListener('load',function () {
    Promise.resolve(window.NFL2K5PreviewFixturesReady).then(assetsReady).then(function () {
      nativeTimeout(function () {
        try { seek(Number(frame[1])); resolveReady(); }
        catch (error) { rejectReady(error); }
      },100);
    },rejectReady);
  });
})();
