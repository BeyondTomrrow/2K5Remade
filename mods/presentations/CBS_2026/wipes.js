/* Reference-scoped CBS halftime-report exit, not a generic replay wipe.
 * Inspected source: https://www.youtube.com/watch?v=goBwy8oRNBk
 * NE/DEN AFC Championship halftime, January 2026; 10.81–11.43s.
 * The sponsor phone/3D title introduction is deliberately outside this clip.
 * See docs/CBS-2026-MOTION-REFERENCE-AUDIT-20261016.md for reconstruction limits.
 */
(function () {
  'use strict';
  var canvas = document.getElementById('canvas');
  if (!canvas) return;
  var root = document.createElement('div');
  root.id = 'cbs-wipe';
  root.hidden = true;
  root.setAttribute('aria-hidden', 'true');
  root.innerHTML = '<div class="cbs-wipe-iris">' +
    '<div class="cbs-wipe-band cbs-wipe-white"></div>' +
    '<div class="cbs-wipe-band cbs-wipe-blue"></div>' +
    '<div class="cbs-wipe-band cbs-wipe-red"></div>' +
    '<div class="cbs-wipe-title">HALFTIME<br>REPORT</div>' +
    '<svg class="cbs-wipe-contours" viewBox="0 0 1920 1080" aria-hidden="true">' +
      '<path d="M-360 -40 Q180 540 -360 1120 M360 -40 Q-180 540 360 1120 M1560 -40 Q2100 540 1560 1120 M2280 -40 Q1740 540 2280 1120"/>' +
    '</svg></div>' +
    '<svg class="cbs-wipe-eye-ring" viewBox="0 0 24 24" aria-hidden="true">' +
      '<defs><mask id="cbs-wipe-aperture"><rect width="24" height="24" fill="white"/><circle cx="12" cy="12" r="6.37" fill="black"/></mask></defs>' +
      '<path mask="url(#cbs-wipe-aperture)" fill="white" d="M23.275 12c-.941-2.074-2.4-3.763-4.368-5.064-2.074-1.402-4.378-2.107-6.912-2.107s-4.848.701-6.941 2.107C3.105 8.237 1.66 9.922.72 12c.941 2.074 2.386 3.773 4.334 5.093 2.093 1.382 4.411 2.074 6.941 2.074s4.838-.691 6.912-2.074c1.973-1.32 3.427-3.019 4.368-5.093z"/>' +
      '<circle class="cbs-wipe-orbit" cx="12" cy="12" r="9.8"/>' +
    '</svg><img class="cbs-wipe-settled-eye" src="assets/branding/cbs-eye.svg" alt="">';
  canvas.appendChild(root);

  var generation = 0, timers = [], running = false;
  var reference = 'CBS NE/DEN AFC Championship halftime report, January 2026; goBwy8oRNBk 10.81–11.43s';
  function report(phase, status) {
    root.setAttribute('data-phase', phase);
    if (window.CBSMotion) window.CBSMotion.report('halftime-report-exit', phase, status, reference);
  }
  function allowed(s) {
    var c = s && s.context || {};
    return !!(s && s.valid === true && s.away && s.home && s.gameStatus !== 'inactive' && !c.paused && !c.playSelection);
  }
  function reduced() {
    return window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches &&
      document.documentElement.getAttribute('data-preview-motion') !== 'full';
  }
  function cancel() {
    generation++;
    timers.forEach(clearTimeout);
    timers = [];
    running = false;
    root.hidden = true;
    root.className = '';
    root.setAttribute('data-phase', 'idle');
  }
  function later(ms, fn) {
    var token = generation;
    timers.push(setTimeout(function () { if (token === generation) fn(); }, ms));
  }
  function handleEvent(e, state) {
    if (!e || (e.name !== 'CBS_REFERENCE_WIPE' && e.name !== 'CBS_HALFTIME_REPORT_READY')) return false;
    cancel();
    if (!allowed(state)) { report('suppressed', 'suppressed'); return true; }
    if (reduced()) { report('settled', 'complete'); return true; }
    running = true;
    root.hidden = false;
    void root.offsetWidth;
    root.className = 'running';
    report('expanding-bands', 'running');
    later(250, function () { report('deconstructed-eye', 'running'); });
    later(480, function () { report('reveal', 'running'); });
    later(620, function () { cancel(); report('settled', 'complete'); });
    return true;
  }
  window.CBSWipe = { handleEvent: handleEvent, cancel: cancel, isCovering: function () { return running; }, duration: 620 };
})();
