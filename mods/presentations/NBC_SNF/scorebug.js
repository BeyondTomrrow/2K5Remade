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
    { el: '.away .record', anchor: 'right',  x: 143,   top: 57,   cap: 10,   ref: ['10-6', 37] },
    { el: '.away .score',  anchor: 'center', x: 193,   top: 31.5, cap: 35,   ref: ['42', 62.5] },
    { el: '.home .score',  anchor: 'center', x: 379,   top: 31.5, cap: 35,   ref: ['27', 58.5] },
    { el: '.home .abbr',   anchor: 'left',   x: 430,   top: 30.5, cap: 21.5, ref: ['CIN', 65] },
    { el: '.home .record', anchor: 'left',   x: 432,   top: 57,   cap: 10,   ref: ['9-7', 29] },
    /* inside the pod (its box starts at 240, 2.5) */
    { el: '.pod .clock',         anchor: 'center', x: 48, top: 36.5, cap: 17,  ref: ['15:00', 76] },
    { el: '.pod .quarter .num',  anchor: 'right',  x: 43, top: 63,   cap: 15,  ref: ['2', 12.5] },
    { el: '.pod .quarter .ord',  anchor: 'left',   x: 43,   top: 63.8, cap: 8.7, ref: ['ND', 23], underline: true, underlineOffset: 0 }
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
    if (spec.el.indexOf('.score') >= 0) {
      el.style.backgroundPosition = '0 ' + (baseline - spec.cap) + 'px';
      el.style.backgroundSize = '100% ' + spec.cap + 'px';
    }
    if (spec.underline) el.style.setProperty('--ul-top', (baseline + (spec.underlineOffset == null ? 1 : spec.underlineOffset)) + 'px');
  }
  /* The reference has proportional numeric advances. Stretching the whole
     string to its total width made 1 too broad, pushed the following 5/0
     right, and drew the colon too low. Keep the measured cap height and
     anchor, then fit each glyph to that role's observed advances. */
  var CLOCK_WIDTH={ '0':20, '1':11, '5':18, ':':5 };
  var CLOCK_ADVANCE={ '0':19, '1':12, '5':20, ':':7 };
  var RECORD_WIDTH={ '0':11.5, '1':5.5, '5':10.5, '6':11.5, '7':7, '9':10.5, '-':5.5 };
  var RECORD_ADVANCE={ '0':13, '1':7, '5':12, '6':13, '7':9, '9':13, '-':7 };
  function fitNumeric(spec,widths,advances,defaultWidth,defaultAdvance,maxWidth) {
    var el=q(spec.el), text=el.textContent, size=parseFloat(el.style.fontSize);
    if (!text) { el.textContent=''; return; }
    var fm=measure(el,size,'H'), baseline=(size-(fm.fontBoundingBoxAscent+fm.fontBoundingBoxDescent))/2+fm.fontBoundingBoxAscent;
    var cursor=0, glyphs=[];
    Array.from(text).forEach(function(ch) {
      var m=measure(el,size,ch), width=widths[ch]==null?defaultWidth:widths[ch];
      glyphs.push({ch:ch,x:cursor,m:m,width:width});
      cursor+=advances[ch]==null?defaultAdvance:advances[ch];
    });
    var last=glyphs[glyphs.length-1], ink=last.x+last.width, scale=Math.min(1,maxWidth/ink);
    el.style.left=(spec.anchor==='right'?spec.x-ink*scale:spec.anchor==='center'?spec.x-ink*scale/2:spec.x)+'px';
    el.style.width=ink+'px'; el.style.transform='scaleX('+scale+')'; el.textContent='';
    glyphs.forEach(function(g) {
      var span=document.createElement('span'); span.className='numeric-glyph'; span.textContent=g.ch;
      if (spec.el==='.pod .clock' && g.ch===':') {
        span.classList.add('clock-colon');span.style.left=g.x+'px';span.style.top=(baseline-spec.cap)+'px';
      } else {
        var sx=g.width/Math.max(.01,g.m.actualBoundingBoxLeft+g.m.actualBoundingBoxRight);
        span.style.left=(g.x+g.m.actualBoundingBoxLeft*sx)+'px';span.style.transform='scaleX('+sx+')';
        if(spec.el==='.pod .clock') {
          span.style.backgroundPosition='0 '+(baseline-spec.cap)+'px';
          span.style.backgroundSize='100% '+spec.cap+'px';
        }
      }
      el.appendChild(span);
    });
  }
  function layout() {
    SPEC.forEach(fit);
    fitNumeric(SPEC[6],CLOCK_WIDTH,CLOCK_ADVANCE,19,20,77);
    fitNumeric(SPEC[1],RECORD_WIDTH,RECORD_ADVANCE,10.5,13,76);
  }

  /* ---- teams ---- */
  var ALIAS = { STL: 'LAR', SD: 'LAC', OAK: 'LV', JAC: 'JAX', WSH: 'WAS' };
  var COLORS = {
    LAR: '#1f4fae', CIN: '#da541b', ARI: '#97233f', ATL: '#a71930', BAL: '#3b2a8f', BUF: '#00338d', CAR: '#0085ca',
    CHI: '#c83803', CLE: '#ff3c00', DAL: '#003594', DEN: '#fb4f14', DET: '#0076b6', GB: '#2f5a3e', HOU: '#a71930',
    IND: '#0b4a8f', JAX: '#006778', KC: '#e31837', LV: '#6b6f74', LAC: '#0080c6', MIA: '#008e97', MIN: '#4f2683',
    NE: '#0c2f5e', NO: '#9f8958', NYG: '#0b2265', NYJ: '#125740', PHI: '#004c54', PIT: '#d9a300', SF: '#aa0000',
    SEA: '#1d4f91', TB: '#d50a0a', TEN: '#4b92db', WAS: '#7a1a1a'
  };
  var missing = {}, missingPlates = {};
  var latestState = null, matchKey = '', noticeTimer = 0, wipeTimer = 0, flagTimer = 0;
  var notice = document.getElementById('snf-notice'), wipe = document.getElementById('snf-wipe');
  function escapeText(t) { return String(t == null ? '' : t).replace(/[&<>"']/g, function (c) { return { '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;' }[c]; }); }
  function teamFor(e) { return e.teamData || (latestState && latestState[e.team]) || null; }
  function rgbaTeam(t) {
    var c = color(t).replace('#', '');
    return /^[0-9a-f]{6}$/i.test(c) ? [parseInt(c.slice(0,2),16),parseInt(c.slice(2,4),16),parseInt(c.slice(4,6),16)].join(',') : '70,78,88';
  }
  function duration(e, fallback) { var n = Number(e.duration || e.displaySeconds || (e.player && e.player.displaySeconds)); return n > 0 ? Math.min(n * 1000, 30000) : fallback; }
  function surname(t) { return String(t || '').trim().split(/\s+/).pop().replace(/[^a-z]/gi,'').toLowerCase(); }
  function cleanPlayer(raw) {
    var p = Object.assign({}, raw);
    /* Earlier native paths sometimes appended the cached QB identity to a
       receiver event. Do not show that QB's face/number under another name. */
    if (p.name && p.fullName && surname(p.name) !== surname(p.fullName)) {
      p.fullName = ''; p.portrait = ''; p.number = null; p.position = '';
    }
    return p;
  }
  function code(t) { var a = (t && t.abbreviation || '').toUpperCase(); return ALIAS[a] || a; }
  function color(t) { return (t && t.primaryColor) || COLORS[code(t)] || '#444a55'; }
  function logoSrc(t) { var c = code(t); return (!c || missing[c]) ? ((t && t.logo) || '') : 'assets/logos/' + c + '.png'; }
  function setLogo(img, t) {
    var src = logoSrc(t), c = code(t);
    img.onerror = function () { if (!missing[c]) { missing[c] = true; img.setAttribute('src', (t && t.logo) || ''); } else img.removeAttribute('src'); };
    if (img.getAttribute('src') !== src) img.setAttribute('src', src);
  }
  function setScorebugLogo(side, t) {
    var win=q('.half.'+side+' .logo-window'), img=win.querySelector('.logo'), c=code(t);
    var plate=side==='away' && c==='LAR' ? 'logo_LAR_away' : side==='home' && c==='CIN' ? 'logo_CIN_home' : '';
    if(missingPlates[plate])plate='';
    win.classList.toggle('reference-logo',!!plate);
    win.classList.toggle('reference-cin',plate==='logo_CIN_home');
    if (!plate) { setLogo(img,t); return; }
    img.onerror=function () {
      missingPlates[plate]=true;
      win.classList.remove('reference-logo','reference-cin');
      setLogo(img,t);
    };
    var src='assets/plates/'+plate+'.png';
    if(img.getAttribute('src')!==src)img.setAttribute('src',src);
  }

  var dirty = false;
  function setText(sel, text) {
    var el = q(sel);
    if (el && el.textContent !== text) { el.textContent = text; dirty = true; }
  }
  function setTeam(side, t, prev) {
    bug.style.setProperty('--' + side, color(t));
    setText('.' + side + ' .abbr', code(t) || '---');
    setText('.' + side + ' .record', t.record || '');
    setText('.' + side + ' .score', t.score == null ? '' : String(t.score));
    if (prev && prev.score !== t.score && !busy) pop(side);
    setScorebugLogo(side, t);
    var bars = bug.querySelectorAll('.' + side + ' .timeouts i');
    for (var i = 0; i < bars.length; i++) bars[i].classList.toggle('used', i >= (t.timeouts == null ? 3 : t.timeouts));
  }
  function pop(side) {
    var el = q('.half.' + side + ' .score');
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
      tab.classList.remove('on', 'home', 'away');
      void tab.offsetWidth;
      tab.classList.add(side);
      tabSide = side;
      tabText.textContent = text; tabValue = text;
      requestAnimationFrame(function () { tab.classList.add('on'); });
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

  /* ---- 2022 SNF player-stat insert ----------------------------------
   * The player/stat trigger is a compact lower third.  It is not the large
   * editorial feature page below: that remains PLAYER_FEATURE. */
  var qbStat = document.getElementById('qb-stat'), qbStatData = null, qbTimer = 0, qbVisible = false, broadcastLive = false;
  function qsq(s) { return qbStat.querySelector(s); }
  /* Unknown values are not zero-stat performances. Keep explicitly supplied
     zeroes, and derive rates only when every input is available. */
  function statNumber(v) {
    if (typeof v !== 'number' && typeof v !== 'string') return null;
    if (typeof v === 'string' && !v.trim()) return null;
    var n = Number(v);
    return Number.isFinite(n) ? n : null;
  }
  function statValue(v) {
    if (typeof v === 'number') return Number.isFinite(v) ? String(v) : '—';
    return typeof v === 'string' && v.trim() ? v : '—';
  }
  function statNumeric(v) { var n = statNumber(v); return n == null ? '—' : String(n); }
  function statPair(a, b, separator) { return statNumeric(a) + (separator || '/') + statNumeric(b); }
  function statAverage(total, count, supplied) {
    var explicit = statNumber(supplied), t = statNumber(total), c = statNumber(count);
    if (explicit != null) return explicit.toFixed(1);
    if (t == null || c == null || c < 0) return '—';
    var average = c > 0 ? t / c : t === 0 ? 0 : null;
    return average != null && Number.isFinite(average) ? average.toFixed(1) : '—';
  }
  function suppliedFields(fields, limit) {
    return fields.filter(function (f) { return f && typeof f.label === 'string' && f.label.trim(); })
      .slice(0, limit).map(function (f) { return [f.label, statValue(f.value)]; });
  }
  function isDefensivePosition(pos) { return /^(DEF|CB|SS|FS|S|DB|LB|ILB|OLB|MLB|DE|DT|NT|DL)$/.test(pos); }
  function statCells(p) {
    var pos = playerCardPosition(p), n = statNumeric;
    if (Array.isArray(p.fields)) return suppliedFields(p.fields, 6);
    if (pos === 'QB') return [['COMP', n(p.completions)], ['ATT', n(p.attempts)], ['YDS', n(p.passingYards)], ['TD/INT', statPair(p.passingTouchdowns, p.interceptions)], ['RATING', passerRating(p)]];
    if (pos === 'RB' || pos === 'FB') return [['CAR', n(p.carries)], ['YDS', n(p.rushingYards)], ['TD', n(p.rushingTouchdowns)], ['AVG', statAverage(p.rushingYards, p.carries, p.yardsPerCarry)]];
    if (pos === 'WR' || pos === 'TE') return [['REC', n(p.receptions)], ['YDS', n(p.receivingYards)], ['TD', n(p.receivingTouchdowns)], ['AVG', statAverage(p.receivingYards, p.receptions, p.yardsPerCatch)]];
    if (pos === 'P') return [['PUNTS', n(p.punts)], ['AVG', n(p.puntAverage)], ['LONG', n(p.longestPunt)], ['IN 20', n(p.puntsInside20)]];
    if (pos === 'K') return [['FG', statPair(p.fieldGoalsMade, p.fieldGoalsAttempted)], ['LONG', n(p.longestFieldGoal)], ['PTS', n(p.points)], ['XP', n(p.extraPoints)]];
    if (isDefensivePosition(pos)) return [['TKL', n(p.tackles)], ['SACK', n(p.sacks)], ['INT', n(p.defensiveInterceptions)], ['FF', n(p.forcedFumbles)]];
    return [['STATS', '—']];
  }
  function setPlayerStat(e) {
    if (!e || !e.player || !(e.player.name || e.player.fullName)) return false;
    var p = cleanPlayer(e.player);
    qbStatData = p;
    qsq('.qs-name').textContent = (p.fullName || p.name || '').toUpperCase();
    qsq('.qs-position').textContent = playerCardPosition(p) + (p.number != null ? '\n' + p.number : '');
    qsq('.qs-season').textContent = e.caption || p.caption || p.season || (p.scope === 'season' ? 'THIS SEASON' : 'TODAY');
    var rawTopic = e.topic || p.topic, rawNote = e.note || p.note;
    var topic = typeof rawTopic === 'string' ? rawTopic.trim() : '', note = typeof rawNote === 'string' ? rawNote.trim() : '';
    qsq('.qs-topic').textContent = topic;
    qsq('.qs-note').textContent = note;
    qbStat.classList.toggle('with-topic', !!topic);
    qbStat.classList.toggle('with-note', !!note);
    qsq('.qs-fields').innerHTML = statCells(p).map(function (data) { return '<span><b>' + escapeText(data[0]) + '</b><strong>' + escapeText(data[1]) + '</strong></span>'; }).join('');
    var portrait = qsq('.qs-portrait');
    portrait.onerror = function () { qbStat.classList.add('no-photo'); portrait.removeAttribute('src'); };
    if (p.portrait) portrait.src = p.portrait; else portrait.removeAttribute('src');
    qbStat.classList.toggle('no-photo', !p.portrait);
    var team = teamFor(e);
    var teamLogo = qsq('.qs-team-logo');
    setLogo(teamLogo, team);
    qbStat.style.setProperty('--qs-team', color(team));
    qbStat.style.setProperty('--qs-rgb', rgbaTeam(team));
    return true;
  }

  var playerFeature = document.getElementById('player-feature'), featureTimer = 0;
  function passerRating(p) {
    var supplied = statNumber(p.passerRating != null ? p.passerRating : p.rating);
    if (supplied != null) return supplied.toFixed(1);
    var a = statNumber(p.attempts), c = statNumber(p.completions), yards = statNumber(p.passingYards);
    var td = statNumber(p.passingTouchdowns), interceptions = statNumber(p.interceptions);
    if (a == null || c == null || yards == null || td == null || interceptions == null ||
        a < 0 || c < 0 || c > a || td < 0 || interceptions < 0) return '—';
    if (!a) return c === 0 && yards === 0 && td === 0 && interceptions === 0 ? '0.0' : '—';
    var x = Math.max(0, Math.min(2.375, (c / a - .3) * 5));
    var y = Math.max(0, Math.min(2.375, (yards / a - 3) * .25));
    var z = Math.max(0, Math.min(2.375, (td / a) * 20));
    var w = Math.max(0, Math.min(2.375, 2.375 - (interceptions / a) * 25));
    return (((x + y + z + w) / 6) * 100).toFixed(1);
  }
  function playerCardRows(p) {
    var pos = playerCardPosition(p), n = statNumeric;
    if (Array.isArray(p.fields)) return suppliedFields(p.fields, 5);
    if (pos === 'QB') return [
      ['COMPLETIONS / ATTEMPTS', statPair(p.completions, p.attempts, ' / ')],
      ['PASSING YARDS', n(p.passingYards)], ['TOUCHDOWNS', n(p.passingTouchdowns)],
      ['INTERCEPTIONS', n(p.interceptions)], ['QB RATING', passerRating(p)]
    ];
    if (pos === 'RB' || pos === 'FB') return [
      ['CARRIES', n(p.carries)], ['RUSHING YARDS', n(p.rushingYards)],
      ['RUSHING TOUCHDOWNS', n(p.rushingTouchdowns)], ['YARDS PER CARRY', statAverage(p.rushingYards, p.carries, p.yardsPerCarry)],
      ['LONGEST RUN', n(p.longestRun)]
    ];
    if (pos === 'WR' || pos === 'TE') return [
      ['RECEPTIONS', n(p.receptions)], ['RECEIVING YARDS', n(p.receivingYards)],
      ['RECEIVING TOUCHDOWNS', n(p.receivingTouchdowns)], ['YARDS PER CATCH', statAverage(p.receivingYards, p.receptions, p.yardsPerCatch)],
      ['LONGEST RECEPTION', n(p.longestReception)]
    ];
    if (pos === 'P') return statCells(p);
    if (pos === 'K') return [
      ['FIELD GOALS', statPair(p.fieldGoalsMade, p.fieldGoalsAttempted, ' / ')],
      ['LONGEST FIELD GOAL', n(p.longestFieldGoal)], ['POINTS', n(p.points)], ['EXTRA POINTS', n(p.extraPoints)], ['KICKOFFS', n(p.kickoffs)]
    ];
    if (isDefensivePosition(pos)) return [
      ['TACKLES', n(p.tackles)], ['SACKS', n(p.sacks)], ['INTERCEPTIONS', n(p.defensiveInterceptions)],
      ['FORCED FUMBLES', n(p.forcedFumbles)], ['DEFENSIVE TOUCHDOWNS', n(p.defensiveTouchdowns)]
    ];
    return [];
  }
  function playerCardPosition(p) {
    var pos = String(p.position || '').toUpperCase();
    if (/^(C|G|T|OG|OT|LG|RG|LT|RT|OL|LS)$/.test(pos)) return pos;
    var kinds = ['QB','RB','WR','DEF','K'], kind = statNumber(p.kind);
    if (kind != null && Number.isInteger(kind) && kinds[kind]) {
      var kindPos = kinds[kind];
      var family = pos === 'FB' ? 'RB' : pos === 'TE' ? 'WR' : /^(CB|SS|FS|DB|LB|ILB|OLB|DE|DT|NT)$/.test(pos) ? 'DEF' : pos;
      if (family !== kindPos && pos !== 'P') return kindPos;
    }
    if (pos) return pos;
    function hasAny(keys) { return keys.some(function (k) { return statNumber(p[k]) != null; }); }
    if (hasAny(['attempts','completions','passingYards'])) return 'QB';
    if (hasAny(['carries','rushingYards'])) return 'RB';
    if (hasAny(['receptions','receivingYards'])) return 'WR';
    if (hasAny(['fieldGoalsMade','fieldGoalsAttempted'])) return 'K';
    if (hasAny(['tackles','sacks','defensiveInterceptions'])) return 'DEF';
    return '';
  }
  function showPlayerFeature(e) {
    if (!e || !e.player || !(e.player.name || e.player.fullName)) return;
    var p = cleanPlayer(e.player), team = teamFor(e);
    clearTimeout(featureTimer);
    playerFeature.querySelector('.pf-name').textContent = p.fullName || p.name || '';
    var surname = (p.fullName || p.name || '').trim().split(/\s+/).pop().toUpperCase();
    playerFeature.querySelector('.pf-title').textContent = e.title || p.title || surname + ' GAME STATS';
    playerFeature.querySelector('.pf-kicker').textContent = e.subtitle || p.subtitle || 'TODAY';
    playerFeature.querySelector('.pf-position').textContent = playerCardPosition(p) + (p.number != null ? '  |  #' + p.number : '');
    playerFeature.style.setProperty('--pf-team', color(team));
    playerFeature.style.setProperty('--pf-rgb', rgbaTeam(team));
    var suppliedRows = Array.isArray(e.rows);
    var compare = suppliedRows && Array.isArray(e.columns) && e.columns.length === 2 &&
      e.columns.every(function (column) { return typeof column === 'string' && column.trim(); });
    playerFeature.classList.toggle('single-column', !compare);
    var headers = playerFeature.querySelector('.pf-head');
    headers.querySelector('b').textContent = compare ? e.columns[0] : (e.valueCaption || 'GAME');
    headers.querySelector('em').textContent = compare ? e.columns[1] : '';
    var rows = playerCardRows(p), cells = playerFeature.querySelectorAll('.pf-table > div:not(.pf-head)');
    cells.forEach(function (row, i) {
      var custom = suppliedRows && e.rows[i], values;
      if (custom && typeof custom.label === 'string' && custom.label.trim()) {
        values = Array.isArray(custom.values) ? custom.values : [custom.value];
      } else custom = null;
      /* Supplied rows may contain meaningful single-column, historical or
         non-QB data. A malformed row stays unavailable; never replace it
         with unrelated current-game statistics. */
      var data = suppliedRows ? custom ? [custom.label, statValue(values[0])] : ['—', '—'] : rows[i] || ['—', '—'];
      row.querySelector('span').textContent = data[0];
      row.querySelector('b').textContent = data[1];
      row.querySelector('em').textContent = compare ? custom ? statValue(values[1]) : '—' : '';
    });
    var portrait = playerFeature.querySelector('.pf-portrait');
    portrait.onerror = function () { playerFeature.classList.add('no-photo'); portrait.removeAttribute('src'); };
    if (p.portrait) portrait.src = p.portrait; else portrait.removeAttribute('src');
    playerFeature.classList.toggle('no-photo', !p.portrait);
    var logo = playerFeature.querySelector('.pf-logo'); if (team) setLogo(logo, team); else logo.removeAttribute('src');
    playerFeature.classList.remove('on'); void playerFeature.offsetWidth; playerFeature.classList.add('on');
    refreshVisibility();
    featureTimer = setTimeout(function () {
      playerFeature.classList.remove('on');
      refreshVisibility();
    }, duration(e, 6500));
  }

  /* ---- scoring sequence (touchdown / field goal / safety) ----
   * split -> neon word -> chrome word -> collapse into the pod -> scoring
   * drive bar (score ticks up) -> back to the scorebug. ~10.5 s; the
   * scorebug stays on screen for all of it. */
  var busy = false, timers = [], scoreQueue = [], recentPoints = { away: 0, home: 0 }, lastDrive = null;
  var takeoverEl = q('.takeover');
  function at(ms, fn) { timers.push(setTimeout(fn, ms)); }
  function fmtTop(s) { if (s == null) return '–'; s = Math.round(s); return Math.floor(s / 60) + ':' + ('0' + (s % 60)).slice(-2); }
  function showDrive(d) {
    q('.drive-bar .plays .val').textContent = d && d.plays != null ? String(d.plays) : '–';
    q('.drive-bar .yds .val').textContent = d && d.yards != null ? String(d.yards) : '–';
    q('.drive-bar .top .val').textContent = d ? fmtTop(d.timeOfPossession) : '–';
  }
  function scoring(word, side, drive) {
    var s = NFL2K5.state;
    if (!broadcastLive || !s || !s[side]) return;
    if (busy) { if (scoreQueue.length < 3) scoreQueue.push([word,side,drive]); return; }
    if (luOn) luEnd();
    clearTimeout(featureTimer); playerFeature.classList.remove('on');
    clearTimeout(qbTimer); qbVisible = false;
    busy = true;
    refreshVisibility();
    var t = s[side], other = side === 'away' ? 'home' : 'away', finalScore = t.score;
    bug.classList.remove('player-drive');
    q('.drive-bar .drive-title').textContent = 'SCORING DRIVE';
    q('.drive-bar .plays .lbl').textContent = 'PLAYS';q('.drive-bar .yds .lbl').textContent = 'YDS';q('.drive-bar .top .lbl').textContent = 'T.O.P';
    bug.style.setProperty('--g', color(t));
    q('.takeover .big-word').textContent = word;
    q('.takeover .neon-logo').style.setProperty('--logo', 'url("' + logoSrc(t) + '")');
    /* stack: the scoring team's old score first, it ticks up later */
    ['away', 'home'].forEach(function (k) {
      setLogo(q('.drive-stack .row.' + k + ' .logo'), s[k]);
      var pts = s[k].score - (k === side ? recentPoints[side] : 0);
      q('.drive-stack .row.' + k + ' .pts').textContent = String(pts);
    });
    recentPoints[side] = 0;
    setLogo(q('.drive-bar .drive-logo .logo'), t);
    showDrive(drive);
    var withDrive = word === 'TOUCHDOWN' || word === 'FIELD GOAL';

    pod.classList.add('glow');
    bug.classList.add('split');
    at(450, function () { bug.classList.add('wordphase'); takeoverEl.classList.add('on', 'neon'); });
    at(1500, function () { takeoverEl.classList.add('chrome'); });
    at(3300, function () { takeoverEl.classList.add('leaving'); });
    if (withDrive) {
      at(3700, function () { takeoverEl.className = 'takeover'; bug.classList.remove('wordphase'); bug.classList.add('compact'); });
      at(4300, function () { bug.classList.add('stats'); });
      at(5400, function () {
        var el = q('.drive-stack .row.' + side + ' .pts');
        el.textContent = String(finalScore);
        el.classList.remove('tick'); void el.offsetWidth; el.classList.add('tick');
      });
      at(9000, function () { bug.classList.add('closing'); });
      at(9900, function () { bug.classList.remove('compact', 'stats', 'closing'); pod.classList.remove('glow'); bug.classList.remove('split'); });
      at(10600, function () { finishScoring(side); });
    } else {
      at(3700, function () { takeoverEl.className = 'takeover'; bug.classList.remove('wordphase', 'split'); pod.classList.remove('glow'); });
      at(4300, function () { finishScoring(side); });
    }
  }
  function finishScoring(side) {
    busy = false; timers = []; refreshVisibility();
    var next = scoreQueue.shift(); if (next && broadcastLive) scoring(next[0],next[1],next[2]);
  }
  function drivePlayer(e) {
    if (!broadcastLive || busy || luOn || !e.player) return;
    var p = cleanPlayer(e.player), team = teamFor(e), s = latestState;
    if (!(p.name || p.fullName) || !team) return;
    clearTimeout(qbTimer);qbVisible=false;clearTimeout(featureTimer);playerFeature.classList.remove('on');
    busy=true;bug.classList.add('player-drive','split');bug.style.setProperty('--g',color(team));
    ['away','home'].forEach(function (side) { setLogo(q('.drive-stack .row.'+side+' .logo'),s[side]);q('.drive-stack .row.'+side+' .pts').textContent=String(s[side].score); });
    var image=q('.drive-player .portrait');image.onerror=function(){image.style.visibility='hidden';};image.style.visibility=p.portrait?'':'hidden';if(p.portrait)image.src=p.portrait;else image.removeAttribute('src');
    q('.drive-player .position').textContent=playerCardPosition(p)+(p.number!=null?'\n'+p.number:'');
    var nameParts=(p.fullName||p.name).trim().toUpperCase().split(/\s+/);
    q('.drive-player .name').innerHTML=escapeText(nameParts.shift())+'<br>'+escapeText(nameParts.join(' '));
    q('.drive-bar .drive-title').textContent=e.caption||'THIS DRIVE';
    var fields=statCells(p);if(playerCardPosition(p)==='QB')fields=[['COMP/ATT',p.completions+'/'+p.attempts],['YDS',p.passingYards],['TD',p.passingTouchdowns]];
    ['plays','yds','top'].forEach(function (key,i){var f=fields[i]||['',''];q('.drive-bar .'+key+' .lbl').textContent=f[0];q('.drive-bar .'+key+' .val').textContent=f[1];});
    refreshVisibility();at(420,function(){bug.classList.add('compact');});at(1000,function(){bug.classList.add('stats');});
    at(duration(e,6000),function(){bug.classList.add('closing');});
    at(duration(e,6000)+900,function(){bug.classList.remove('compact','stats','closing','player-drive','split');finishScoring(e.team);});
  }

  /* ---- starting lineup ----
   * The game sends LINEUP after kickoff: the offense of the team with the
   * ball, a few plays later the other team's defense, and the same after the
   * change of possession. Each starter gets the introduction look for
   * PER_PLAYER ms (portrait, position and number, name, college); the bug
   * steps aside meanwhile. */
  var lu = document.getElementById('lineup'), luOn = false, luTimers = [];
  /* Lineup bar colours: [main, second]. Main fills the bar (darkening into
   * the ribbon); second is the swoosh and edge. Teams by current names
   * (ALIAS maps the 2004 abbreviations). */
  var LU_COLORS = {
    ARI: ['#97233f', '#ffb612'], ATL: ['#a71930', '#000000'], BAL: ['#241773', '#9e7c0c'], BUF: ['#00338d', '#c60c30'],
    CAR: ['#0085ca', '#101820'], CHI: ['#0b162a', '#c83803'], CIN: ['#fb4f14', '#000000'], CLE: ['#311d00', '#ff3c00'],
    DEN: ['#fb4f14', '#002244'], DET: ['#0076b6', '#b0b7bc'], GB: ['#203731', '#ffb612'], HOU: ['#03202f', '#a71930'],
    IND: ['#002c5f', '#a2aaad'], JAX: ['#006778', '#d7a22a'], KC: ['#e31837', '#ffb81c'], LV: ['#000000', '#a5acaf'],
    LAC: ['#0080c6', '#ffc20e'], LAR: ['#003594', '#ffa300'], MIA: ['#008e97', '#fc4c02'], MIN: ['#4f2683', '#ffc62f'],
    NE: ['#002244', '#c60c30'], NO: ['#101820', '#d3bc8d'], NYG: ['#0b2265', '#a71930'], NYJ: ['#125740', '#ffffff'],
    PHI: ['#004c54', '#a5acaf'], PIT: ['#101820', '#ffb612'], SF: ['#aa0000', '#b3995d'], SEA: ['#002244', '#69be28'],
    TB: ['#d50a0a', '#34302b'], TEN: ['#0c2340', '#4b92db'], WAS: ['#5a1414', '#ffb612']
  };
  /* Dallas is the reference: its exact colours. */
  var LU_EXACT = { DAL: { top: '#0a2c3e', bot: '#042232', ribbon: '#001421', ink: '#394d58', edge: '#002646', swoosh: '#2f86ff' } };
  function luColors(t) {
    var c = code(t), pair = LU_COLORS[c] || [color(t), '#a5acaf'], exact = LU_EXACT[c] || {};
    lu.style.setProperty('--lu-base', pair[0]);
    lu.style.setProperty('--lu-accent', pair[1]);
    ['top', 'bot', 'ribbon', 'ink', 'edge', 'swoosh'].forEach(function (k) {
      if (exact[k]) lu.style.setProperty('--lu-' + k, exact[k]); else lu.style.removeProperty('--lu-' + k);
    });
  }
  var PER_PLAYER = 2600;
  function luq(sel) { return lu.querySelector(sel); }
  function luClear() { luTimers.forEach(clearTimeout); luTimers = []; }
  function luEnd() {
    luClear();
    lu.classList.remove('on', 'swap');
    luTimers.push(setTimeout(function () { luOn = false; refreshVisibility(); }, 450));
  }
  function luPlayer(p, first) {
    var show = function () {
      var img = luq('.lu-portrait');
      img.onerror = function () { img.style.visibility = 'hidden'; img.removeAttribute('src'); };
      if (p.portrait) { img.style.visibility = ''; img.setAttribute('src', p.portrait); }
      else { img.style.visibility = 'hidden'; img.removeAttribute('src'); }
      luq('.lu-tab .pos').textContent = p.position || '';
      luq('.lu-tab .num').textContent = p.number != null ? String(p.number) : '';
      luq('.lu-name').textContent = p.name || ((p.first || '') + ' ' + (p.last || '')).trim();
      luq('.lu-sub').textContent = p.college || '';
      lu.classList.remove('swap');
    };
    if (first) { show(); return; }
    lu.classList.add('swap');
    luTimers.push(setTimeout(show, 260));
  }
  function lineup(e) {
    var s = NFL2K5.state, t = s && e.team && s[e.team];
    var players = (e.players || []).filter(function (p) { return p && (p.name || p.last); });
    if (!broadcastLive || !t || !players.length || busy || playerFeature.classList.contains('on')) return;
    luClear();
    luColors(t);
    setLogo(luq('.lu-logo .logo'), t);
    setLogo(luq('.lu-ghost .logo'), t);
    var team = ((t.city || '') + ' ' + (t.name || '')).trim().toUpperCase() || code(t);
    var track = luq('.lu-track'), html = '';
    for (var i = 0; i < 16; i++) html += '<span>' + team.replace(/[<&]/g, '') + '</span><i></i>';
    track.innerHTML = html;
    luOn = true;
    bug.classList.add('off-air');
    luPlayer(players[0], true);
    lu.classList.add('on');
    refreshVisibility();
    players.forEach(function (p, i) { if (i) luTimers.push(setTimeout(function () { luPlayer(p, false); }, i * PER_PLAYER)); });
    luTimers.push(setTimeout(luEnd, players.length * PER_PLAYER));
  }

  function matchActive() {
    var s = latestState, cx = s && s.context || {};
    return !!(s && s.valid === true && s.away && s.home && !cx.playSelection && !cx.paused);
  }
  function refreshVisibility() {
    var s = latestState, cx = s && s.context || {};
    broadcastLive = !!(s && s.valid === true && s.away && s.home && cx.scorebugVisible === true && !cx.playSelection && !cx.paused);
    var featureOn = playerFeature.classList.contains('on');
    var extra = window.SNFExtraInserts && window.SNFExtraInserts.active();
    var scene = window.SNFBroadcastScenes && window.SNFBroadcastScenes.active();
    var pregame = window.SNFPregameInserts && window.SNFPregameInserts.active();
    bug.classList.toggle('off-air', !broadcastLive || featureOn || luOn || extra === 'comparison' || !!scene || !!pregame);
    qbStat.classList.toggle('on', broadcastLive && qbVisible && !featureOn && !luOn && !busy && !extra && !scene && !pregame);
    if (!broadcastLive) notice.classList.remove('on');
    if (!matchActive()) wipe.classList.remove('on');
  }
  document.addEventListener('snf-extra-change', refreshVisibility);
  document.addEventListener('snf-pregame-change', function () {
    if(window.SNFPregameInserts&&window.SNFPregameInserts.active())resetBroadcast();else refreshVisibility();
  });
  document.addEventListener('snf-broadcast-change', function () {
    /* Automatic GAME_STARTED opens also take ownership. Cancel previous
       cards and their callbacks before the open, not only explicit events. */
    if (window.SNFBroadcastScenes && window.SNFBroadcastScenes.active()) resetBroadcast();
    else refreshVisibility();
  });
  function resetBroadcast(preserveWipe) {
    timers.forEach(clearTimeout); timers = []; scoreQueue = []; busy = false;
    recentPoints = { away:0, home:0 }; lastDrive = null;
    bug.classList.remove('split','wordphase','compact','stats','closing','player-drive');
    takeoverEl.className = 'takeover'; pod.classList.remove('glow','flag'); flagUntil = 0;
    clearTimeout(flagTimer); clearTimeout(qbTimer); qbVisible = false; qbStatData = null;
    clearTimeout(featureTimer); playerFeature.classList.remove('on');
    luClear(); luOn = false; lu.classList.remove('on','swap');
    clearTimeout(noticeTimer); notice.classList.remove('on');
    if (!preserveWipe) { clearTimeout(wipeTimer); wipe.classList.remove('on'); }
    refreshVisibility();
  }
  function showNotice(title, detail, ms) {
    if (!broadcastLive || busy || luOn || playerFeature.classList.contains('on')) return;
    notice.querySelector('.notice-title').textContent = title;
    notice.querySelector('.notice-detail').textContent = detail || '';
    clearTimeout(qbTimer);qbVisible=false;refreshVisibility();
    clearTimeout(noticeTimer); notice.classList.remove('on'); void notice.offsetWidth; notice.classList.add('on');
    noticeTimer = setTimeout(function () { notice.classList.remove('on'); }, ms || 4200);
  }
  function replayWipe() {
    if (!matchActive()) return;
    clearTimeout(wipeTimer); wipe.classList.remove('on'); void wipe.offsetWidth; wipe.classList.add('on');
    wipeTimer = setTimeout(function () { wipe.classList.remove('on'); }, 1100);
  }
  NFL2K5.onState(function (s, prev) {
    latestState = s;
    var key = s.away && s.home ? code(s.away) + '|' + code(s.home) : '';
    var changedMatch = key && matchKey && key !== matchKey || prev && s.quarter > 0 && s.quarter < prev.quarter;
    refreshVisibility();
    if (!broadcastLive || changedMatch) resetBroadcast(!changedMatch && matchActive());
    if (key) matchKey = key;
    if (!s.away || !s.home) return;
    setTeam('away', s.away, prev && prev.away);
    setTeam('home', s.home, prev && prev.home);
    setText('.pod .clock', clock(s.gameClock));
    var n = s.quarter || 0;
    setText('.pod .quarter .num', n === 0 ? '' : n <= 4 ? String(n) : 'OT');
    setText('.pod .quarter .ord', n >= 1 && n <= 4 ? ORD[n] : '');
    if (dirty) { layout(); dirty = false; }

    var cx = s.context || {};
    refreshVisibility();
    var down = niceDown(s.downDistanceText);
    setTab(s.possession, down, !!s.possession && !!down);
    var pc = s.playClock;
    clockBox.textContent = pc >= 0 ? String(pc) : '';
    clockBox.classList.toggle('on', pc >= 0 && !!s.possession);
    clockBox.classList.toggle('low', pc >= 0 && pc <= 5);
    updateFlag(broadcastLive && !!cx.flag);
  });

  NFL2K5.onEvent(function (e) {
    if (!e) return;
    /* A replay transition can begin while the native scorebug is hidden.
       It still belongs to a valid match; menus and inactive games cannot
       start it. Normal inserts continue to require the live camera. */
    if (e.name === 'REPLAY_STARTED' || e.name === 'REPLAY_ENDED') { replayWipe(); return; }
    if (e.name === 'MATCHUP_INTRO' || e.name === 'BROADCAST_INTRO' || e.name === 'PLAYER_PROFILE' || e.name === 'PLAYER_BIO' || e.name === 'TIMEOUT') {
      if (matchActive()) resetBroadcast();
      return;
    }
    if (!broadcastLive) return;
    switch (e.name) {
    case 'PLAYER_COMPARISON': case 'TEAM_STATS': resetBroadcast(); break;
    case 'SCORE_CHANGED': if (e.team) recentPoints[e.team] = (busy ? recentPoints[e.team] : 0) + (e.points || 0); break;
    case 'TOUCHDOWN': scoring('TOUCHDOWN', e.team, e.drive); break;
    case 'FIELD_GOAL': scoring('FIELD GOAL', e.team, e.drive); break;
    case 'SAFETY': scoring('SAFETY', e.team, null); break;
    case 'EXTRA_POINT': scoring('EXTRA POINT', e.team, null); break;
    case 'TWO_POINT_CONVERSION': scoring('TWO POINTS', e.team, null); break;
    case 'DRIVE_SUMMARY': lastDrive = e.drive; if (busy && e.drive) showDrive(e.drive); break;
    case 'LINEUP': if (broadcastLive) lineup(e); break;
    case 'PLAYER_STAT':
      if(e.placement==='scorebug' || e.scope==='drive'){drivePlayer(e);break;}
      if (!setPlayerStat(e)) break;
      clearTimeout(qbTimer);
      qbVisible = true;
      if (broadcastLive && !busy && !luOn && !playerFeature.classList.contains('on')) {
        /* Restart the staged insert every time a different player is shown. */
        qbStat.classList.remove('on');
        void qbStat.offsetWidth;
        qbStat.classList.add('on');
      }
      refreshVisibility();
      qbTimer = setTimeout(function () { qbVisible = false; refreshVisibility(); }, duration(e,5500));
      break;
    case 'PLAYER_STAT_HIDE': clearTimeout(qbTimer); qbVisible = false; qbStat.classList.remove('on'); break;
    case 'PLAYER_FEATURE': if (broadcastLive) showPlayerFeature(e); break;
    case 'PENALTY': clearTimeout(flagTimer); flagUntil = Date.now() + 8000; updateFlag(true); flagTimer = setTimeout(function () { updateFlag(false); }, 8100); break;
    case 'TWO_MINUTE_WARNING': showNotice('TWO-MINUTE WARNING', '', 4200); break;
    case 'QUARTER_ENDED': showNotice('END OF ' + (e.quarter || latestState.quarter) + ({1:'ST',2:'ND',3:'RD',4:'TH'}[e.quarter || latestState.quarter] || '') + ' QUARTER', '', 4800); break;
    case 'HALFTIME': showNotice('HALFTIME', '', 4800); break;
    case 'OVERTIME': showNotice('OVERTIME', '', 4800); break;
    case 'GAME_ENDED': showNotice('FINAL', '', 6500); break;
    case 'GAME_STARTED': case 'QUARTER_STARTED': notice.classList.remove('on'); break;
    }
  });

  /* Extra width beyond the 1920 design, per side (fill_width pages). */
  function pad() {
    document.documentElement.style.setProperty('--pad', Math.max(0, (window.innerWidth - 1920) / 2) + 'px');
  }
  window.addEventListener('resize', pad);
  pad();

  if (document.fonts && document.fonts.ready) document.fonts.ready.then(layout);
  layout();
})();
