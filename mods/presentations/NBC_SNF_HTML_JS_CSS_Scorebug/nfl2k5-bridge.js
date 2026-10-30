/* Connects this scorebug to live NFL 2K5 data (Presentation API v1).
 * scorebug.js keeps its own demo state and render function; this file only
 * replaces the demo values with the game's and hides the bug when the game
 * says it should be off screen. Loaded after scorebug.js. */
(function () {
  'use strict';
  if (!window.NFL2K5 || typeof renderScorebug !== 'function' || typeof state === 'undefined') return;

  /* NFL 2K5 uses 2004 team codes; this package's logo files use current ones. */
  var ALIAS = { STL: 'LAR', SD: 'LAC', OAK: 'LV', JAC: 'JAX', WSH: 'WAS' };
  var ORD = ['', '1ST', '2ND', '3RD', '4TH'];
  var missing = {};   /* package logo files that do not exist */

  function packageLogo(abbr, fallback) {
    var code = ALIAS[abbr] || abbr;
    if (!code || missing[code]) return fallback || '';
    return 'assets/logos/' + code + '.png';
  }
  /* A missing package logo falls back to the game's own team logo. */
  ['.away-logo', '.home-logo'].forEach(function (sel) {
    var img = document.querySelector(sel);
    if (!img) return;
    img.addEventListener('error', function () {
      var m = /assets\/logos\/(\w+)\.png$/.exec(img.getAttribute('src') || '');
      if (!m) return;
      missing[m[1]] = true;
      var side = sel === '.away-logo' ? 'away' : 'home';
      var game = NFL2K5.state && NFL2K5.state[side] && NFL2K5.state[side].logo;
      if (game) { state[side].logo = game; img.src = game; }
    });
  });

  function mix(hex, toward, amount) {
    var m = /^#?([0-9a-f]{6})$/i.exec(hex || '');
    if (!m) return hex;
    var n = parseInt(m[1], 16), r = n >> 16, g = (n >> 8) & 255, b = n & 255;
    function f(c) { return Math.round(c + (toward - c) * amount); }
    return '#' + ((1 << 24) | (f(r) << 16) | (f(g) << 8) | f(b)).toString(16).slice(1);
  }
  function team(t) {
    var primary = t.primaryColor || '#333333';
    return {
      abbr: t.abbreviation || '',
      record: t.record || '',
      score: t.score,
      primary: primary,
      light: mix(primary, 255, 0.28),
      dark: mix(primary, 0, 0.5),
      logo: packageLogo(t.abbreviation, t.logo)
    };
  }
  function clock(seconds) {
    if (seconds == null || seconds < 0) return '';
    var s = Math.ceil(seconds);
    return Math.floor(s / 60) + ':' + ('0' + (s % 60)).slice(-2);
  }
  function quarter(q) {
    if (!q) return '';
    return q <= 4 ? ORD[q] : (q === 5 ? 'OT' : (q - 4) + 'OT');
  }

  NFL2K5.onState(function (s) {
    if (!s.away || !s.home) return;
    state.away = Object.assign({}, state.away, team(s.away));
    state.home = Object.assign({}, state.home, team(s.home));
    state.clock = clock(s.gameClock);
    state.quarter = quarter(s.quarter);
    state.timeouts.away = s.away.timeouts || 0;
    state.timeouts.home = s.home.timeouts || 0;
    renderScorebug();
    var bug = document.getElementById('scorebug');
    if (bug) bug.classList.toggle('off-air', !(s.context && s.context.scorebugVisible));
  });
})();
