/* NBC Sunday Night Football: live data, states and animations.
 *
 * Text blocks are placed from the reference's measurements (design pixels:
 * capital/figure top and height, anchor x, and the width the reference
 * string has there). Font size comes from the font's own cap height and the
 * horizontal scale from the reference string, so the layout holds when the
 * stand-in font is replaced by the broadcast font. */
(function () {
  'use strict';
  var bug = document.getElementById('bug');
  var q = function (s) { return bug.querySelector(s); };

  var SPEC = [
    { el: '.away .abbr',   anchor: 'right',  x: 143,   top: 32,   cap: 21,   ref: ['LAR', 75] },
    { el: '.away .record', anchor: 'right',  x: 142.5, top: 57,   cap: 10,   ref: ['10-6', 37] },
    { el: '.away .score',  anchor: 'center', x: 193,   top: 31.5, cap: 35,   ref: ['42', 62.5] },
    { el: '.home .score',  anchor: 'center', x: 379,   top: 31.5, cap: 35,   ref: ['27', 58.5] },
    { el: '.home .abbr',   anchor: 'left',   x: 431,   top: 31.5, cap: 21.5, ref: ['CIN', 65] },
    { el: '.home .record', anchor: 'left',   x: 432,   top: 57,   cap: 10,   ref: ['9-7', 29] },
    /* inside the pod (its box starts at 240, 2.5) */
    { el: '.pod .clock',         anchor: 'center', x: 48.5, top: 36.5, cap: 17,  ref: ['15:00', 76] },
    { el: '.pod .quarter .num',  anchor: 'right',  x: 46.5, top: 62,   cap: 16,  ref: ['2', 16] },
    { el: '.pod .quarter .ord',  anchor: 'left',   x: 46,   top: 62.8, cap: 8.7, ref: ['ND', 19.5], underline: true }
  ];

  var ctx = document.createElement('canvas').getContext('2d');
  function measure(el, size, text) {
    var cs = getComputedStyle(el);
    ctx.font = cs.fontWeight + ' ' + size + 'px ' + cs.fontFamily;
    return ctx.measureText(text);
  }
  function fit(spec) {
    var el = q(spec.el);
    if (!el) return;
    var capPer = measure(el, 100, spec.ref[0]).actualBoundingBoxAscent / 100;
    var size = spec.cap / capPer;
    var r = measure(el, size, spec.ref[0]);
    var sx = spec.ref[1] / (r.actualBoundingBoxLeft + r.actualBoundingBoxRight);
    var m = measure(el, size, el.textContent || ' ');
    var ink = (m.actualBoundingBoxLeft + m.actualBoundingBoxRight) * sx;
    var fm = measure(el, size, 'H');
    var baseline = (size - (fm.fontBoundingBoxAscent + fm.fontBoundingBoxDescent)) / 2 + fm.fontBoundingBoxAscent;
    el.style.fontSize = size + 'px';
    el.style.top = (spec.top + spec.cap - baseline) + 'px';
    var left = spec.anchor === 'left' ? spec.x : spec.anchor === 'right' ? spec.x - ink : spec.x - ink / 2;
    el.style.left = (left + m.actualBoundingBoxLeft * sx) + 'px';
    el.style.transformOrigin = '0 0';
    el.style.transform = 'scaleX(' + sx + ')';
    if (spec.underline) el.style.setProperty('--ul-top', (baseline + 1) + 'px');
  }
  function layout() { SPEC.forEach(fit); }

  /* ---- teams ---- */
  var ALIAS = { STL: 'LAR', SD: 'LAC', OAK: 'LV', JAC: 'JAX', WSH: 'WAS' };
  var COLORS = {
    LAR: '#1f4fae', CIN: '#da541b', ARI: '#97233f', ATL: '#a71930', BAL: '#3b2a8f', BUF: '#00338d', CAR: '#0085ca',
    CHI: '#c83803', CLE: '#ff3c00', DAL: '#003594', DEN: '#fb4f14', DET: '#0076b6', GB: '#2f5a3e', HOU: '#a71930',
    IND: '#0b4a8f', JAX: '#006778', KC: '#e31837', LV: '#6b6f74', LAC: '#0080c6', MIA: '#008e97', MIN: '#4f2683',
    NE: '#0c2f5e', NO: '#9f8958', NYG: '#0b2265', NYJ: '#125740', PHI: '#004c54', PIT: '#d9a300', SF: '#aa0000',
    SEA: '#1d4f91', TB: '#d50a0a', TEN: '#4b92db', WAS: '#7a1a1a'
  };
  var missing = {};
  function code(t) { var a = (t && t.abbreviation || '').toUpperCase(); return ALIAS[a] || a; }
  function logoSrc(t) { var c = code(t); return (!c || missing[c]) ? (t.logo || '') : 'assets/logos/' + c + '.png'; }

  var dirty = false;
  function setText(sel, text) {
    var el = q(sel);
    if (el && el.textContent !== text) { el.textContent = text; dirty = true; }
  }
  function setLogo(img, t) {
    var src = logoSrc(t), c = code(t);
    if (img.getAttribute('src') !== src) img.setAttribute('src', src);
    img.onerror = function () { if (!missing[c]) { missing[c] = true; img.setAttribute('src', t.logo || ''); } };
  }
  function setTeam(side, t, prev) {
    bug.style.setProperty('--' + side, COLORS[code(t)] || t.primaryColor || '#444a55');
    setText('.' + side + ' .abbr', code(t) || '---');
    setText('.' + side + ' .record', t.record || '');
    setText('.' + side + ' .score', t.score == null ? '' : String(t.score));
    if (prev && prev.score !== t.score) pop(side);
    setLogo(q('.' + side + ' .logo'), t);
    var bars = bug.querySelectorAll('.' + side + ' .timeouts i');
    for (var i = 0; i < bars.length; i++) bars[i].classList.toggle('used', i >= (t.timeouts == null ? 3 : t.timeouts));
  }
  function pop(side) {
    var el = q('.' + side + ' .score');
    el.classList.remove('pop'); void el.offsetWidth; el.classList.add('pop');
  }

  function clock(sec) {
    if (sec == null || sec < 0) return '';
    var s = Math.ceil(sec);
    return Math.floor(s / 60) + ':' + ('0' + (s % 60)).slice(-2);
  }
  var ORD = { 1: 'ST', 2: 'ND', 3: 'RD', 4: 'TH' };
  /* "3RD & 8" -> "3rd & 8", "1ST & GOAL" -> "1st & Goal", "KICKOFF" -> "Kickoff" */
  function niceDown(text) {
    return (text || '').toLowerCase().replace(/\b([a-z])/g, function (c) { return c.toUpperCase(); })
      .replace(/(\d)(St|Nd|Rd|Th)\b/g, function (_, d, o) { return d + o.toLowerCase(); }).replace('Pat', 'PAT');
  }

  /* ---- down & distance tab ---- */
  var tab = q('.tab'), tabText = q('.tab-text'), clockBox = q('.playclock');
  var tabSide = null, tabValue = '';
  function setTab(side, text, show) {
    if (!show) { tab.classList.remove('on'); tabSide = null; tabValue = ''; return; }
    if (side !== tabSide) {
      tab.classList.remove('on', 'opening', 'home', 'away');
      void tab.offsetWidth;
      tab.classList.add(side, 'opening');
      tabSide = side;
      tabText.textContent = text; tabValue = text;
      requestAnimationFrame(function () { tab.classList.add('on'); });
      setTimeout(function () { tab.classList.remove('opening'); }, 700);
      return;
    }
    if (text !== tabValue) {            /* new down: fade the text out and back in */
      tabValue = text;
      tabText.classList.add('out');
      setTimeout(function () { tabText.textContent = text; tabText.classList.remove('out'); }, 240);
    }
  }

  /* ---- flag ---- */
  var pod = q('.pod'), flagUntil = 0;
  function updateFlag(on) { pod.classList.toggle('flag', on || Date.now() < flagUntil); }

  /* ---- takeover (touchdown, field goal) ---- */
  var takeoverBusy = false;
  function takeover(word, side) {
    if (takeoverBusy || !NFL2K5.state) return;
    var t = NFL2K5.state[side] || {};
    takeoverBusy = true;
    var box = q('.takeover');
    box.style.setProperty('--glow', COLORS[code(t)] || '#4d7cff');
    q('.takeover .big-word').textContent = word;
    var neon = q('.takeover .neon-logo');
    neon.setAttribute('src', logoSrc(t));
    bug.classList.remove('takeover-on'); void bug.offsetWidth;
    bug.classList.add('takeover-on');
    setTimeout(function () { bug.classList.remove('takeover-on'); takeoverBusy = false; }, 3700);
  }

  NFL2K5.onState(function (s, prev) {
    if (!s.away || !s.home) return;
    setTeam('away', s.away, prev && prev.away);
    setTeam('home', s.home, prev && prev.home);
    setText('.pod .clock', clock(s.gameClock));
    var n = s.quarter || 0;
    setText('.pod .quarter .num', n === 0 ? '' : n <= 4 ? String(n) : 'OT');
    setText('.pod .quarter .ord', n >= 1 && n <= 4 ? ORD[n] : '');
    if (dirty) { layout(); dirty = false; }

    var live = !!(s.context && s.context.scorebugVisible);
    bug.classList.toggle('off-air', !live);
    var down = niceDown(s.downDistanceText);
    setTab(s.possession, down, live && !!s.possession && !!down);
    var pc = s.playClock;
    clockBox.textContent = pc >= 0 ? String(pc) : '';
    clockBox.classList.toggle('on', live && pc >= 0 && !!s.possession);
    clockBox.classList.toggle('low', pc >= 0 && pc <= 5);
    updateFlag(!!(s.context && s.context.flag));
  });

  NFL2K5.onEvent(function (e) {
    switch (e.name) {
    case 'TOUCHDOWN': takeover('TOUCHDOWN', e.team); break;
    case 'FIELD_GOAL': takeover('FIELD GOAL', e.team); break;
    case 'SAFETY': takeover('SAFETY', e.team); break;
    case 'PENALTY': flagUntil = Date.now() + 8000; updateFlag(true); setTimeout(function () { updateFlag(false); }, 8100); break;
    }
  });

  /* ---- previews: scorebug.html?preview=<state> ----
   * reference: the bug exactly where it sits in reference/reference.png
   * (1400x268, scale 1); home-ball / away-ball / flag / touchdown: states
   * for checking the package, on the 1920x1080 canvas. */
  var pv = (/[?&]preview=([\w-]+)/.exec(location.search) || [])[1];
  if (pv) {
    var st = { quarter: 2, gameClock: 900, playClock: 39, possession: 'home', downDistanceText: '3RD & 8',
               context: { scorebugVisible: true, flag: false },
               away: { abbreviation: 'LAR', record: '10-6', score: 42, timeouts: 3 },
               home: { abbreviation: 'CIN', record: '9-7', score: 27, timeouts: 2 } };
    if (pv === 'reference') {
      document.documentElement.style.background = document.body.style.background = '#212121';
      bug.style.setProperty('--bug-scale', '1'); bug.style.setProperty('--bug-x', '413px'); bug.style.setProperty('--bug-y', '90px');
      st.possession = null; st.playClock = -1;
    } else {
      document.documentElement.style.background = document.body.style.background = '#2d4a2f';
    }
    if (pv === 'away-ball') { st.possession = 'away'; st.downDistanceText = '4TH & INCHES'; st.playClock = 35; }
    if (pv === 'flag') st.context.flag = true;
    NFL2K5.dispatch({ type: 'state', state: st });
    if (pv === 'touchdown') setTimeout(function () { NFL2K5.dispatch({ type: 'event', event: { name: 'TOUCHDOWN', team: 'away' } }); }, 300);
  }

  if (document.fonts && document.fonts.ready) document.fonts.ready.then(layout);
  layout();
})();
