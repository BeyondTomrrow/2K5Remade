// Unit test for the EdgeHTML WebView shim in src/platform/uwp/uwp_webview_host.cpp
// (k_shim): runs it in Node with a minimal fake window/document and checks the
// chrome.webview polyfill, __nfl2k5_recv, __nfl2k5_busy and the error report.
//   node tools/uwp/test-webview-shim.js
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, '../../src/platform/uwp/uwp_webview_host.cpp'), 'utf8');
const m = src.match(/const char \*k_shim =\s*([\s\S]*?);\n/);
if (!m) throw new Error('k_shim not found');
// Join the C string literals.
let html = '';
for (const lit of m[1].replace(/\/\*[\s\S]*?\*\//g, '').matchAll(/"((?:[^"\\]|\\.)*)"/g)) html += JSON.parse('"' + lit[1] + '"');
const js = html.replace(/^<script>/, '').replace(/<\/script>$/, '');

let failures = 0;
function check(ok, name) { console.log((ok ? 'PASS ' : 'FAIL ') + name); if (!ok) failures++; }

// Fake browser globals.
const notified = [];
const listeners = {};
let observerCb = null;
const anims = [];
global.window = {
  external: { notify: (s) => notified.push(s) },
  addEventListener: (t, f) => { (listeners[t] = listeners[t] || []).push(f); },
};
global.document = { getAnimations: () => anims };
global.MutationObserver = function (cb) { observerCb = cb; this.observe = () => {}; };
const run = new Function('window', 'document', 'MutationObserver', js);
run(global.window, global.document, global.MutationObserver);
const w = global.window;

check(w.NFL2K5_HOST === 'game', 'NFL2K5_HOST is "game"');
check(w.chrome && w.chrome.webview && typeof w.chrome.webview.postMessage === 'function', 'chrome.webview polyfill');

const got = [];
const f = (e) => got.push(e.data);
w.chrome.webview.addEventListener('message', f);
w.__nfl2k5_recv({ type: 'state', state: { q: 1 } });
check(got.length === 1 && got[0].state.q === 1, '__nfl2k5_recv delivers to message listeners');
w.chrome.webview.removeEventListener('message', f);
w.__nfl2k5_recv({ type: 'event' });
check(got.length === 1, 'removeEventListener');

w.chrome.webview.postMessage('ack card 7');
check(notified.includes('ack card 7'), 'postMessage -> window.external.notify');

check(w.__nfl2k5_busy() === true, 'busy: first call true (initial draw)');
check(w.__nfl2k5_busy() === false, 'busy: still page -> false');
observerCb && observerCb([]);
check(w.__nfl2k5_busy() === true, 'busy: DOM mutation -> true');
check(w.__nfl2k5_busy() === false, 'busy: settles to false');
anims.push({ playState: 'running' });
check(w.__nfl2k5_busy() === true, 'busy: running animation -> true');
anims.length = 0;
check(w.__nfl2k5_busy() === false, 'busy: animation ended -> false');

// EdgeHTML 18: no document.getAnimations().
let now = Date.now() + 100000;
Date.now = () => now;
delete global.document.getAnimations;
const fire = (t) => (listeners[t] || []).forEach((h) => h({}));
w.__nfl2k5_busy();
now += 5000;
check(w.__nfl2k5_busy() === false, 'edge: still page -> false');
observerCb([]);
check(w.__nfl2k5_busy() === true, 'edge: mutation -> true');
now += 700;
check(w.__nfl2k5_busy() === true, 'edge: 0.7 s after a change (transition) -> still true');
now += 1000;
check(w.__nfl2k5_busy() === false, 'edge: 1.7 s after -> false');
fire('animationstart');
now += 10000;
check(w.__nfl2k5_busy() === true, 'edge: CSS animation running -> true');
fire('animationend');
now += 2000;
check(w.__nfl2k5_busy() === true, "edge: one last capture when the animation ends");
check(w.__nfl2k5_busy() === false, "edge: animation finished -> false");

(listeners.error || []).forEach((h) => h({ message: 'Unexpected token ?.', filename: 'https://presentation.local/x/app.js', lineno: 12 }));
check(notified.some((s) => s.startsWith('__nfl2k5_error Unexpected token ?.') && s.includes('app.js:12')), 'script errors reported to the host');

console.log(failures ? `${failures} failure(s)` : 'all passed');
process.exit(failures ? 1 : 0);
