/* HTML Test: binds live Presentation API state to the elements, and shows
 * a banner for each event. Everything shown comes from the game. */
(function () {
    'use strict';
    var $ = function (sel) { return document.querySelector(sel); };
    var ORD = ['', '1ST', '2ND', '3RD', '4TH'];

    function clockText(seconds) {
        if (seconds == null || seconds < 0) return '';
        var s = Math.ceil(seconds);
        return Math.floor(s / 60) + ':' + ('0' + (s % 60)).slice(-2);
    }
    function quarterText(q) {
        if (!q) return 'PRE';
        return q <= 4 ? ORD[q] : (q === 5 ? 'OT' : (q - 4) + 'OT');
    }
    function setText(sel, text, pulse) {
        var el = $(sel);
        if (!el || el.textContent === String(text)) return;
        el.textContent = text;
        if (pulse) { el.classList.remove('changed'); void el.offsetWidth; el.classList.add('changed'); }
    }
    function team(side, t, prev) {
        if (!t) return;
        setText('.' + side + ' .abbr', t.abbreviation || '---');
        setText('.' + side + ' .score', t.score, prev && prev.score !== t.score);
        var logo = $('.' + side + ' .logo');
        if (logo.getAttribute('src') !== (t.logo || '')) logo.setAttribute('src', t.logo || '');
        var bars = document.querySelectorAll('.' + side + ' .timeouts i');
        for (var i = 0; i < bars.length; i++) bars[i].classList.toggle('used', i >= (t.timeouts || 0));
    }

    NFL2K5.onState(function (s, prev) {
        team('away', s.away, prev && prev.away);
        team('home', s.home, prev && prev.home);
        $('.away').classList.toggle('possession', s.possession === 'away');
        $('.home').classList.toggle('possession', s.possession === 'home');
        setText('.quarter', quarterText(s.quarter));
        setText('.clock', clockText(s.gameClock));
        setText('.down', s.downDistanceText || '', true);
        $('#bug').classList.toggle('hidden', !(s.context && s.context.scorebugVisible));
        setText('#status', 'API v' + NFL2K5.version + ' | ' + NFL2K5.host + ' | ' + (s.gameStatus || '?') +
                ' | phase ' + s.phase + ' | play clock ' + (s.playClock >= 0 ? s.playClock : '-'));
    });

    var LABEL = {
        TOUCHDOWN: 'TOUCHDOWN', FIELD_GOAL: 'FIELD GOAL', EXTRA_POINT: 'EXTRA POINT',
        TWO_POINT_CONVERSION: '2-PT CONVERSION', SAFETY: 'SAFETY', FIRST_DOWN: 'FIRST DOWN',
        INTERCEPTION: 'INTERCEPTION', FUMBLE: 'FUMBLE', TURNOVER: 'TURNOVER', PENALTY: 'FLAG',
        TIMEOUT: 'TIMEOUT', TWO_MINUTE_WARNING: 'TWO MINUTE WARNING', QUARTER_ENDED: 'END OF QUARTER',
        QUARTER_STARTED: 'QUARTER', HALFTIME: 'HALFTIME', GAME_ENDED: 'FINAL'
    };
    NFL2K5.onEvent(function (e) {
        NFL2K5.log('event ' + e.name + ' ' + (e.team || ''));
        var text = LABEL[e.name];
        if (!text) return;
        if (e.name === 'QUARTER_STARTED' && e.quarter) text = quarterText(e.quarter) + ' QUARTER';
        var team = e.team && NFL2K5.state && NFL2K5.state[e.team];
        if (team && team.abbreviation && /TOUCHDOWN|FIELD_GOAL|INTERCEPTION|TIMEOUT|SAFETY/.test(e.name))
            text = team.abbreviation + ' ' + text;
        var b = $('#banner');
        b.className = '';
        b.querySelector('span').textContent = text;
        void b.offsetWidth;
        b.className = 'show ' + e.name.toLowerCase().replace(/_/g, '-');
        if (e.name === 'INTERCEPTION') b.classList.add('interception');
        if (e.name === 'TOUCHDOWN') b.classList.add('touchdown');
        if (e.name === 'PENALTY') b.classList.add('penalty');
    });
})();
