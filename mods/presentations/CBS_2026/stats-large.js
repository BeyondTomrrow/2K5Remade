/* Editable CBS full-screen comparison, measured from the official NFL upload
 * PLR04ljZ5DI, 307.80–323.05s (Chiefs/Cowboys, Thanksgiving 2025).
 * No source photographs or statistics are embedded. Portraits are explicit
 * replaceable assets; absent portraits remain absent. Lifecycle belongs to
 * the presentation manager, not this renderer.
 */
(function () {
  'use strict';
  var svgNS = 'http://www.w3.org/2000/svg', serial = 0;
  var reference = 'Official NFL full game PLR04ljZ5DI, CBS Chiefs/Cowboys Thanksgiving 2025, WALK-OFF WINS comparison 307.80–323.05s';
  var aliases = {STL:'LAR',SD:'LAC',OAK:'LV',JAC:'JAX',WSH:'WAS'};
  var colors = {ARI:'#970d2b',ATL:'#a71930',BAL:'#241773',BUF:'#00338d',CAR:'#0085ca',CHI:'#0b162a',CIN:'#fb4f14',CLE:'#311d00',DAL:'#002c5f',DEN:'#fb4f14',DET:'#0076b6',GB:'#174535',HOU:'#03202f',IND:'#002c5f',JAX:'#006778',KC:'#b90c22',LV:'#25272a',LAC:'#0080c6',LAR:'#003594',MIA:'#008e97',MIN:'#4f2683',NE:'#002244',NO:'#39342d',NYG:'#123e97',NYJ:'#006c54',PHI:'#004c54',PIT:'#242426',SF:'#aa0000',SEA:'#002244',TB:'#9c1222',TEN:'#0c2340',WAS:'#5a1414'};
  function text(v) { return typeof v === 'string' ? v : typeof v === 'number' && Number.isFinite(v) ? String(v) : ''; }
  function node(tag, cls, value, parent) {
    var n = document.createElement(tag); if (cls) n.className = cls;
    if (value != null) n.textContent = text(value); if (parent) parent.appendChild(n); return n;
  }
  function vector(tag, attrs, parent) {
    var n = document.createElementNS(svgNS, tag);
    Object.keys(attrs).forEach(function (k) { n.setAttribute(k, attrs[k]); });
    if (parent) parent.appendChild(n); return n;
  }
  function clean(host) { while (host.firstChild) host.removeChild(host.firstChild); }
  function code(t) { var c = String(t && t.abbreviation || '').toUpperCase(); return aliases[c] || c; }
  function color(t) { return t && /^#[\da-f]{6}$/i.test(t.primaryColor || '') ? t.primaryColor : colors[code(t)] || '#303842'; }
  function accent(t) { var defaults={KC:'#eac670',DAL:'#bec7d5',NE:'#c60c30',PIT:'#ffb612',GB:'#ffb612',LV:'#bcc3c8'}; return t && /^#[\da-f]{6}$/i.test(t.secondaryColor || '') ? t.secondaryColor : defaults[code(t)] || '#c6d2df'; }
  function asset(src) {
    return typeof src === 'string' && src.trim() && !/[\u0000-\u001f]/.test(src) && !/^\s*(?:javascript|vbscript):/i.test(src) ? src : '';
  }
  function picture(parent, cls, src, alt) {
    src = asset(src); if (!src) return null;
    var img = node('img', cls, null, parent); img.alt = alt || ''; img.src = src;
    img.onerror = function () { img.hidden = true; parent.classList.add('cl-image-unavailable'); };
    return img;
  }
  function teamLogo(parent, cls, t) {
    var c = code(t), src = asset(t && t.logo) || (/^[A-Z0-9]{2,4}$/.test(c) ? 'assets/logos/' + c + '.png' : '');
    return picture(parent, cls, src, text(t && (t.name || t.abbreviation)));
  }
  function frame(parent, away, home) {
    var svg = vector('svg', {class:'cl-team-frame',viewBox:'0 0 1920 1080','aria-hidden':'true'}, parent);
    var id = 'cl-frame-' + (++serial), defs = vector('defs', {}, svg);
    var mask = vector('mask', {id:id,maskUnits:'userSpaceOnUse',x:'0',y:'0',width:'1920',height:'1080'}, defs);
    vector('rect', {width:'1920',height:'1080',fill:'white'}, mask);
    vector('ellipse', {cx:'960',cy:'540',rx:'966',ry:'558',fill:'black'}, mask);
    var group = vector('g', {mask:'url(#' + id + ')'}, svg);
    vector('path', {d:'M0 0H960V1080H0Z',fill:color(away)}, group);
    vector('path', {d:'M960 0H1920V1080H960Z',fill:color(home)}, group);
    var lines = vector('g', {fill:'none','stroke-width':'3',opacity:'.65'}, group);
    [0,1,2,3].forEach(function (i) {
      vector('path', {d:'M' + (-210 + i * 60) + ' 510L' + (360 + i * 60) + ' 0M0 ' + (735 + i * 60) + 'L' + (420 + i * 60) + ' 1080',stroke:accent(away)}, lines);
      vector('path', {d:'M' + (1530 - i * 60) + ' 0L1920 ' + (330 + i * 60) + 'M' + (1520 - i * 60) + ' 1080L1920 ' + (730 - i * 60),stroke:accent(home)}, lines);
    });
    vector('path', {d:'M960 -18A966 558 0 0 0 960 1098',fill:'none',stroke:accent(away),'stroke-width':'4'}, svg);
    vector('path', {d:'M960 -18A966 558 0 0 1 960 1098',fill:'none',stroke:accent(home),'stroke-width':'4'}, svg);
    teamLogo(parent, 'cl-frame-logo cl-left', away); teamLogo(parent, 'cl-frame-logo cl-right', home);
  }
  function render(host, e, state) {
    if (!host || !e || e.name !== 'PLAYER_COMPARISON') return false;
    clean(host);
    if (!state || !state.away || !state.home || !text(e.title) || !Array.isArray(e.players) || e.players.length !== 2 || !Array.isArray(e.rows) || !e.rows.length || e.rows.length > 6) return false;
    var entries = e.players, teams = [], players = [];
    for (var i = 0; i < 2; i++) {
      var item = entries[i];
      if (!item || (item.team !== 'away' && item.team !== 'home') || !item.player || !text(item.player.name || item.player.fullName)) return false;
      teams.push(state[item.team]); players.push(item.player);
    }
    for (i = 0; i < e.rows.length; i++) {
      if (!e.rows[i] || !text(e.rows[i].label) || !Array.isArray(e.rows[i].values) || e.rows[i].values.length !== 2) return false;
    }
    host.setAttribute('data-cbs-large-kind', 'player-comparison');
    host.style.setProperty('--cl-away', color(teams[0])); host.style.setProperty('--cl-home', color(teams[1]));
    host.style.setProperty('--cl-row-count', e.rows.length);
    host.style.setProperty('--cl-row-font', Math.min(76, 100 - e.rows.length * 8) + 'px');
    host.style.setProperty('--cl-label-font', Math.min(43, 58 - e.rows.length * 5) + 'px');
    var stage = node('div', 'cl-stage', null, host);
    node('div', 'cl-shade', null, stage);
    var shell = node('div', 'cl-shell', null, stage);
    frame(shell, teams[0], teams[1]);
    picture(shell, 'cl-network', 'assets/branding/cbs-sports.svg', 'CBS Sports');
    var panel = node('section', 'cl-panel', null, shell);
    panel.setAttribute('aria-label', text(e.title));
    var heading = node('header', 'cl-heading', null, panel);
    node('div', 'cl-title', null, heading).appendChild(node('span', '', text(e.title).toUpperCase()));
    if (text(e.subtitle)) node('div', 'cl-subtitle', text(e.subtitle).toUpperCase(), heading);
    var columns = node('div', 'cl-columns', null, panel);
    for (i = 0; i < 2; i++) {
      var column = node('div', 'cl-column cl-' + (i ? 'right' : 'left'), null, columns);
      teamLogo(column, 'cl-column-logo', teams[i]);
      node('span', '', text(entries[i].label || players[i].name || players[i].fullName).toUpperCase(), column);
    }
    var rows = node('div', 'cl-rows', null, panel);
    e.rows.forEach(function (row) {
      var line = node('div', 'cl-row', null, rows);
      for (var j = 0; j < 2; j++) {
        var value = node('div', 'cl-value cl-' + (j ? 'right' : 'left'), null, line);
        var valueText = text(row.values[j]);
        if (!valueText) { valueText = '\u2014'; value.setAttribute('aria-label', 'Unavailable'); }
        if (row.highlightSide === j || row.highlightSide === (j ? 'right' : 'left')) value.classList.add('cl-highlight');
        if (valueText.length > 12) value.classList.add('cl-value-long');
        node('span', '', valueText, value);
      }
      node('div', 'cl-row-label', text(row.label).toUpperCase(), line);
    });
    if (text(e.footnote)) node('div', 'cl-footnote', text(e.footnote).toUpperCase(), panel);
    for (i = 0; i < 2; i++) {
      var portrait = node('div', 'cl-portrait cl-' + (i ? 'right' : 'left'), null, shell);
      if (!picture(portrait, 'cl-player-image', players[i].portrait, text(players[i].fullName || players[i].name))) portrait.hidden = true;
    }
    var opener = node('div', 'cl-opener', null, stage);
    for (i = 0; i < 2; i++) {
      var plane = node('div', 'cl-plane cl-' + (i ? 'right' : 'left'), null, opener);
      teamLogo(plane, 'cl-plane-logo', teams[i]);
    }
    return true;
  }
  window.CBSLargeStats = {render:render,enterMs:1250,exitMs:550,reference:reference};
})();
