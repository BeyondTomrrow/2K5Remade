/* Pregame native-card replacements. Only the owning native event supplies
 * the player, fact, coin-toss message and lifetime. No statistics or toss
 * choices are inferred, and no controller input is intercepted here. */
(function () {
  'use strict';
  var api = window.NFL2K5, canvas = document.getElementById('canvas');
  if (!api || !canvas) return;
  var latest = api.state, current = null, timer = 0, ownership = '', matchKey = '';
  var aliases = { STL:'LAR', SD:'LAC', OAK:'LV', JAC:'JAX', WSH:'WAS' };
  var colors = { LAR:'#1f4fae', CIN:'#da541b', ARI:'#97233f', ATL:'#a71930', BAL:'#3b2a8f', BUF:'#00338d', CAR:'#0085ca', CHI:'#c83803', CLE:'#ff3c00', DAL:'#003594', DEN:'#fb4f14', DET:'#0076b6', GB:'#2f5a3e', HOU:'#a71930', IND:'#0b4a8f', JAX:'#006778', KC:'#e31837', LV:'#6b6f74', LAC:'#0080c6', MIA:'#008e97', MIN:'#4f2683', NE:'#0c2f5e', NO:'#9f8958', NYG:'#0b2265', NYJ:'#125740', PHI:'#004c54', PIT:'#d9a300', SF:'#aa0000', SEA:'#1d4f91', TB:'#d50a0a', TEN:'#4b92db', WAS:'#7a1a1a' };

  function node(tag, cls, parent, text) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = String(text);
    if (parent) parent.appendChild(n);
    return n;
  }
  function image(cls, parent) { var n = node('img', cls, parent); n.setAttribute('alt', ''); return n; }
  function hasText(v) { return typeof v === 'string' && v.trim().length > 0; }
  function code(t) {
    var c = t && hasText(t.abbreviation) ? t.abbreviation.trim().toUpperCase() : '';
    return /^[A-Z0-9]{2,4}$/.test(c) ? aliases[c] || c : '';
  }
  function color(t) {
    var c = t && (t.primaryColor || t.color);
    if (typeof c === 'string' && /^#[\da-f]{6}$/i.test(c)) return c;
    return colors[code(t)] || '#354554';
  }
  function asset(src) {
    if (!hasText(src)) return '';
    return /^(?:https?:\/\/|assets\/)/i.test(src) && !/[\u0000-\u001f]/.test(src) ? src : '';
  }
  function setImage(n, src, fallback) {
    src = asset(src); fallback = asset(fallback);
    n.onerror = function () {
      if (fallback && n.getAttribute('src') !== fallback) { n.setAttribute('src', fallback); return; }
      n.style.visibility = 'hidden'; n.removeAttribute('src');
      if (n === fact.portrait) fact.root.classList.add('no-photo');
    };
    n.style.visibility = src ? '' : 'hidden';
    if (src) n.setAttribute('src', src); else n.removeAttribute('src');
  }
  function teamFor(e) {
    var side = e.team === 'away' || e.team === 'home' ? latest && latest[e.team] : null;
    if (side) {
      if (e.teamData && (!code(e.teamData) || code(e.teamData) !== code(side))) return null;
      return e.teamData || side;
    }
    if (!e.teamData || !code(e.teamData) || !latest) return null;
    var matches = [latest.away, latest.home].filter(function (t) { return code(t) === code(e.teamData); });
    return matches.length === 1 ? e.teamData : null;
  }
  function teamImage(n, t) { var c = code(t); setImage(n, c ? 'assets/logos/' + c + '.png' : t && t.logo, t && t.logo); }
  function teamStyle(root, t) {
    var c = color(t), hex = c.slice(1);
    root.style.setProperty('--pregame-team', c);
    root.style.setProperty('--pregame-rgb', [parseInt(hex.slice(0,2),16), parseInt(hex.slice(2,4),16), parseInt(hex.slice(4,6),16)].join(','));
  }
  function stage() { return latest && latest.context && latest.context.pregameStage || ''; }
  function basePermitted() {
    var s = latest, cx = s && s.context || {};
    if (!s || s.valid !== true || !s.away || !s.home || s.gameStatus === 'inactive' || s.gameStatus === 'final' || cx.playSelection || cx.paused) return false;
    /* A known football phase wins over an old pregame-stage marker. Phase
       zero is the native pregame/toss phase; one through four are play. */
    if (s.phase !== 0) return false;
    return true;
  }
  function permitted(kind, legacyNative) {
    if (!basePermitted()) return false;
    var p = stage();
    if (kind === 'player-fact') {
      if (p === 'warmup' || p === 'panning') return true;
      /* An owned native fact already confirms the card is being shown.
         This does not infer a camera stage or open the matchup title. */
      return !p && legacyNative === true && latest.gameStatus === 'pregame';
    }
    if (p === 'coin_toss') return true;
    return !p && legacyNative === true && latest.gameStatus === 'pregame' && latest.phase === 0;
  }
  function notify() {
    var detail = { active:current }, ev;
    if (typeof CustomEvent === 'function') ev = new CustomEvent('snf-pregame-change', { detail:detail });
    else if (document.createEvent) { ev = document.createEvent('Event'); ev.initEvent('snf-pregame-change', false, false); ev.detail = detail; }
    if (ev) document.dispatchEvent(ev);
  }
  function curve(parent) {
    var ns = 'http://www.w3.org/2000/svg', svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('class', 'pg-curve'); svg.setAttribute('viewBox', '0 0 60 100'); svg.setAttribute('preserveAspectRatio', 'none'); svg.setAttribute('aria-hidden', 'true');
    [['M12 0 C50 22 50 78 8 100 L29 100 C66 70 67 25 42 0Z','#cfd4d9'], ['M42 0 C65 25 67 70 29 100 L42 100 C68 63 68 31 52 0Z','#7f8993']].forEach(function (p) {
      var path = document.createElementNS(ns, 'path'); path.setAttribute('d', p[0]); path.setAttribute('fill', p[1]); svg.appendChild(path);
    });
    parent.appendChild(svg);
  }
  function shell(id) {
    var root = node('div', 'snf-pregame', canvas); root.id = id; root.setAttribute('aria-hidden', 'true');
    node('div', 'pg-team-wall', root);
    var plate = node('div', 'pg-plate', root);
    node('div', 'pg-glass', plate); curve(plate); node('div', 'pg-sheen', plate);
    return { root:root, plate:plate, logo:image('pg-team-logo', root) };
  }
  var fact = shell('snf-player-fact');
  fact.portrait = image('pg-portrait', fact.root);
  fact.position = node('div', 'pg-position', fact.plate);
  var factHeader = node('div', 'pg-fact-heading', fact.plate);
  fact.name = node('div', 'pg-player-name', factHeader);
  fact.caption = node('div', 'pg-caption', factHeader);
  fact.text = node('div', 'pg-fact-text', fact.plate);
  var toss = shell('snf-coin-toss');
  var tossHeader = node('div', 'pg-toss-heading', toss.plate);
  toss.title = node('div', 'pg-toss-title', tossHeader);
  var brand = node('div', 'pg-brand', tossHeader);
  image('pg-peacock', brand).setAttribute('src', 'assets/branding/nbc-peacock-user.png');
  image('pg-snf', brand).setAttribute('src', 'assets/branding/snf-wordmark.svg');
  toss.text = node('div', 'pg-toss-text', toss.plate);
  var nativeOwned = false;
  var ackToken = 0, ackSerial = 0;
  function hostMessage(action, token) {
    if (window.NFL2K5_HOST !== 'game' || !window.chrome || !window.chrome.webview || !token) return;
    window.chrome.webview.postMessage('SNF_PREGAME_' + action + ':' + token);
  }
  function releaseNative() {
    ++ackSerial;
    hostMessage('HIDE', ackToken); ackToken = 0;
  }
  function acknowledgeNative(e) {
    var token = e.source === 'native' && e.nativeVisible === true &&
      Number.isInteger(e.replacementToken) && e.replacementToken > 0 && e.replacementToken <= 4294967295 ? e.replacementToken : 0;
    if (token === ackToken) return;
    releaseNative(); ackToken = token;
    if (!token || typeof window.requestAnimationFrame !== 'function') return;
    var serial = ackSerial;
    /* Keep ESPN as fallback until this owned replacement reaches a paint.
       A reset or replacement during those frames cancels this claim. */
    window.requestAnimationFrame(function () {
      window.requestAnimationFrame(function () {
        if (serial === ackSerial && token === ackToken && current && nativeOwned) hostMessage('SHOW', token);
      });
    });
  }

  function erase() {
    [fact, toss].forEach(function (c) { c.root.classList.remove('on', 'out'); c.root.setAttribute('aria-hidden', 'true'); c.root.removeAttribute('data-owner'); c.root.style.removeProperty('--pregame-team'); c.root.style.removeProperty('--pregame-rgb'); setImage(c.logo, ''); });
    setImage(fact.portrait, ''); fact.root.classList.add('no-photo');
    [fact.position, fact.name, fact.caption, fact.text, toss.title, toss.text].forEach(function (n) { n.textContent = ''; });
    fact.position.hidden = true; fact.root.classList.remove('long-name');
  }
  function reset() {
    releaseNative();
    clearTimeout(timer); timer = 0; ownership = ''; nativeOwned = false; erase();
    if (current !== null) { current = null; notify(); }
  }
  function hide() {
    var card = current === 'player-fact' ? fact : current === 'coin-toss' ? toss : null;
    if (!card || card.root.classList.contains('out')) return;
    clearTimeout(timer); releaseNative();
    /* Keep this card's own strings through its exit. Phase/menu changes and
       replacement cards still reset immediately and cancel this timer. */
    card.root.classList.remove('on'); card.root.classList.add('out');
    timer = setTimeout(reset, 350);
  }
  function lifetime(e) {
    clearTimeout(timer); timer = 0;
    var seconds = e.duration != null ? e.duration : e.displaySeconds;
    /* Unknown FLTM values do not mean five seconds. In their absence the
       native hide event, next card or phase change owns the lifetime. */
    if (typeof seconds === 'number' && isFinite(seconds) && seconds > 0) timer = setTimeout(hide, Math.min(seconds * 1000, 2147483647));
  }
  function reveal(kind, c, e, owner) {
    ownership = owner; current = kind; c.root.setAttribute('data-owner', owner); c.root.setAttribute('aria-hidden', 'false');
    c.root.classList.remove('on', 'out'); void c.root.offsetWidth; c.root.classList.add('on'); lifetime(e); notify();
  }
  function playerFact(e) {
    var p = e.player, t = teamFor(e), ownsNativeFact = e.source === 'native' && e.nativeVisible === true;
    if (!permitted('player-fact', ownsNativeFact) || !p || !hasText(p.fullName) || !hasText(e.text) || !t) { reset(); return; }
    /* Conflicting cached identity must not put one player's face beneath
       another player's native fact. The bridge must supply one owner. */
    if (hasText(p.name) && p.name.trim().split(/\s+/).pop().toLowerCase() !== p.fullName.trim().split(/\s+/).pop().toLowerCase()) { reset(); return; }
    var number = Number.isInteger(p.number) && p.number >= 0 && p.number <= 99 ? String(p.number) : '';
    var portrait = asset(p.portrait), position = hasText(p.position) ? p.position : '';
    var owner = JSON.stringify([code(t), p.id == null ? null : p.id, p.fullName, position, number, portrait, e.caption || 'CAREER', e.text]);
    if (current === 'player-fact' && ownership === owner && !fact.root.classList.contains('out')) {
      nativeOwned = ownsNativeFact; acknowledgeNative(e); return;
    }
    reset(); teamStyle(fact.root, t); teamImage(fact.logo, t); setImage(fact.portrait, portrait);
    nativeOwned = ownsNativeFact;
    fact.root.classList.toggle('no-photo', !portrait); fact.root.classList.toggle('long-name', p.fullName.length > 26);
    fact.position.textContent = [position, number].filter(Boolean).join('\n'); fact.position.hidden = !position && !number;
    fact.name.textContent = p.fullName; fact.caption.textContent = hasText(e.caption) ? e.caption : 'CAREER'; fact.text.textContent = e.text;
    reveal('player-fact', fact, e, owner);
    acknowledgeNative(e);
  }
  function coinToss(e) {
    var nativeMessage = e.source === 'native' || e.native === true, t = teamFor(e);
    if (!permitted('coin-toss', nativeMessage) || !hasText(e.text) || !t) { reset(); return; }
    var owner = JSON.stringify([code(t), hasText(e.title) ? e.title : 'COIN TOSS', e.text]);
    if (current === 'coin-toss' && ownership === owner && !toss.root.classList.contains('out')) {
      nativeOwned = nativeMessage; acknowledgeNative(e); return;
    }
    var update = current === 'coin-toss' && !toss.root.classList.contains('out');
    if (!update) reset();
    nativeOwned = nativeMessage; teamStyle(toss.root, t); teamImage(toss.logo, t);
    toss.title.textContent = hasText(e.title) ? e.title : 'COIN TOSS'; toss.text.textContent = e.text;
    if (update) { ownership = owner; toss.root.setAttribute('data-owner', owner); lifetime(e); }
    else reveal('coin-toss', toss, e, owner);
    acknowledgeNative(e);
  }
  window.SNFPregameInserts = { active:function () { return current; }, reset:reset };
  api.onState(function (s) {
    latest = s;
    var key = s && s.away && s.home ? String(s.matchId || '') + '|' + code(s.away) + '|' + code(s.home) : '';
    var changed = key && matchKey && key !== matchKey;
    if (changed || !basePermitted() || current && !permitted(current, nativeOwned)) reset();
    matchKey = basePermitted() ? key : '';
  });
  api.onEvent(function (e) {
    if (!e) return;
    if (e.name === 'PLAYER_FACT') { playerFact(e); return; }
    if (e.name === 'COIN_TOSS') { coinToss(e); return; }
    if (e.name === 'PLAYER_FACT_HIDE') { if (current === 'player-fact') hide(); return; }
    if (e.name === 'COIN_TOSS_HIDE') { if (current === 'coin-toss') hide(); return; }
    if (['PREGAME_INSERT_HIDE','PLAYER_STAT','PLAYER_FEATURE','PLAYER_PROFILE','PLAYER_BIO','PLAYER_COMPARISON','TEAM_STATS','LINEUP','MATCHUP_INTRO','BROADCAST_INTRO','GAME_STARTED','PLAY_STARTED','REPLAY_STARTED','TIMEOUT','TOUCHDOWN','FIELD_GOAL','SAFETY','EXTRA_POINT','TWO_POINT_CONVERSION','QUARTER_ENDED','GAME_ENDED'].indexOf(e.name) >= 0) reset();
  });
})();
