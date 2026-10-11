/* Supplemental 2022 SNF insert families. All names, portraits, colors and
 * numbers are supplied event data. No roster identity or stats are invented. */
(function () {
  'use strict';
  var api = window.NFL2K5, canvas = document.getElementById('canvas');
  if (!api || !canvas) return;
  var latest = api.state, current = null, timer = 0, matchKey = '';
  var alias = { STL:'LAR', SD:'LAC', OAK:'LV', JAC:'JAX', WSH:'WAS' };
  function node(tag, cls, parent, text) {
    var el = document.createElement(tag);
    if (cls) el.className = cls;
    if (text != null) el.textContent = String(text);
    if (parent) parent.appendChild(el);
    return el;
  }
  function image(cls, parent) { var el = node('img', cls, parent); el.setAttribute('alt', ''); return el; }
  function text(value) { return value == null ? '' : String(value); }
  function teamColor(team) { return team && (team.primaryColor || team.color) || '#334550'; }
  function teamLogo(team) {
    if (!team) return '';
    if (team.logo) return team.logo;
    var c = String(team.abbreviation || '').toUpperCase();
    return c ? 'assets/logos/' + (alias[c] || c) + '.png' : '';
  }
  function setImage(el, src) {
    el.style.visibility = src ? '' : 'hidden';
    el.onerror = function () { el.style.visibility = 'hidden'; el.removeAttribute('src'); };
    if (src) el.setAttribute('src', src); else el.removeAttribute('src');
  }
  function permitted(activeInsert) {
    var s = latest, cx = s && s.context || {};
    return !!(s && s.valid === true && s.away && s.home && !cx.playSelection && !cx.paused &&
      (cx.scorebugVisible === true || activeInsert));
  }
  function notify() {
    if (!document.dispatchEvent) return;
    var ev, detail = { active:current };
    if (typeof CustomEvent === 'function') ev = new CustomEvent('snf-extra-change', { detail:detail });
    else if (document.createEvent) { ev = document.createEvent('Event'); ev.initEvent('snf-extra-change', false, false); ev.detail = detail; }
    if (ev) document.dispatchEvent(ev);
  }
  function reset() {
    clearTimeout(timer); timer = 0;
    comparison.classList.remove('on'); teamStats.classList.remove('on');
    comparison.setAttribute('aria-hidden', 'true'); teamStats.setAttribute('aria-hidden', 'true');
    if (current !== null) { current = null; notify(); }
  }
  function show(kind, element, e) {
    clearTimeout(timer);
    current = kind;
    element.classList.remove('on'); void element.offsetWidth;
    element.setAttribute('aria-hidden', 'false'); element.classList.add('on'); notify();
    var seconds = Number(e.duration || e.displaySeconds);
    timer = setTimeout(reset, seconds > 0 && isFinite(seconds) ? Math.min(seconds, 30) * 1000 : 7000);
  }

  var comparison = node('div', 'snf-extra', canvas); comparison.setAttribute('id', 'snf-comparison'); comparison.setAttribute('aria-hidden', 'true');
  var stage = node('div', 'xc-stage', comparison);
  var glass = node('div', 'xc-glass', stage);
  var heading = node('div', 'xc-heading', glass);
  var compTitle = node('div', 'xc-title', heading), compSubtitle = node('div', 'xc-subtitle', heading);
  var compRows = node('div', 'xc-rows', glass);
  var sides = ['left','right'].map(function (side) {
    var wall = node('div', 'xc-side ' + side, stage);
    node('div', 'xc-vapor', wall); node('div', 'xc-silver-curve', wall);
    var logo = image('xc-logo', wall), portrait = image('xc-portrait', wall);
    var meta = node('div', 'xc-player-meta', wall), pos = node('div', 'xc-position', meta), name = node('div', 'xc-name', meta);
    return { root:wall, logo:logo, portrait:portrait, position:pos, name:name };
  });
  function validRows(rows, count) {
    if (!Array.isArray(rows)) return [];
    return rows.filter(function (r) {
      return r && r.label != null && Array.isArray(r.values) && r.values.length >= count &&
        r.values.slice(0,count).every(function (v) { return typeof v === 'string' || typeof v === 'number' && isFinite(v); });
    }).slice(0, 8);
  }
  function compare(e) {
    if (!permitted(false) || !Array.isArray(e.players) || e.players.length !== 2 ||
        e.players.some(function (p) { return !p || !(p.name || p.fullName); })) return;
    var rows = validRows(e.rows, 2); if (!rows.length) return;
    reset();
    compTitle.textContent = text(e.title || 'PLAYER COMPARISON'); compSubtitle.textContent = text(e.subtitle);
    compRows.textContent = '';
    rows.forEach(function (r, i) {
      var row = node('div', 'xc-row', compRows); row.style.setProperty('--extra-row', i);
      node('b', 'xc-value left', row, r.values[0]); node('span', 'xc-label', row, r.label); node('b', 'xc-value right', row, r.values[1]);
    });
    e.players.forEach(function (p, i) {
      var side = sides[i], team = p.teamData || {};
      side.root.style.setProperty('--extra-team', teamColor(team));
      side.name.textContent = text(p.fullName || p.name).toUpperCase();
      side.position.textContent = text(p.position).toUpperCase() + (p.number != null ? ' ' + p.number : '');
      setImage(side.portrait, p.portrait); setImage(side.logo, teamLogo(team));
      side.root.classList.toggle('no-photo', !p.portrait);
      comparison.style.setProperty(i ? '--extra-right' : '--extra-left', teamColor(team));
    });
    show('comparison', comparison, e);
  }

  var teamStats = node('div', 'snf-extra', canvas); teamStats.setAttribute('id', 'snf-team-stats'); teamStats.setAttribute('aria-hidden', 'true');
  node('div', 'xt-curve', teamStats); node('div', 'xt-light', teamStats);
  var teamHeader = node('div', 'xt-heading', teamStats), statsLogo = image('xt-logo', teamHeader), statsTitle = node('div', 'xt-title', teamHeader);
  var columns = node('div', 'xt-columns', teamStats), teamRows = node('div', 'xt-rows', teamStats);
  function table(e) {
    if (!permitted(false) || !e.teamData || !Array.isArray(e.columns) || !e.columns.length ||
        !e.columns.every(function (c) { return typeof c === 'string'; })) return;
    var cols = e.columns.slice(0,5), rows = validRows(e.rows, cols.length); if (!rows.length) return;
    reset();
    teamStats.style.setProperty('--extra-team', teamColor(e.teamData));
    teamStats.style.setProperty('--extra-columns', cols.length);
    statsTitle.textContent = text(e.title || 'TEAM STATS').toUpperCase();
    setImage(statsLogo, teamLogo(e.teamData));
    columns.textContent = ''; node('span', 'xt-label', columns, '');
    cols.forEach(function (c) { node('span', 'xt-column', columns, c); });
    teamRows.textContent = '';
    rows.forEach(function (r, i) {
      var row = node('div', 'xt-row', teamRows); row.style.setProperty('--extra-row', i);
      node('span', 'xt-label', row, r.label); r.values.slice(0, cols.length).forEach(function (v) { node('b', 'xt-value', row, v); });
    });
    show('team', teamStats, e);
  }

  window.SNFExtraInserts = { active:function () { return current; }, reset:reset };
  api.onState(function (s, prev) {
    latest = s;
    var key = s.away && s.home ? text(s.away.abbreviation) + '|' + text(s.home.abbreviation) : '';
    var changed = key && matchKey && key !== matchKey || prev && s.quarter > 0 && s.quarter < prev.quarter;
    if (!permitted(current !== null) || changed) reset();
    if (key) matchKey = key;
  });
  api.onEvent(function (e) {
    if (!e) return;
    if (e.name === 'PLAYER_COMPARISON') { reset(); compare(e); return; }
    if (e.name === 'TEAM_STATS') { reset(); table(e); return; }
    if (['TOUCHDOWN','FIELD_GOAL','SAFETY','EXTRA_POINT','TWO_POINT_CONVERSION','SCORING','PLAYER_STAT','PLAYER_FEATURE','PLAYER_PROFILE','PLAYER_BIO','MATCHUP_INTRO','BROADCAST_INTRO','GAME_STARTED','TIMEOUT','LINEUP','GAME_ENDED','REPLAY_STARTED'].indexOf(e.name) >= 0) reset();
  });
})();
