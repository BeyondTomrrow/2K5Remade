/* One owner for the package's replay-in/replay-out wipe. This controls only
 * overlay layers; it never pauses football or requests a native camera cut.
 * The curve/light motion is a reconstruction, not a verified NBC 3D asset. */
(function () {
  'use strict';
  var api = window.NFL2K5, wipe = document.getElementById('snf-wipe');
  if (!api || !wipe) return;
  var latest = api.state, current = null, phase = 'hidden', timers = [], matchKey = '';
  function permitted() {
    var s = latest, cx = s && s.context || {};
    return !!(s && s.valid === true && s.away && s.home && s.gameStatus !== 'inactive' &&
      s.gameStatus !== 'final' && !cx.playSelection && !cx.paused);
  }
  function notify() {
    document.dispatchEvent(new CustomEvent('snf-transition-change', { detail:{ active:current, phase:phase } }));
  }
  function setPhase(value) { phase = value; wipe.setAttribute('data-phase', value); }
  function reset() {
    timers.forEach(clearTimeout); timers = [];
    var owned = current !== null;
    current = null; setPhase('hidden'); wipe.classList.remove('on');
    wipe.setAttribute('aria-hidden', 'true');
    wipe.removeAttribute('data-direction'); wipe.removeAttribute('data-reduced-motion');
    if (owned) notify();
  }
  function start(kind) {
    if (!permitted() || current === kind) return;
    timers.forEach(clearTimeout); timers = [];
    current = kind; setPhase('covering');
    var reduced = !!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches &&
      document.documentElement.getAttribute('data-preview-motion') !== 'full');
    wipe.classList.remove('on');
    wipe.setAttribute('data-direction', kind === 'replay-out' ? 'out' : 'in');
    wipe.setAttribute('data-reduced-motion', String(reduced));
    wipe.setAttribute('aria-hidden', 'false');
    void wipe.offsetWidth; wipe.classList.add('on');
    /* Ownership is synchronous: inserts clear before the first paint, and
       later same-batch events cannot reveal a second graphic underneath. */
    notify();
    timers.push(setTimeout(function () { setPhase('covered'); }, reduced ? 40 : 440));
    timers.push(setTimeout(function () { setPhase('revealing'); }, reduced ? 80 : 560));
    timers.push(setTimeout(reset, reduced ? 160 : 1100));
  }
  window.SNFTransitions = { active:function () { return current; }, phase:function () { return phase; }, reset:reset };
  api.onState(function (s) {
    latest = s;
    var key = s.away && s.home ? String(s.matchId || '') + '|' + s.away.abbreviation + '|' + s.home.abbreviation : '';
    if (!permitted() || key && matchKey && key !== matchKey) reset();
    if (key) matchKey = key;
  });
  api.onEvent(function (e) {
    if (!e) return;
    if (e.name === 'REPLAY_STARTED') start('replay-in');
    else if (e.name === 'REPLAY_ENDED') start('replay-out');
    /* Live football can interrupt a transition. Never cover a resumed snap
       just to finish a decorative timeline or restore a canceled insert. */
    else if (['PLAY_STARTED','GAME_ENDED','QUARTER_ENDED','HALFTIME','OVERTIME'].indexOf(e.name) >= 0) reset();
  });
})();
