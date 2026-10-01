/* NBC Sunday Night Football: live data binding and measured text fitting.
 *
 * Text blocks are placed from measurements of reference/reference.png
 * (reference pixels, relative to the bug box at x 413, y 90): the top and
 * height of the capitals/figures, the anchor x, and the ink width the
 * reference string has. Font size is solved from the font's own cap height,
 * and the horizontal scale from the reference string's width, so the layout
 * stays exact when the stand-in font is replaced by the real one. */
(function () {
  'use strict';
  var bug = document.getElementById('bug');
  var q = function (s) { return bug.querySelector(s); };

  /* anchor: 'left' | 'right' | 'center'; x: anchor position; top/cap:
   * capital (or figure) top and height; ref: reference string and its ink
   * width, which sets the horizontal scale for every string in that style. */
  var SPEC = {
    'away-abbr':   { el: '.abbr.away',   anchor: 'right',  x: 143, top: 32, cap: 21, ref: ['LAR', 75] },
    'away-record': { el: '.record.away', anchor: 'right',  x: 142.5, top: 57, cap: 10, ref: ['10-6', 37] },
    'away-score':  { el: '.score.away',  anchor: 'center', x: 193, top: 31.5, cap: 35, ref: ['42', 62.5] },
    'home-score':  { el: '.score.home',  anchor: 'center', x: 379, top: 31.5, cap: 35, ref: ['27', 58.5] },
    'home-abbr':   { el: '.abbr.home',   anchor: 'left',   x: 431, top: 31.5, cap: 21.5, ref: ['CIN', 65] },
    'home-record': { el: '.record.home', anchor: 'left',   x: 432, top: 57, cap: 10, ref: ['9-7', 29] },
    'clock':       { el: '.clock',       anchor: 'center', x: 288.5, top: 39, cap: 17, ref: ['15:00', 76] },
    'q-num':       { el: '.quarter .num', anchor: 'right', x: 286.5, top: 64.5, cap: 16, ref: ['2', 16] },
    'q-ord':       { el: '.quarter .ord', anchor: 'left',  x: 286, top: 65.3, cap: 8.7,  ref: ['ND', 19.5] }
  };

  var ctx = document.createElement('canvas').getContext('2d');
  function fontOf(el) {
    var cs = getComputedStyle(el);
    return { family: cs.fontFamily, weight: cs.fontWeight };
  }
  function metrics(f, size, text) {
    ctx.font = f.weight + ' ' + size + 'px ' + f.family;
    return ctx.measureText(text);
  }
  function fit(spec) {
    var el = q(spec.el);
    if (!el) return;
    var f = fontOf(el);
    /* cap height per px of font size, from the figures/capitals of the
     * reference string itself */
    var m100 = metrics(f, 100, spec.ref[0]);
    var capPer = m100.actualBoundingBoxAscent / 100;
    var size = spec.cap / capPer;
    var mRef = metrics(f, size, spec.ref[0]);
    var refInk = mRef.actualBoundingBoxLeft + mRef.actualBoundingBoxRight;
    var sx = spec.ref[1] / refInk;
    var text = el.textContent;
    var m = metrics(f, size, text);
    var ink = (m.actualBoundingBoxLeft + m.actualBoundingBoxRight) * sx;
    /* element box: line-height 1 => the baseline sits fontAscent below the
     * box top when the line box is the font's ascent+descent; place the box
     * so the cap top lands on spec.top. */
    var fm = metrics(f, size, 'H');
    var asc = fm.fontBoundingBoxAscent, desc = fm.fontBoundingBoxDescent;
    var lineTop = (size - (asc + desc)) / 2;          /* half-leading for line-height 1 */
    var baseline = lineTop + asc;
    var capTop = baseline - m.actualBoundingBoxAscent * (spec.ref[0] === text ? 1 : mRef.actualBoundingBoxAscent / Math.max(1e-6, m.actualBoundingBoxAscent));
    el.style.fontSize = size + 'px';
    el.style.top = (spec.top - (baseline - spec.cap)) + 'px';
    var inkLeft = -m.actualBoundingBoxLeft * sx;        /* ink start relative to the pen */
    var left = spec.anchor === 'left' ? spec.x : spec.anchor === 'right' ? spec.x - ink : spec.x - ink / 2;
    el.style.left = (left - inkLeft) + 'px';
    el.style.transformOrigin = '0 0';
    el.style.transform = 'scaleX(' + sx + ')';
    if (spec.el === '.quarter .ord') el.style.setProperty('--ul-top', (baseline + 1) + 'px');
    return capTop;
  }
  function layout() { Object.keys(SPEC).forEach(function (k) { fit(SPEC[k]); }); }

  /* ---- teams ---- */
  var ALIAS = { STL: 'LAR', SD: 'LAC', OAK: 'LV', JAC: 'JAX', WSH: 'WAS' };
  /* Panel colours. LAR and CIN were measured from the reference (full
   * colour and dark tone); the rest use the clubs' primary colours with the
   * same dark tone rule. */
  var COLORS = {
    LAR: ['#1f4fae', '#1a1e2e'], CIN: ['#da541b', '#2a2127'],
    ARI: ['#97233f'], ATL: ['#a71930'], BAL: ['#241773'], BUF: ['#00338d'], CAR: ['#0085ca'], CHI: ['#c83803'],
    CLE: ['#ff3c00'], DAL: ['#003594'], DEN: ['#fb4f14'], DET: ['#0076b6'], GB: ['#203731'], HOU: ['#a71930'],
    IND: ['#002c5f'], JAX: ['#006778'], KC: ['#e31837'], LV: ['#5a5a5a'], LAC: ['#0080c6'], MIA: ['#008e97'],
    MIN: ['#4f2683'], NE: ['#002244'], NO: ['#9f8958'], NYG: ['#0b2265'], NYJ: ['#125740'], PHI: ['#004c54'],
    PIT: ['#ffb612'], SF: ['#aa0000'], SEA: ['#002244'], TB: ['#d50a0a'], TEN: ['#4b92db'], WAS: ['#5a1414']
  };
  /* Logo windows that are cut from the reference (exact for those teams). */
  var EXACT = { 'away:LAR': 'assets/plates/logo_LAR_away.png', 'home:CIN': 'assets/plates/logo_CIN_home.png' };
  var missing = {};

  function darkTone(hex) {
    var n = parseInt(hex.slice(1), 16), r = n >> 16, g = (n >> 8) & 255, b = n & 255;
    function f(c) { return Math.round(c * 0.1 + 26 * 0.9); }
    return '#' + ((1 << 24) | (f(r) << 16) | (f(g) << 8) | f(b)).toString(16).slice(1);
  }
  function teamCode(t) { var a = (t && t.abbreviation || '').toUpperCase(); return ALIAS[a] || a; }

  function setTeam(side, t) {
    var code = teamCode(t);
    var c = COLORS[code] || [t.primaryColor || '#444444'];
    bug.style.setProperty('--' + side + '-team', c[0]);
    bug.style.setProperty('--' + side + '-dark', c[1] || darkTone(c[0]));
    setText('.abbr.' + side, code || '---');
    setText('.record.' + side, t.record || '');
    setText('.score.' + side, t.score == null ? '' : String(t.score));
    var win = q('.logo-window.' + side), plate = win.querySelector('.plate'), logo = win.querySelector('.logo');
    var exact = EXACT[side + ':' + code];
    win.classList.toggle('exact', !!exact);
    if (exact) { if (plate.getAttribute('src') !== exact) plate.src = exact; }
    else {
      var src = missing[code] ? (t.logo || '') : 'assets/logos/' + code + '.png';
      if (logo.getAttribute('src') !== src) logo.src = src;
      logo.onerror = function () { missing[code] = true; if (t.logo) logo.src = t.logo; };
    }
    var bars = win.parentNode.querySelectorAll('.timeouts.' + side + ' i');
    for (var i = 0; i < bars.length; i++) bars[i].classList.toggle('used', i >= (t.timeouts == null ? 3 : t.timeouts));
  }
  var dirty = false;
  function setText(sel, text) {
    var el = q(sel);
    if (el && el.textContent !== text) { el.textContent = text; dirty = true; }
  }
  function clock(seconds) {
    if (seconds == null || seconds < 0) return '';
    var s = Math.ceil(seconds);
    return Math.floor(s / 60) + ':' + ('0' + (s % 60)).slice(-2);
  }
  var ORD = { 1: 'ST', 2: 'ND', 3: 'RD', 4: 'TH' };
  function quarter(n) {
    if (!n) return ['', ''];
    if (n <= 4) return [String(n), ORD[n]];
    return ['OT', ''];
  }

  NFL2K5.onState(function (s) {
    if (!s.away || !s.home) return;
    setTeam('away', s.away);
    setTeam('home', s.home);
    setText('.clock', clock(s.gameClock));
    var qq = quarter(s.quarter);
    setText('.quarter .num', qq[0]);
    setText('.quarter .ord', qq[1]);
    if (dirty) { layout(); dirty = false; }
    bug.classList.toggle('off-air', !(s.context && s.context.scorebugVisible));
  });

  /* Preview against the reference: scorebug.html?preview=reference puts the
   * bug at its place in reference/reference.png (1400x268, background
   * #212121, scale 1) with the reference's own game values. */
  if (/[?&]preview=reference\b/.test(location.search)) {
    document.documentElement.style.background = '#212121';
    document.body.style.background = '#212121';
    bug.style.setProperty('--bug-scale', '1');
    bug.style.setProperty('--bug-x', '413px');
    bug.style.setProperty('--bug-y', '90px');
    document.documentElement.classList.add('preview');
    NFL2K5.dispatch({ type: 'state', state: {
      quarter: 2, gameClock: 900, context: { scorebugVisible: true },
      away: { abbreviation: 'LAR', record: '10-6', score: 42, timeouts: 3 },
      home: { abbreviation: 'CIN', record: '9-7', score: 27, timeouts: 2 } } });
  }

  if (document.fonts && document.fonts.ready) document.fonts.ready.then(layout);
  layout();
})();
