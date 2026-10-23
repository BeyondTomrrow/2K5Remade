(function (root) {
  'use strict';
  var TAB_NAMES = ['Teams', 'Players', 'Tools', 'Scene'];
  var last = null;
  var pointerSequence = 0, pointerGeneration = -1, pointerHover = null;
  var renderedRows = [], rowNodes = [], tabSignature = '';
  var $ = function (id) { return root.document.getElementById(id); };
  var finite = function (n, fallback) { return typeof n === 'number' && isFinite(n) ? n : fallback; };
  var int = function (n, fallback) { return Math.trunc(finite(n, fallback)); };
  var text = function (value, fallback) { return typeof value === 'string' ? value.slice(0, 500) : fallback || ''; };
  var commandName = function (s) { return typeof s === 'string' && /^[a-z_]{1,32}$/.test(s); };

  function send(cmd, value) {
    if (!commandName(cmd)) return;
    if (value !== undefined && (typeof value !== 'number' || !isFinite(value) || Math.trunc(value) !== value)) return;
    value = int(value, 0);
    if (value < -1000000 || value > 1000000) return;
    var message = JSON.stringify({ cmd: cmd, value: value, generation: int(last && last.generation, 0) });
    if (root.chrome && root.chrome.webview) root.chrome.webview.postMessage(message);
    else root.dispatchEvent(new root.CustomEvent('sandboxcommand', { detail: JSON.parse(message) }));
  }

  function setupState(s) { return s.phase === 'setup' || s.screen === 'stadiums' || s.tab === 'stadiums'; }
  function tabs(s) {
    if (setupState(s)) return [{ id: 0, label: 'Stadiums', enabled: true }];
    if (Array.isArray(s.tabs) && s.tabs.length) return s.tabs.slice(0, 8).map(function (t, i) {
      return typeof t === 'string' ? { id: i, label: t, enabled: true } : { id: int(t.id, i), label: text(t.label, TAB_NAMES[i] || 'Tools'), enabled: t.enabled !== false };
    });
    return TAB_NAMES.map(function (label, id) { return { id: id, label: label, enabled: true }; });
  }

  function rows(s) {
    var data = Array.isArray(s.rows) ? s.rows : null;
    if (!data) {
      var key = setupState(s) ? 'stadiums' : ['teams', 'players', 'tools', 'scene'][int(s.tab, 0)];
      data = Array.isArray(s[key]) ? s[key] : [];
    }
    return data.slice(0, 512).map(function (item, index) {
      if (!item || typeof item !== 'object') item = {};
      var cmd = commandName(item.command) ? item.command : '';
      return {
        id: int(item.id, index), label: text(item.label, text(item.name, 'Item ' + (index + 1))),
        value: item.value == null ? '' : String(item.value).slice(0, 80), description: text(item.description),
        subtitle: text(item.subtitle, text(item.position)), command: cmd,
        commandValue: int(item.commandValue, int(item.id, index)), enabled: item.enabled !== false && (!!cmd || item.enabled === true),
        adjustable: item.adjustable === true,
        min: finite(item.min, null), max: finite(item.max, null), step: finite(item.step, 1),
        note: text(item.note, item.enabled === false ? text(item.reason, 'Unavailable in this scene.') : ''),
        details: item.details && typeof item.details === 'object' ? item.details : null,
        mark: text(item.mark), index: index
      };
    });
  }

  function selected(s, list) { return list[Math.max(0, Math.min(list.length - 1, int(s.selection, 0)))]; }
  function adjust(row, direction) {
    if (!row || !row.enabled) return;
    /* The core owns per-field increments (five seconds, one yard, etc.).
     * A relative row must not be converted to an absolute command value. */
    if (row.adjustable) { send('adjust', direction < 0 ? -1 : 1); return; }
    if (row.min === null || row.max === null) return;
    var n = Number(row.value);
    if (!isFinite(n)) return;
    if (row.command) send(row.command, Math.max(row.min, Math.min(row.max, n + direction * row.step)));
    else send('adjust', direction);
  }
  function activate(row) { if (row && row.enabled) send(row.command || 'select', row.command ? row.commandValue : 0); }
  function setText(element, value) { if (element.textContent !== value) element.textContent = value; }
  function makeRowNode(index) {
    var b = root.document.createElement('button'); b.type = 'button';
    b.setAttribute('role', 'option'); b.setAttribute('data-sandbox-row', 'true');
    var number = root.document.createElement('span'); number.className = 'row-index';
    var label = root.document.createElement('span'); label.className = 'row-title';
    var name = root.document.createElement('span');
    var sub = root.document.createElement('span'); sub.className = 'row-subtitle';
    label.appendChild(name); label.appendChild(sub);
    var value = root.document.createElement('span'); value.className = 'row-value';
    var arrow = root.document.createElement('span'); arrow.className = 'row-arrow';
    b.appendChild(number); b.appendChild(label); b.appendChild(value); b.appendChild(arrow);
    /* Resolve the current authoritative row at event time. Camera/FOV
     * snapshots can change while a pointer remains over the same DOM node. */
    b.addEventListener('click', function () {
      var current = selected(last || {}, renderedRows);
      if (renderedRows[index]) send('navigate', index - (current ? current.index : 0));
    });
    b.addEventListener('dblclick', function () { activate(renderedRows[index]); });
    return { button: b, number: number, name: name, sub: sub, value: value, arrow: arrow };
  }

  function addPrompt(parent, key, label, cls, ps) {
    var prompt = root.document.createElement('span'); prompt.className = 'prompt';
    var k = root.document.createElement('span'); k.className = 'key ' + (cls || '') + (ps ? ' ps' : ''); k.textContent = key;
    var l = root.document.createElement('span'); l.textContent = label;
    prompt.appendChild(k); prompt.appendChild(l); parent.appendChild(prompt);
  }
  function buttonPrompts(s) {
    var ps = s.style === 'playstation' || s.style === 'ps' || s.style === 1 || s.style === 2;
    var p = $('prompts'); p.replaceChildren();
    addPrompt(p, ps ? '×' : 'A', 'Select', 'select', ps);
    addPrompt(p, ps ? '○' : 'B', 'Back', 'back', ps);
    addPrompt(p, 'D-pad', 'Browse / adjust', '', false);
    addPrompt(p, ps ? 'L1 / R1' : 'LB / RB', 'Categories', '', false);
    addPrompt(p, ps ? 'Options' : 'Menu', 'Close browser', '', false);
    $('quick-prompt').textContent = s.grabMode ?
      (ps ? 'Left stick Move · L2/R2 Height · Right stick Turn · × Drop · ○ Cancel · Options Browser' :
            'LS Move · LT/RT Height · RS Turn · A Drop · B Cancel · Menu Browser') : s.reticle === false ?
      (ps ? 'Options Browser · Share Pause' : 'Menu Browser · View Pause') :
      (ps ? 'Left stick Move · Right stick Look · L2/R2 Height · L1/R1 Zoom · Options Browser · Share Pause · △ Step' :
            'LS Move · RS Look · LT/RT Height · LB/RB Zoom · Menu Browser · View Pause · Y Step');
  }
  function details(row) {
    var d = $('item-details'); d.replaceChildren();
    if (!row || !row.details) return;
    Object.keys(row.details).slice(0, 6).forEach(function (key) {
      var dt = root.document.createElement('dt'), dd = root.document.createElement('dd');
      dt.textContent = key.slice(0, 40); dd.textContent = String(row.details[key]).slice(0, 60); d.appendChild(dt); d.appendChild(dd);
    });
  }

  function render(s) {
    if (!s || typeof s !== 'object') s = {};
    last = s;
    var active = s.active === true || s.active === 1;
    var open = active && (s.menuOpen === true || s.menuOpen === 1 || setupState(s));
    $('sandbox').hidden = !active || !!s.photoMode;
    $('browser').hidden = !open;
    $('quick-status').hidden = open || !!s.hideHints;
    $('reticle').hidden = open || s.reticle === false;
    if (!open) {
      $('mouse-pointer').hidden = true;
      if (pointerHover) pointerHover.classList.remove('pointer-hover');
      pointerHover = null;
    }
    var speed = finite(s.speedPercent, finite(s.speed, 100)) / 100;
    var sim = s.paused ? 'PAUSED' : speed === 1 ? 'PLAYING' : String(speed) + '× SPEED';
    $('simulation').textContent = sim;
    $('mode-label').textContent = s.grabMode ? 'PHOTO POSING' : 'SANDBOX';
    $('quick-sim').textContent = s.grabMode && text(s.selectedPlayer) ? text(s.selectedPlayer).slice(0, 64) + ' · ' + sim : sim;
    $('location').textContent = text(s.stadium, text(s.location));
    $('title').textContent = setupState(s) ? 'CHOOSE A STADIUM' : 'SANDBOX';
    $('message').textContent = text(s.message);
    var ts = tabs(s), current = int(s.tab, 0), tabNav = $('tabs');
    var nextTabSignature = JSON.stringify([ts, current, setupState(s)]);
    if (nextTabSignature !== tabSignature) {
      tabSignature = nextTabSignature; tabNav.replaceChildren();
      ts.forEach(function (t) {
        var b = root.document.createElement('button'); b.type = 'button'; b.textContent = t.label;
        b.setAttribute('role', 'tab'); b.setAttribute('aria-selected', String(t.id === current || setupState(s)));
        b.disabled = !t.enabled;
        b.addEventListener('click', function () { send('tab', t.id - int(last && last.tab, 0)); });
        tabNav.appendChild(b);
      });
    }
    var list = rows(s), choice = selected(s, list), rowBox = $('rows'), priorScroll = rowBox.scrollTop;
    renderedRows = list;
    if (rowNodes.length !== list.length) {
      while (rowNodes.length < list.length) rowNodes.push(makeRowNode(rowNodes.length));
      rowNodes.length = list.length;
      rowBox.replaceChildren.apply(rowBox, rowNodes.map(function (n) { return n.button; }));
    }
    list.forEach(function (row, i) {
      var node = rowNodes[i], b = node.button;
      b.className = (choice === row ? 'selected' : '') + (pointerHover === b ? ' pointer-hover' : '');
      b.setAttribute('aria-selected', String(choice === row)); b.setAttribute('aria-disabled', String(!row.enabled));
      setText(node.number, String(i + 1).padStart(2, '0'));
      setText(node.name, row.label); setText(node.sub, row.subtitle); node.sub.hidden = !row.subtitle;
      setText(node.value, row.value); setText(node.arrow, row.enabled ? '›' : '—');
    });
    rowBox.scrollTop = priorScroll;
    if (choice) {
      var el = rowBox.children[choice.index];
      if (el && (el.offsetTop < rowBox.scrollTop || el.offsetTop + el.offsetHeight > rowBox.scrollTop + rowBox.clientHeight)) el.scrollIntoView({ block: 'nearest' });
    }
    $('empty').hidden = !!list.length;
    $('empty').textContent = current === 1 ? 'Players appear here when the field scene is ready.' : 'No items available in this scene.';
    $('count').textContent = list.length ? String((choice ? choice.index : 0) + 1) + ' / ' + list.length : '0 items';
    $('page-up').disabled = !choice || choice.index === 0;
    $('page-down').disabled = !choice || choice.index === list.length - 1;
    $('page-up').onclick = function () { if (choice) send('navigate', -Math.min(6, choice.index)); };
    $('page-down').onclick = function () { if (choice) send('navigate', Math.min(6, list.length - 1 - choice.index)); };
    $('category').textContent = text(s.category, setupState(s) ? 'Available stadiums' : (ts.find(function (t) { return t.id === current; }) || ts[0]).label);
    $('item-type').textContent = setupState(s) ? 'MAP SELECT' : $('category').textContent.toUpperCase();
    $('item-title').textContent = choice ? choice.label : 'Nothing selected';
    $('item-mark').textContent = choice && choice.mark ? choice.mark : setupState(s) ? 'MAP' : ['TEAM', 'PLAYER', 'TOOL', 'SCENE'][current] || '2K5';
    $('item-description').textContent = choice ? choice.description || 'Select this item to edit the sandbox scene.' : 'This category has no available items.';
    $('capability-note').hidden = !choice || !choice.note;
    $('capability-note').textContent = choice ? choice.note : '';
    $('activate').disabled = !choice || !choice.enabled;
    $('activate').textContent = setupState(s) ? 'Load stadium' : choice && choice.adjustable ? 'Next value' : choice && choice.min !== null ? 'Apply' : 'Select';
    $('activate').onclick = function () { activate(choice); };
    var adjustable = choice && choice.enabled && (choice.adjustable || (choice.min !== null && choice.max !== null));
    $('decrease').disabled = !adjustable; $('increase').disabled = !adjustable;
    $('decrease').onclick = function () { adjust(choice, -1); }; $('increase').onclick = function () { adjust(choice, 1); };
    $('value').textContent = adjustable ? choice.value : '';
    details(choice); buttonPrompts(s);
  }

  function pointer(event) {
    if (!event || !last || !last.active || !last.menuOpen || last.photoMode) return false;
    var generation = int(event.generation, -1), current = int(last.generation, 0);
    if (generation !== current) return false;
    if (pointerGeneration !== generation) { pointerGeneration = generation; pointerSequence = 0; }
    var sequence = int(event.sequence, 0);
    if (sequence <= pointerSequence) return false;
    pointerSequence = sequence;
    var cursor = $('mouse-pointer');
    if (!event.visible) { cursor.hidden = true; return true; }
    var x = finite(event.x, -1), y = finite(event.y, -1);
    if (x < 0 || x >= 1920 || y < 0 || y >= 1080) { cursor.hidden = true; return false; }
    cursor.hidden = false; cursor.style.left = x + 'px'; cursor.style.top = y + 'px';
    var scale = Math.min(root.innerWidth / 1920, root.innerHeight / 1080);
    var target = root.document.elementFromPoint(x * scale, y * scale);
    var button = target && typeof target.closest === 'function' ? target.closest('button') : null;
    if (button && !$('browser').contains(button)) button = null;
    var disabled = button && (button.disabled || button.getAttribute('aria-disabled') === 'true');
    /* Unavailable rows may still be selected to read their explanation. The
     * row's activate handler remains disabled, including on double-click. */
    var inspectOnly = button && button.getAttribute('data-sandbox-row') === 'true';
    var blocked = !button || button.disabled || (disabled && !inspectOnly);
    cursor.className = blocked ? 'blocked' : '';
    if (pointerHover && pointerHover !== button) pointerHover.classList.remove('pointer-hover');
    pointerHover = button;
    if (button) button.classList.add('pointer-hover');
    var pressed = int(event.pressed, 0) & 7;
    if (pressed & 2) { send('back', 0); return true; }
    if (pressed & 4) { send('toggle_menu', 0); return true; }
    if ((pressed & 1) && !blocked) {
      if (event.doubleClick && inspectOnly && !disabled) button.dispatchEvent(new root.MouseEvent('dblclick', { bubbles: true }));
      else if (event.doubleClick && inspectOnly && disabled) return true;
      else button.click();
    }
    return true;
  }

  function fit() { $('sandbox').style.transform = 'scale(' + Math.min(root.innerWidth / 1920, root.innerHeight / 1080) + ')'; }
  root.addEventListener('resize', fit);
  root.addEventListener('keydown', function (e) {
    if (!last || !last.active) return;
    var key = { ArrowUp: ['navigate', -1], ArrowDown: ['navigate', 1], ArrowLeft: ['adjust', -1], ArrowRight: ['adjust', 1], Enter: ['select', 0], Escape: ['back', 0], Tab: ['tab', e.shiftKey ? -1 : 1] }[e.key];
    if (key) { e.preventDefault(); send(key[0], key[1]); }
  });
  if (root.chrome && root.chrome.webview) root.chrome.webview.addEventListener('message', function (event) {
    var data = event.data;
    if (data && data.type === 'state') render(data.state);
    else if (data && data.type === 'event' && data.event && data.event.type === 'sandboxPointer') pointer(data.event);
  });
  /* Browser review/test harness. No sample scene is injected into game use. */
  root.NFL2K5SandboxUI = { render: render, rows: rows, tabs: tabs, send: send, setupState: setupState, pointer: pointer };
  fit(); render({ active: false });
})(window);
