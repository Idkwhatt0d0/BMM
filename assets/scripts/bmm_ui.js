(function () {
  // BMM page script. Builds the Mods button + mod browser with createElement/textContent only
  // (manifest text is untrusted, so never innerHTML). One host call per page view: bmm.list.
  var FONT = "'Minecraft Seven','Segoe UI','Microsoft YaHei',sans-serif";
  var st = { mods: [], counts: {}, sel: -1, q: '', sort: 'name', libs: false, open: false, loaded: false };

  function $(id) { return document.getElementById(id); }
  function el(tag, css, text) {
    var e = document.createElement(tag);
    if (css) e.style.cssText = css;
    if (text != null) e.textContent = text;
    return e;
  }
  function px(n) { return Math.round(n) + 'px'; }
  function rank(m) { return m.type === 'native' ? 0 : m.type === 'preload-native' ? 1 : 2; }

  // ---- data -------------------------------------------------------------------------------
  function visible() {
    var q = st.q.toLowerCase(), out = [];
    st.mods.forEach(function (m, i) {
      if (m.library && !st.libs) return;
      var hay = (m.name + ' ' + m.description + ' ' + (m.authors || []).join(' ')).toLowerCase();
      if (q && hay.indexOf(q) < 0) return;
      out.push(i);
    });
    if (st.sort === 'type') out.sort(function (a, b) { return rank(st.mods[a]) - rank(st.mods[b]) || a - b; });
    return out;
  }

  // ---- widgets ----------------------------------------------------------------------------
  function badge(text, bg) {
    return el('span', 'display:inline-block;margin-left:6px;padding:1px 6px;border-radius:3px;font-size:0.75em;' +
      'color:#fff;background:' + bg + ';', text);
  }

  function nativeButton(label, w, h, onClick) {
    // Bevelled grey button approximating the game's own.
    var b = el('div',
      'position:relative;box-sizing:border-box;width:' + w + ';height:' + h + ';display:flex;align-items:center;' +
      'justify-content:center;cursor:pointer;user-select:none;pointer-events:auto;font-family:' + FONT + ';' +
      'font-size:' + px(Math.max(12, window.innerHeight * 0.022)) + ';color:#3c3c3e;background:#b1b2b5;' +
      'border:2px solid #1e1e1f;box-shadow:inset 0 3px #ecedef,inset 0 -4px #58585a;', label);
    b.onmouseenter = function () { b.style.background = '#c8c9cd'; };
    b.onmouseleave = function () { b.style.background = '#b1b2b5'; };
    b.onclick = onClick;
    return b;
  }

  function row(i, indent) {
    var m = st.mods[i];
    var r = el('div',
      'padding:6px 8px;margin:1px 0;cursor:pointer;pointer-events:auto;padding-left:' + px(8 + indent) + ';' +
      'background:' + (i === st.sel ? '#48494a' : 'transparent') + ';color:#fff;');
    r.appendChild(el('span', '', m.name));
    r.appendChild(el('span', 'color:#b1b2b5;margin-left:8px;font-size:0.85em;', m.version || ''));
    if (m.type === 'preload-native') r.appendChild(badge('Preload', '#96641e'));
    else if (m.type !== 'native') r.appendChild(badge(m.type || 'other', '#5a5a5a'));
    if (m.library) r.appendChild(badge('Library', '#3c5a96'));
    if (!m.enabled) r.appendChild(badge('Disabled', '#963232'));
    r.onclick = function () { st.sel = i; render(); };
    return r;
  }

  function listPane() {
    var pane = el('div', 'overflow-y:auto;height:100%;pointer-events:auto;');
    var idx = visible(), flat = st.q.length > 0;
    var shown = {}; idx.forEach(function (i) { shown[st.mods[i].name] = true; });
    idx.forEach(function (i) {
      var m = st.mods[i];
      if (!flat && m.parent && shown[m.parent]) return; // drawn under its parent
      pane.appendChild(row(i, 0));
      if (!flat) idx.forEach(function (c) { if (st.mods[c].parent === m.name) pane.appendChild(row(c, 18)); });
    });
    if (!idx.length) pane.appendChild(el('div', 'padding:12px;color:#b1b2b5;', 'No mods match.'));
    return pane;
  }

  function detailPane() {
    var pane = el('div', 'overflow-y:auto;height:100%;padding:10px;color:#fff;pointer-events:auto;');
    var m = st.mods[st.sel];
    if (!m) { pane.appendChild(el('div', 'color:#b1b2b5;', 'Select a mod')); return pane; }
    pane.appendChild(el('div', 'font-size:1.5em;margin-bottom:8px;', m.name));
    function line(k, v) {
      var d = el('div', 'margin:2px 0;');
      d.appendChild(el('span', 'color:#b1b2b5;', k + ': '));
      d.appendChild(el('span', '', v || 'unknown'));
      pane.appendChild(d);
    }
    line('Version', m.version);
    line('Authors', (m.authors || []).join(', '));
    line('Type', m.type);
    line('State', m.state);
    line('License', m.license);
    if ((m.deps || []).length) line('Depends on', m.deps.join(', '));
    pane.appendChild(el('div', 'margin:10px 0;line-height:1.35;', m.description || 'No description.'));
    pane.appendChild(el('div', 'color:#8b8b8d;font-size:0.8em;word-break:break-all;', m.folder));
    return pane;
  }

  function panel(root) {
    var W = window.innerWidth, H = window.innerHeight;
    var pw = W * 0.72, ph = H * 0.78;
    var back = el('div', 'position:absolute;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.6);pointer-events:auto;');
    back.onclick = function () { st.open = false; render(); };
    root.appendChild(back);

    var p = el('div',
      'position:absolute;box-sizing:border-box;left:' + px((W - pw) / 2) + ';top:' + px((H - ph) / 2) + ';width:' + px(pw) +
      ';height:' + px(ph) + ';display:flex;flex-direction:column;padding:12px;background:#313233;border:2px solid #1e1e1f;' +
      'color:#fff;font-family:' + FONT + ';font-size:' + px(Math.max(12, H * 0.02)) + ';pointer-events:auto;');
    p.onclick = function (e) { e.stopPropagation(); };

    var c = st.counts;
    var head = el('div', 'display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;');
    var title = (c.total || 0) + ' mods installed (' + (c.native || 0) + ' native, ' + (c.preload || 0) + ' preload-native' +
      (c.other ? ', ' + c.other + ' other' : '') + ')' + (!st.libs && c.libraries ? ' - ' + c.libraries + ' libraries hidden' : '');
    head.appendChild(el('div', '', title));
    head.appendChild(nativeButton('Done', px(pw * 0.1), px(ph * 0.06), function () { st.open = false; render(); }));
    p.appendChild(head);

    var bar = el('div', 'display:flex;align-items:center;gap:10px;margin-bottom:8px;');
    var input = el('input', 'flex:0 0 30%;padding:4px 6px;background:#1e1e1f;color:#fff;border:1px solid #58585a;font-family:' + FONT + ';font-size:inherit;');
    input.placeholder = 'Search'; input.value = st.q;
    input.oninput = function () { st.q = input.value; render(true); };
    bar.appendChild(input);
    bar.appendChild(nativeButton('Sort: ' + (st.sort === 'name' ? 'Name' : 'Type'), px(pw * 0.14), px(ph * 0.06),
      function () { st.sort = st.sort === 'name' ? 'type' : 'name'; render(); }));
    bar.appendChild(nativeButton(st.libs ? 'Libraries: shown' : 'Libraries: hidden', px(pw * 0.2), px(ph * 0.06),
      function () { st.libs = !st.libs; render(); }));
    p.appendChild(bar);

    var body = el('div', 'display:flex;flex:1;min-height:0;gap:8px;');
    var left = el('div', 'flex:0 0 42%;background:#242425;border:1px solid #58585a;min-height:0;');
    left.appendChild(listPane());
    var right = el('div', 'flex:1;background:#242425;border:1px solid #58585a;min-height:0;');
    right.appendChild(detailPane());
    body.appendChild(left); body.appendChild(right);
    p.appendChild(body);
    root.appendChild(p);
  }

  function render(keepFocus) {
    var root = $('bmm-root'); if (!root) return;
    root.textContent = '';
    var W = window.innerWidth, H = window.innerHeight;
    // Slot right of "Profile", measured on a 1456x819 title screen.
    var label = 'Mods' + (st.loaded ? ' (' + st.mods.length + ')' : '');
    var btn = nativeButton(label, px(W * 0.095), px(H * 0.054), function () { st.open = true; render(); });
    btn.style.position = 'absolute'; btn.style.left = px(W * 0.163); btn.style.top = px(H * 0.828);
    root.appendChild(btn);
    if (st.open) {
      panel(root);
      if (keepFocus) { var i = root.querySelector('input'); if (i) { i.focus(); try { i.setSelectionRange(i.value.length, i.value.length); } catch (e) {} } }
    }
  }

  // ---- boot -------------------------------------------------------------------------------
  var tries = 0;
  function boot() {
    tries++;
    if (!$('bmm-root') || !window.oreui) { if (tries < 100) setTimeout(boot, 50); return; }
    render();
    try {
      window.oreui.host.call('bmm.list', {}).then(function (res) {
        try {
          var p = JSON.parse(res);
          st.mods = p.mods || []; st.counts = p.counts || {}; st.loaded = true;
        } catch (e) {}
        render();
      }).catch(function () {});
    } catch (e) {}
    window.addEventListener('resize', function () { render(true); });
    window.addEventListener('keydown', function (e) { if (e.key === 'Escape' && st.open) { st.open = false; render(); } });
  }
  boot();
})();
