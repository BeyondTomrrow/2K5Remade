/* My NFL Eras draft show overlay. Events (src/nfl2k5_eras_draftshow.c):
 *   DRAFT_SHOW_START {year, round}          the bug and ticker
 *   DRAFT_BOARD {picks:[{pick, team, teamName, player, position, made}], current}
 *   DRAFT_ON_CLOCK / DRAFT_PICK_IN / DRAFT_ANNOUNCE / DRAFT_TEAM / DRAFT_PICK_CARD / DRAFT_PHOTO
 *       {round, pick, roundPick, team, teamCity, teamName, teamAbbr, primary, secondary,
 *        player, position, college}
 *   DRAFT_HANDSHAKE, DRAFT_CLEAR, DRAFT_ROUND_SUMMARY {picks, current}
 * Team logos: https://erateams.local/<season>/<eras id>/logos/scorebug.png (1980 looks). */
(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var inGame = !!(window.chrome && window.chrome.webview);
  var season = 1980, made = [], clockTimer = null;

  function log(t) { if (inGame) window.chrome.webview.postMessage('draftshow: ' + t); }
  function show(id, on) { $(id).classList.toggle('on', !!on); }
  function logo(team) { return 'https://erateams.local/' + season + '/' + team + '/logos/scorebug.png'; }
  function ord(n) { var s = ['TH', 'ST', 'ND', 'RD'], v = n % 100; return n + (s[(v - 20) % 10] || s[v] || s[0]); }
  function full(e) { return ((e.teamCity ? e.teamCity + ' ' : '') + (e.teamName || e.team)).toUpperCase(); }
  function colours(e) {
    var r = document.documentElement.style;
    if (e.primary) r.setProperty('--team', e.primary);
    if (e.secondary) r.setProperty('--team2', e.secondary === '#000000' ? '#e0a43a' : e.secondary);
  }
  function clearCards() { ['clock', 'board', 'pickin', 'announce', 'card', 'photo'].forEach(function (id) { show(id, false); }); }

  function ticker() {
    if (!made.length) return;
    $('ticker-tx').innerHTML = made.slice(-3).map(function (m) {
      return m.pick + '. ' + (m.teamName || m.team).toUpperCase() + ' - ' + m.player.toUpperCase() + ' ' + m.position;
    }).join('<em>&#9632;</em>');
  }

  function board(e, summary) {
    var ol = $('board-list'); ol.innerHTML = '';
    (e.picks || []).slice(0, 14).forEach(function (p) {
      if (summary && !p.made) return;
      var li = document.createElement('li');
      if (p.pick === e.current && !summary) li.className = 'cur';
      li.innerHTML = '<span class="n">' + p.pick + '</span><img src="' + logo(p.team) + '"><span class="t">' +
        (p.teamName || p.team).toUpperCase() + '</span><span class="p">' +
        (p.made ? (p.player + ' ' + p.position).toUpperCase() : (p.pick === e.current ? 'ON THE CLOCK' : '')) + '</span>';
      ol.appendChild(li);
    });
    show('board', true);
  }

  function startClock() {
    var t = 15 * 60;              /* 1981: fifteen minutes a first-round pick; run fast for TV */
    if (clockTimer) clearInterval(clockTimer);
    $('clock-time').textContent = '15:00';
    clockTimer = setInterval(function () {
      t = Math.max(0, t - 7);
      $('clock-time').textContent = Math.floor(t / 60) + ':' + ('0' + (t % 60)).slice(-2);
    }, 100);
  }

  var handlers = {
    DRAFT_SHOW_START: function (e) {
      season = e.season || 1980;
      $('bug-round').textContent = 'ROUND ' + (e.round || 1);
      show('bug', true); show('ticker', true);
    },
    DRAFT_BOARD: function (e) { board(e, false); },
    DRAFT_ON_CLOCK: function (e) {
      colours(e);
      $('clock-logo').src = logo(e.team);
      $('clock-team').textContent = full(e);
      $('clock-sub').innerHTML = 'ROUND ' + e.round + ' &middot; PICK ' + e.roundPick + ' &middot; ' + ord(e.pick) + ' OVERALL';
      startClock();
      show('clock', true);
    },
    DRAFT_PICK_IN: function () {
      if (clockTimer) clearInterval(clockTimer);
      show('board', false); show('clock', false); show('pickin', true);
    },
    DRAFT_ANNOUNCE: function (e) {
      show('pickin', false);
      $('announce-1').textContent = 'WITH THE ' + ord(e.pick) + ' PICK IN THE ' + e.year + ' NFL DRAFT,';
      $('announce-2').textContent = '';
      $('announce-2').style.visibility = 'hidden';
      show('announce', true);
    },
    DRAFT_TEAM: function (e) {
      colours(e);
      $('announce-2').textContent = 'THE ' + full(e) + ' SELECT...';
      $('announce-2').style.visibility = 'visible';
    },
    DRAFT_PICK_CARD: function (e) {
      /* the captions go at once (no fade: the card never shares the frame with "...SELECT"),
       * then the card slides in on the team clip's end (this event is posted at its last sample) */
      $('announce').classList.add('cut'); show('announce', false);
      void $('announce').offsetWidth;
      setTimeout(function () { $('announce').classList.remove('cut'); }, 50);
      colours(e);
      $('card-round').textContent = e.round;
      $('card-pick').textContent = e.roundPick;
      $('card-name').textContent = e.player.toUpperCase();
      $('card-pos').textContent = e.position;
      $('card-college').textContent = e.college.toUpperCase();
      $('card-team').textContent = full(e);
      $('card-logo').src = logo(e.team);
      show('card', true);
      made.push(e);
      ticker();
      log('card ' + e.pick + ' ' + e.player + ' ' + e.team);
    },
    DRAFT_HANDSHAKE: function () { show('card', false); },
    DRAFT_PHOTO: function (e) {
      show('card', false);
      $('photo-name').textContent = e.player.toUpperCase();
      $('photo-team').innerHTML = full(e) + ' &middot; NO. ' + e.pick + ' OVERALL';
      show('photo', true);
      var f = $('flash'); f.classList.remove('go'); void f.offsetWidth; f.classList.add('go');
    },
    DRAFT_CLEAR: function () { clearCards(); },
    DRAFT_ROUND_SUMMARY: function (e) { clearCards(); board(e, true); }
  };

  function dispatch(msg) {
    if (!msg || msg.type !== 'event' || !msg.event) return;
    var h = handlers[msg.event.name];
    try { if (h) h(msg.event); } catch (err) { log('error ' + err); }
  }
  if (inGame) window.chrome.webview.addEventListener('message', function (e) { dispatch(e.data); });
  window.addEventListener('error', function (e) { log('script error ' + e.message + ' @' + e.lineno); });
  window.DraftShow = { dispatch: dispatch };
  log('page ready');

  /* in a plain browser: a demo of pick 1 (layout work) */
  if (!inGame) {
    document.body.classList.add('demo');
    var pick = { year: 1981, season: 1980, round: 1, pick: 1, roundPick: 1, team: 'saints', teamCity: 'New Orleans',
      teamName: 'Saints', primary: '#9F8958', secondary: '#000000', player: 'George Rogers', position: 'RB', college: 'South Carolina' };
    var seq = [['DRAFT_SHOW_START', {}], ['DRAFT_BOARD', { picks: [{ pick: 1, team: 'saints', teamName: 'Saints' }, { pick: 2, team: 'giants', teamName: 'Giants' }], current: 1 }],
      ['DRAFT_ON_CLOCK', pick], ['DRAFT_PICK_IN', pick], ['DRAFT_ANNOUNCE', pick], ['DRAFT_TEAM', pick], ['DRAFT_PICK_CARD', pick], ['DRAFT_PHOTO', pick]];
    var q = (location.hash || '').replace('#', '');
    seq.forEach(function (s, i) {
      if (q && i > +q) return;
      var ev = Object.assign({ name: s[0], season: 1980, year: 1981, round: 1 }, s[1]);
      if (q) dispatch({ type: 'event', event: ev });
      else setTimeout(function () { dispatch({ type: 'event', event: ev }); }, 1500 * i);
    });
  }
})();
