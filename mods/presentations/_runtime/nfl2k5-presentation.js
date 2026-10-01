/* NFL 2K5 Presentation API runtime (version 1).
 *
 * Every HTML presentation package includes this file:
 *     <script src="../_runtime/nfl2k5-presentation.js"></script>
 *
 * It delivers the game's broadcast STATE and EVENTS to the page. The game
 * only reports football facts ("TOUCHDOWN, home team"); how they look is
 * entirely up to the package.
 *
 *   NFL2K5.onState(fn)   fn(state, previousState) on every state update
 *   NFL2K5.onEvent(fn)   fn(event) for each event, e.g. { name: "TOUCHDOWN", team: "home" }
 *   NFL2K5.state         newest state (null until the first update)
 *   NFL2K5.host          "game" inside NFL2K5.exe, "editor" in Scorebug Studio, "browser" otherwise
 *
 * Inside the game, messages arrive from the WebView2 host. In the editor (or
 * any browser) nothing arrives on its own; the editor calls
 * NFL2K5.dispatch(message) with sample data. The full field list is in
 * docs/PRESENTATION-SYSTEM.md. */
(function () {
    'use strict';
    var stateHandlers = [], eventHandlers = [];
    var api = {
        version: 1,
        host: window.NFL2K5_HOST || (window.parent !== window ? 'editor' : 'browser'),
        state: null,
        /* True once the page has registered for game data (editor check). */
        get listening() { return stateHandlers.length > 0; },
        onState: function (fn) {
            stateHandlers.push(fn);
            if (api.state) safe(fn, api.state, null);
            return api;
        },
        onEvent: function (fn) { eventHandlers.push(fn); return api; },
        /* Feed one Presentation API message: { type: "state", state: {...} }
         * or { type: "event", event: {...} }. */
        dispatch: function (msg) {
            if (!msg || typeof msg !== 'object') return;
            if (msg.type === 'state' && msg.state) {
                var prev = api.state;
                api.state = msg.state;
                for (var i = 0; i < stateHandlers.length; i++) safe(stateHandlers[i], msg.state, prev);
            } else if (msg.type === 'event' && msg.event) {
                for (var j = 0; j < eventHandlers.length; j++) safe(eventHandlers[j], msg.event);
            }
        },
        /* Lines written here show up in the game's log as [HTMLPRES] page: ... */
        log: function (text) {
            if (window.chrome && window.chrome.webview) window.chrome.webview.postMessage(String(text));
            else if (window.console) console.log('[presentation]', text);
        }
    };

    function safe(fn, a, b) {
        try { fn(a, b); } catch (e) { api.log('handler error: ' + (e && e.stack || e)); }
    }

    if (window.chrome && window.chrome.webview) {
        window.chrome.webview.addEventListener('message', function (e) { api.dispatch(e.data); });
    }
    /* The editor talks to a package inside an iframe with postMessage. */
    window.addEventListener('message', function (e) {
        if (e.data && e.data.nfl2k5) api.dispatch(e.data.nfl2k5);
    });
    window.addEventListener('error', function (e) { api.log('script error: ' + e.message + ' @' + e.lineno); });

    window.NFL2K5 = api;
})();
