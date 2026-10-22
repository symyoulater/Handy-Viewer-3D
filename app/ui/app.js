// Thin Place VR - app interface.
// Talks to the app through WebView2 messages (see app/Bridge.cs):
//   send(cmd, args) -> reply;  "state" (settings, games, updates) and "live" (game link) are pushed.
(function () {
  'use strict';

  // Action numbers understood by the VR layer (src/hotkeys.h).
  const A = {
    ToggleAll: 3, Place: 5, Centre: 7, Invert: 8,
    Left: 9, Right: 10, Up: 11, Down: 12, Away: 13, Closer: 14,
    TiltUp: 15, TiltDown: 16, TurnLeft: 17, TurnRight: 18, RollLeft: 19, RollRight: 20,
    Bigger: 21, Smaller: 22, Select: 23,
  };

  const S = { state: null, live: { running: false }, page: 'home', tab: 0, fine: false, key: '' };
  const main = document.getElementById('main');
  const pending = new Map();
  let nextId = 1;

  // ------------------------------------------------------------ messaging ----
  function send(cmd, args) {
    const id = nextId++;
    return new Promise((resolve) => {
      pending.set(id, resolve);
      window.chrome.webview.postMessage({ id, cmd, args: args || {} });
    }).then((r) => {
      if (!r.ok && r.error) toast(r.error, true);
      return r;
    });
  }

  window.chrome.webview.addEventListener('message', (e) => {
    const m = e.data;
    if (!m) return;
    if (m.type === 'reply') {
      const done = pending.get(m.id);
      pending.delete(m.id);
      if (done) done(m);
    } else if (m.type === 'state') {
      S.state = m;
      refresh();
    } else if (m.type === 'live') {
      S.live = m;
      patchLive();
    }
  });

  let toastTimer = 0;
  function toast(text, err) {
    const t = document.getElementById('toast');
    t.textContent = text;
    t.className = 'toast show' + (err ? ' err' : '');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.className = 'toast' + (err ? ' err' : ''); }, err ? 6000 : 2500);
  }

  // -------------------------------------------------------------- helpers ----
  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const cfg = () => (S.state && S.state.config) || null;
  const wheel = () => {
    const c = cfg();
    if (!c) return null;
    return c.wheels.find((w) => w.id === c.activeWheel) || c.wheels[0] || null;
  };
  // Games and runtimes often report short internal names; show the ones people know.
  const NAMES = { acevo: 'Assetto Corsa EVO', virtualdesktopxr: 'Virtual Desktop (VDXR)', 'le mans ultimate': 'Le Mans Ultimate', elitedangerous: 'Elite Dangerous' };
  const friendly = (n) => NAMES[String(n || '').toLowerCase()] || n;
  const deg = (v) => (v < 0 ? '−' : '') + Math.abs(v).toFixed(1) + '°';
  const when = (iso) => {
    if (!iso) return '';
    const d = new Date(iso);
    if (isNaN(d)) return iso;
    const today = new Date();
    const hm = d.toTimeString().slice(0, 5);
    if (d.toDateString() === today.toDateString()) return 'today ' + hm;
    const y = new Date(today); y.setDate(today.getDate() - 1);
    if (d.toDateString() === y.toDateString()) return 'yesterday ' + hm;
    return d.toLocaleDateString(undefined, { day: 'numeric', month: 'short' }) + ' ' + hm;
  };

  function sw(id, checked, label, setKey, extra) {
    return `<label class="switch">${label ? `<span style="color:var(--text-2)">${esc(label)}</span>` : ''}
      <input type="checkbox" id="${id}" ${checked ? 'checked' : ''} ${setKey ? `data-set="${setKey}"` : ''} ${extra || ''} aria-label="${esc(label || id)}"><span class="track"></span></label>`;
  }

  function rimSvg(path, opts) {
    const o = Object.assign({ w: '100%', h: 70, fill: 0.18, stroke: 3, colour: 'var(--accent)', margin: 0, id: '' }, opts);
    if (!path) {
      return `<svg width="${o.w}" height="${o.h}" viewBox="-180 -180 360 360" aria-hidden="true"><circle r="155" fill="none" stroke="${o.colour}" stroke-width="${o.stroke * 2}"/></svg>`;
    }
    // Fit the view to the outline, allowing room for the hand margin and for turning.
    const nums = path.match(/-?\d+(\.\d+)?/g).map(Number);
    let r = 0;
    for (let i = 0; i + 1 < nums.length; i += 2) r = Math.max(r, Math.hypot(nums[i], nums[i + 1]));
    const ext = (o.margin ? r : Math.max(...nums.map(Math.abs))) + 8 + o.margin;
    const vh = o.margin ? ext : Math.max(...nums.filter((_, i) => i % 2).map(Math.abs)) + 8;
    return `<svg width="${o.w}" height="${o.h}" viewBox="${-ext} ${-vh} ${ext * 2} ${vh * 2}" aria-hidden="true">
      <g ${o.id ? `id="${o.id}"` : ''}>
      ${o.margin ? `<path d="${path}" fill="none" stroke="${o.colour}" stroke-opacity="0.32" stroke-width="${o.margin * 2}" stroke-linejoin="round"/>` : ''}
      <path d="${path}" fill="${o.colour}" fill-opacity="${o.fill}" stroke="${o.margin ? '#ECEEF2' : o.colour}" stroke-width="${o.stroke}"/>
      ${o.margin ? '<circle r="3" fill="#0D0E11" stroke="#ECEEF2" stroke-width="1"/>' : ''}
      </g></svg>`;
  }

  // ---------------------------------------------------------------- pages ----
  function noConfig() {
    return `<div class="banner">Settings appear here after Thin Place VR has run in a game once. Start a VR game from Virtual Desktop, then come back.</div>`;
  }

  function pageHome() {
    const st = S.state, c = cfg();
    const games = (st.games || []).filter((g) => !/check/i.test(g.exe));
    const w = wheel();
    return `
      <div class="head">
        <div><div class="eyebrow">Status</div><h1 data-v="homeTitle"></h1></div>
        ${c ? sw('enabled', c.enabled, 'Windows visible in VR', 'enabled') : ''}
      </div>
      ${!st.layerActive ? `<div class="banner">Thin Place VR isn't registered with Windows, so games won't load it. Reinstall Thin Place VR to fix this.</div>` : ''}
      <section class="grid3" aria-label="System status">
        <div class="card tile"><div class="k"><span class="dot" data-v="layerDot"></span>VR layer</div><div class="v" data-v="layerText"></div><div class="muted">Loads into OpenXR games</div></div>
        <div class="card tile"><div class="k"><span class="dot" data-v="rtDot"></span>Runtime</div><div class="v" data-v="rtText"></div><div class="muted" data-v="rtSub"></div></div>
        <div class="card tile"><div class="k"><span class="dot" data-v="baseDot"></span>Wheel base</div><div class="v" data-v="baseText"></div><div class="muted" data-v="baseSub"></div></div>
      </section>
      <div class="split">
        <section class="card" aria-labelledby="win-h">
          <div class="top"><h2 id="win-h">Your windows</h2><button class="linkbtn" data-act="go" data-page="windows">Adjust</button></div>
          ${!c ? noConfig() : `
          <div class="item">
            <div class="ic">${rimSvg(w && w.shape, { w: 32, h: 22, stroke: 10, fill: 0 })}</div>
            <div class="body"><span style="font-weight:500">Wheel</span><span class="muted">${esc(w ? w.name : '')}${w && w.rotates ? ' · follows steering' : ''}</span></div>
            <span class="muted ${c.wheelPlaced ? 'good-t' : 'warn-t'}">${c.wheelPlaced ? 'Placed' : 'Needs placing'}</span>
          </div>
          ${c.windows.map((x) => `
          <div class="item">
            <div class="ic"><svg width="24" height="18" viewBox="0 0 24 18" aria-hidden="true"><rect x="1" y="1" width="22" height="16" rx="3" fill="none" stroke="var(--accent)" stroke-width="1.6"/></svg></div>
            <div class="body"><span style="font-weight:500">${esc(x.name)}</span><span class="muted">${Math.round(x.width)} × ${Math.round(x.height)} mm</span></div>
            <span class="muted">${!x.enabled ? 'Hidden' : x.onlyWhileAdjusting ? 'While adjusting' : 'Shown'}</span>
          </div>`).join('')}
          <button class="btn dashed" style="align-self:flex-start;margin-top:auto" data-act="addWindow">+ Add a window</button>`}
        </section>
        <section class="card" aria-labelledby="games-h">
          <h2 id="games-h">Recent games</h2>
          <div class="rows">
            ${games.length ? games.slice(0, 6).map((g) => `<div><span>${esc(g.name)}</span><span class="muted">${esc(when(g.lastUsed))}</span></div>`).join('')
              : '<div><span class="muted">No games yet. Start one in VR and it will appear here.</span></div>'}
          </div>
          <p class="muted" style="margin:auto 0 0;line-height:1.6">Games with anti-cheat (such as Le Mans Ultimate) block Thin Place VR until it is digitally signed.<br>
          In VR: <span class="mono" style="color:var(--text)">Ctrl+Alt+H</span> adjust windows · <span class="mono" style="color:var(--text)">Ctrl+Alt+V</span> hide them</p>
        </section>
      </div>`;
  }

  function pageWindows() {
    const c = cfg();
    if (!c) return `<div><div class="eyebrow">Windows</div><h1>Windows</h1></div>${noConfig()}`;
    if (S.tab > c.windows.length) S.tab = 0;
    const isWheel = S.tab === 0;
    const w = wheel();
    const win = isWheel ? null : c.windows[S.tab - 1];
    const title = isWheel ? 'Wheel' : win.name;
    const tabs = [`<button class="tab ${isWheel ? 'on' : ''}" data-act="tab" data-i="0">Wheel</button>`]
      .concat(c.windows.map((x, i) => `<button class="tab ${S.tab === i + 1 ? 'on' : ''}" data-act="tab" data-i="${i + 1}">${esc(x.name)}</button>`))
      .concat([(c.windows.some((x) => /keyboard/i.test(x.name)) ? '' : '<button class="tab" data-act="addKeyboard">+ Keyboard</button>'),
        '<button class="tab" data-act="addWindow" aria-label="Add a window">+</button>']).join('');

    const preview = isWheel
      ? `${rimSvg(w && w.shape, { w: 480, h: 340, fill: 0.55, stroke: 1, margin: c.margin, id: 'rimrot' })}
         <div class="corner"><span class="muted">Live steering</span><span class="mono" style="font-size:22px" data-live="steer">—</span></div>
         <div class="legend"><span><span class="sw" style="opacity:.8"></span>Rim</span><span><span class="sw" style="opacity:.35"></span>Space for hands · ${Math.round(c.margin)} mm</span></div>`
      : `<svg width="460" height="320" viewBox="${-win.width / 2 - 40} ${-win.height / 2 - 40} ${win.width + 80} ${win.height + 80}" aria-hidden="true" style="max-width:90%">
           <rect x="${-win.width / 2}" y="${-win.height / 2}" width="${win.width}" height="${win.height}" rx="2" fill="var(--accent)" fill-opacity="0.55" stroke="#ECEEF2" stroke-width="${Math.max(win.width, win.height) / 300}"/></svg>
         <div class="corner"><span class="muted">Size</span><span class="mono" style="font-size:22px">${Math.round(win.width)} × ${Math.round(win.height)} mm</span></div>`;

    const shape = isWheel ? `
        <div class="slider"><div class="t"><span>Space for hands</span><span data-v="marginV"></span></div>
          <input type="range" min="0" max="150" step="5" data-set="margin" data-fmt="mm" aria-label="Space for hands"></div>
        <div class="slider"><div class="t"><span>Size</span><span data-v="scaleV"></span></div>
          <input type="range" min="50" max="200" step="1" data-set="wheelScale" data-fmt="pct" aria-label="Wheel window size"></div>`
      : `
        <div class="field"><label for="wname" class="muted">Name</label><input id="wname" class="text-in" style="max-width:190px" value="${esc(win.name)}" data-win="name"></div>
        <div class="field"><label for="ww">Width (mm)</label><input id="ww" class="num-in" type="number" min="20" max="800" value="${Math.round(win.width)}" data-win="width"></div>
        <div class="field"><label for="wh">Height (mm)</label><input id="wh" class="num-in" type="number" min="20" max="800" value="${Math.round(win.height)}" data-win="height"></div>
        <div class="field"><span>Show in VR</span>${sw('wen', win.enabled, '', null, 'data-win="enabled"')}</div>
        <div class="field"><div class="lbl"><span>Only while adjusting</span><span class="muted">Appears in edit mode (Ctrl+Alt+H) or for 20 s after using these buttons</span></div>${sw('wadj', win.onlyWhileAdjusting, '', null, 'data-win="onlyWhileAdjusting"')}</div>
        <button class="btn danger" data-act="removeWindow">Remove this window</button>`;

    return `
      <div class="head"><div><div class="eyebrow">Windows</div><h1>${esc(title)}</h1></div>
        <span class="muted" data-v="linkState"></span></div>
      <div class="tabs">${tabs}</div>
      <div class="split-r">
        <section class="preview" aria-label="Window preview">${preview}</section>
        <div style="display:flex;flex-direction:column;gap:16px">
          <section class="card" aria-labelledby="pos-h">
            <div class="top"><h2 id="pos-h">Position</h2>
              <div class="seg" role="group" aria-label="Step size"><button data-act="fine" data-f="1" class="${S.fine ? 'on' : ''}">Fine</button><button data-act="fine" data-f="0" class="${S.fine ? '' : 'on'}">Normal</button></div></div>
            <button class="btn primary" data-act="do" data-a="${A.Place}">Place where I'm looking</button>
            <div class="pad">
              <button class="small" data-act="do" data-a="${A.Closer}">Closer</button>
              <button data-act="do" data-a="${A.Up}" aria-label="Move up">↑</button>
              <button class="small" data-act="do" data-a="${A.Away}">Away</button>
              <button data-act="do" data-a="${A.Left}" aria-label="Move left">←</button>
              <button data-act="do" data-a="${A.Down}" aria-label="Move down">↓</button>
              <button data-act="do" data-a="${A.Right}" aria-label="Move right">→</button>
            </div>
            <div class="pm"><span>Tilt</span><button data-act="do" data-a="${A.TiltDown}" aria-label="Tilt towards you">−</button><button data-act="do" data-a="${A.TiltUp}" aria-label="Tilt away">+</button></div>
            <div class="pm"><span>Turn</span><button data-act="do" data-a="${A.TurnRight}" aria-label="Turn right">−</button><button data-act="do" data-a="${A.TurnLeft}" aria-label="Turn left">+</button></div>
            <div class="pm"><span>Roll</span><button data-act="do" data-a="${A.RollRight}" aria-label="Roll right">−</button><button data-act="do" data-a="${A.RollLeft}" aria-label="Roll left">+</button></div>
            <p class="muted" style="margin:0;line-height:1.5">Works while a game is running in VR. Point your head at where the window should go, then press Place — Virtual Desktop's desktop view lets you click these without taking the headset off.</p>
          </section>
          <section class="card" aria-labelledby="shape-h"><h2 id="shape-h">${isWheel ? 'Shape' : 'Window'}</h2>${shape}</section>
        </div>
      </div>`;
  }

  function pageWheel() {
    const c = cfg();
    if (!c) return `<div><div class="eyebrow">Wheel</div><h1>Rim and steering</h1></div>${noConfig()}`;
    return `
      <div><div class="eyebrow">Wheel</div><h1>Rim and steering</h1></div>
      <section aria-labelledby="rim-h" style="display:flex;flex-direction:column;gap:12px">
        <h2 id="rim-h">Fitted rim</h2>
        <div class="grid3">
          ${c.wheels.map((w) => `
          <button class="rim ${w.id === c.activeWheel ? 'on' : ''}" data-act="rim" data-id="${esc(w.id)}" aria-pressed="${w.id === c.activeWheel}">
            ${rimSvg(w.shape, { colour: w.id === c.activeWheel ? 'var(--accent)' : '#5A606B', fill: w.id === c.activeWheel ? 0.18 : 0 })}
            <span style="display:flex;justify-content:space-between;align-items:center"><span style="font-weight:500">${esc(w.name)}</span>${w.id === c.activeWheel ? '<span class="muted" style="color:var(--accent)">In use</span>' : ''}</span>
            <span class="muted">${w.rotates ? 'Turns with the wheel' : 'Round rim — no turning needed'} · size ${w.scale}%</span>
          </button>`).join('')}
          <div class="rim" style="border-style:dashed;align-items:center;justify-content:center;text-align:center">
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="var(--text-2)" stroke-width="1.6" aria-hidden="true"><path d="M12 5v14M5 12h14"/></svg>
            <span style="color:var(--text-2)">Add a rim</span><span class="muted">Send Claude a front-on photo of the rim and it will be added in an update.</span>
          </div>
        </div>
      </section>
      <div class="grid2">
        <section class="card" aria-labelledby="steer-h">
          <h2 id="steer-h">Steering</h2>
          <div class="field"><div class="lbl"><span class="muted">Wheel base</span><span style="font-weight:500" data-live="base">—</span></div><span class="muted" data-live="baseState"></span></div>
          <div class="field"><label class="lbl" for="range"><span>Steering range</span><span class="muted">Match Simucube Tuner</span></label>
            <span style="display:flex;align-items:center;gap:6px"><input id="range" class="num-in" type="number" min="90" max="3600" step="10" data-set="range"><span class="muted">°</span></span></div>
          <div class="field"><div class="lbl"><span>Flip direction</span><span class="muted">If the window turns the wrong way</span></div>${sw('invert', c.invert, '', 'invert')}</div>
        </section>
        <section class="card" aria-labelledby="angle-h">
          <h2 id="angle-h">Live angle</h2>
          <div style="flex:1;display:flex;align-items:center;justify-content:center;gap:28px">
            <svg width="140" height="140" viewBox="-70 -70 140 140" aria-hidden="true">
              <circle r="62" fill="none" stroke="#22252B" stroke-width="2"/>
              <g id="dial"><line x1="0" y1="-62" x2="0" y2="-44" stroke="var(--accent)" stroke-width="3" stroke-linecap="round"/></g>
              <line x1="0" y1="-68" x2="0" y2="-60" stroke="#5A606B" stroke-width="1"/>
            </svg>
            <div style="display:flex;flex-direction:column;gap:4px"><span class="mono" style="font-size:34px;letter-spacing:-1px" data-live="steer">—</span><span class="muted" data-v="rangeHalf"></span></div>
          </div>
          <button class="btn" data-act="centre">Set centre — hold the wheel straight</button>
        </section>
      </div>`;
  }

  function pagePassthrough() {
    const c = cfg(), st = S.state;
    const k = c ? c.keyColour : [255, 0, 255];
    const vd = /Virtual Desktop/.test(st.activeRuntime);
    const tick = (ok) => ok
      ? '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#4CC38A" stroke-width="2" aria-hidden="true"><path d="M5 12l5 5 9-10"/></svg>'
      : '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#5A606B" stroke-width="2" aria-hidden="true"><circle cx="12" cy="12" r="8"/></svg>';
    return `
      <div><div class="eyebrow">Passthrough</div><h1>How the windows show your room</h1></div>
      <div class="grid2">
        <section class="card" aria-labelledby="key-h">
          <h2 id="key-h">Key colour</h2>
          <p style="margin:0;color:var(--text-2);line-height:1.5">Virtual Desktop swaps this colour for your headset's cameras. It must match the colour set under Configure in Virtual Desktop's passthrough settings.</p>
          <div style="display:flex;align-items:center;gap:14px">
            <span style="width:52px;height:52px;border-radius:10px;border:1px solid var(--line-2);background:rgb(${k[0]},${k[1]},${k[2]})"></span>
            <div style="display:flex;flex-direction:column;gap:4px"><span class="mono muted">R ${k[0]} · G ${k[1]} · B ${k[2]}</span></div>
            <button class="btn" style="margin-left:auto" data-act="colour" ${c ? '' : 'disabled'}>Change</button>
          </div>
        </section>
        <section class="card" aria-labelledby="vd-h">
          <h2 id="vd-h">Virtual Desktop checklist</h2>
          <div class="check">${tick(vd)}<span>OpenXR runtime is Virtual Desktop${vd ? '' : ` <span class="warn-t">— currently ${esc(st.activeRuntime)}</span>`}</span></div>
          <div class="check">${tick(false)}<span style="color:var(--text-2)">Frame rate 90 fps or lower</span></div>
          <div class="check">${tick(false)}<span style="color:var(--text-2)">Passthrough: Environment ticked</span></div>
          <div class="check">${tick(false)}<span style="color:var(--text-2)">Similarity 4% · Smoothness 10% · Opacity 100%</span></div>
          <p class="muted" style="margin:0">Only the runtime can be checked from here; the rest are set in the headset.</p>
        </section>
      </div>
      <div class="grid2">
        <section class="card ${c ? '' : 'disabled'}" aria-labelledby="edit-h">
          <h2 id="edit-h">Placing and nudging</h2>
          <div class="field"><label class="lbl" for="dist"><span>Place distance</span><span class="muted">About your eyes-to-wheel distance</span></label>
            <span style="display:flex;align-items:center;gap:6px"><input id="dist" class="num-in" type="number" min="20" max="150" data-set="distanceCm"><span class="muted">cm</span></span></div>
          <div class="field"><label for="nmm">Move step</label><span style="display:flex;align-items:center;gap:6px"><input id="nmm" class="num-in" type="number" min="1" max="50" data-set="nudgeMm"><span class="muted">mm</span></span></div>
          <div class="field"><label for="ndeg">Turn step</label><span style="display:flex;align-items:center;gap:6px"><input id="ndeg" class="num-in" type="number" min="0.25" max="10" step="0.25" data-set="nudgeDeg"><span class="muted">°</span></span></div>
        </section>
        <section class="card" aria-labelledby="tools-h">
          <h2 id="tools-h">Troubleshooting</h2>
          <div style="display:flex;gap:10px;flex-wrap:wrap">
            <button class="btn" data-act="runCheck">Run a check</button>
            <button class="btn" data-act="openLog">Open log</button>
            <button class="btn" data-act="openFolder">Open settings folder</button>
          </div>
          <div class="field" style="padding-top:12px;border-top:1px solid var(--line)">
            <span class="muted">Shortcuts in VR</span>
            <span style="display:flex;gap:8px;flex-wrap:wrap" class="mono"><span class="btn ghost" style="padding:6px 10px;font-size:12px">Adjust · Ctrl+Alt+H</span><span class="btn ghost" style="padding:6px 10px;font-size:12px">Hide · Ctrl+Alt+V</span></span>
          </div>
        </section>
      </div>`;
  }

  // ------------------------------------------------------- bound values ----
  // Text that changes without rebuilding the page.
  const V = {
    homeTitle: () => {
      const st = S.state;
      if (!st.layerActive) return 'Not active';
      if (S.live.running) return 'Running in ' + (friendly(S.live.game) || 'a game');
      if (!/Virtual Desktop/.test(st.activeRuntime)) return 'Set Virtual Desktop as the runtime';
      return 'Ready to go into VR';
    },
    layerDot: () => ['dot', S.state.layerActive ? 'good' : 'bad'],
    layerText: () => (S.state.layerActive ? 'Active' : 'Not registered'),
    rtDot: () => ['dot', /Virtual Desktop/.test(S.state.activeRuntime) ? 'good' : 'warn'],
    rtText: () => (S.live.running && S.live.runtime ? friendly(S.live.runtime) : S.state.activeRuntime),
    rtSub: () => (S.live.running ? 'In use now' : 'Active OpenXR runtime'),
    baseDot: () => ['dot', S.live.running ? (S.live.wheelFound ? 'good' : 'warn') : ''],
    baseText: () => (S.live.running ? (S.live.wheelFound ? S.live.wheelBase || 'Found' : 'Not found') : '—'),
    baseSub: () => (S.live.running ? (S.live.wheelFound ? 'Steering ' + deg(S.live.steering) : 'Check the base is on') : 'Shown while a game is running'),
    marginV: () => Math.round(cfg().margin) + ' mm',
    scaleV: () => (wheel() ? wheel().scale : 100) + '%',
    rangeHalf: () => 'of ±' + Math.round(cfg().range / 2) + '°',
    linkState: () => (S.live.running ? 'Connected to ' + (friendly(S.live.game) || 'the game') : 'Start a game in VR to place windows'),
  };
  // Inputs that mirror a setting.
  const I = {
    margin: () => cfg().margin,
    wheelScale: () => (wheel() ? wheel().scale : 100),
    range: () => cfg().range,
    distanceCm: () => cfg().distanceCm,
    nudgeMm: () => cfg().nudgeMm,
    nudgeDeg: () => cfg().nudgeDeg,
    enabled: () => cfg().enabled,
    invert: () => cfg().invert,
  };

  function patch() {
    if (!S.state) return;
    main.querySelectorAll('[data-v]').forEach((el) => {
      const f = V[el.dataset.v];
      if (!f) return;
      try {
        const v = f();
        if (Array.isArray(v)) el.className = v.filter(Boolean).join(' ');
        else el.textContent = v;
      } catch (e) { /* value not available on this page */ }
    });
    if (!cfg()) return;
    main.querySelectorAll('[data-set]').forEach((el) => {
      if (el === document.activeElement || el.dataset.dragging) return;
      const f = I[el.dataset.set];
      if (!f) return;
      if (el.type === 'checkbox') el.checked = !!f();
      else el.value = f();
    });
  }

  function patchLive() {
    const L = S.live;
    document.querySelectorAll('[data-live="steer"]').forEach((el) => { el.textContent = L.running && L.wheelFound ? deg(L.steering) : '—'; });
    document.querySelectorAll('[data-live="base"]').forEach((el) => { el.textContent = L.running ? (L.wheelBase || 'Not found') : 'Shown while a game is running'; });
    document.querySelectorAll('[data-live="baseState"]').forEach((el) => {
      el.innerHTML = L.running ? (L.wheelFound ? '<span class="good-t">Connected</span>' : '<span class="warn-t">Not found</span>') : '';
    });
    const a = L.running && L.wheelFound ? L.steering : 0;
    const w = wheel();
    const rot = document.getElementById('rimrot');
    if (rot) rot.setAttribute('transform', `rotate(${w && w.rotates ? a : 0})`);
    const dial = document.getElementById('dial');
    if (dial) dial.setAttribute('transform', `rotate(${a})`);
    if (S.state) patch();
  }

  // --------------------------------------------------------------- render ----
  function structureKey() {
    const c = cfg();
    return JSON.stringify([S.page, S.tab, S.fine, S.state && S.state.layerActive, S.state && S.state.activeRuntime,
      c && [c.activeWheel, c.wheelPlaced, c.invert, c.enabled, c.keyColour, Math.round(c.margin),
        c.wheels.map((w) => [w.id, w.scale]), c.windows.map((w) => [w.name, w.width, w.height, w.enabled, w.onlyWhileAdjusting])],
      S.state && (S.state.games || []).map((g) => g.exe + g.lastUsed)]);
  }

  function refresh(force) {
    renderVersion();
    if (!S.state) return;
    const key = structureKey();
    const typing = document.activeElement && main.contains(document.activeElement) &&
      (document.activeElement.tagName === 'INPUT' && document.activeElement.type !== 'checkbox' && document.activeElement.type !== 'range');
    if (force || (key !== S.key && !typing)) {
      S.key = key;
      const pages = { home: pageHome, windows: pageWindows, wheel: pageWheel, passthrough: pagePassthrough };
      const scroll = main.scrollTop;
      main.innerHTML = pages[S.page]();
      main.scrollTop = scroll;
    }
    patch();
    patchLive();
    document.querySelectorAll('#nav button').forEach((b) => b.classList.toggle('on', b.dataset.page === S.page));
  }

  function renderVersion() {
    const el = document.getElementById('version');
    if (!S.state) { el.innerHTML = ''; return; }
    const u = S.state.update || {};
    let msg = '', btn = '';
    switch (u.state) {
      case 'checking': msg = '<div class="msg">Checking for updates…</div>'; break;
      case 'available': msg = `<div class="msg" style="color:var(--text)">${esc(u.latest)} is available</div>`; btn = '<button class="btn primary" style="padding:8px" data-act="install">Install update</button>'; break;
      case 'downloading': msg = `<div class="msg">Downloading ${esc(u.latest)}…</div><div class="bar"><span style="width:${u.progress || 0}%"></span></div>`; break;
      case 'installing': msg = '<div class="msg">Installing — the app will reopen</div>'; break;
      case 'uptodate': msg = '<div class="msg good"><span class="dot good"></span>Up to date</div>'; btn = '<button class="linkbtn" style="align-self:flex-start" data-act="check">Check again</button>'; break;
      case 'error': msg = `<div class="msg warn">${esc(u.message)}</div>`; btn = '<button class="linkbtn" style="align-self:flex-start" data-act="check">Try again</button>'; break;
      default: btn = '<button class="linkbtn" style="align-self:flex-start" data-act="check">Check for updates</button>';
    }
    const html = `<div class="row"><span class="label">Version</span><span class="num">${esc(S.state.version)}</span></div>${msg}${btn}`;
    if (el.dataset.html !== html) { el.innerHTML = html; el.dataset.html = html; }
    el.classList.toggle('has-update', u.state === 'available');
  }

  // --------------------------------------------------------------- events ----
  document.getElementById('nav').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-page]');
    if (!b) return;
    S.page = b.dataset.page;
    refresh(true);
    main.focus();
  });

  document.getElementById('version').addEventListener('click', async (e) => {
    const b = e.target.closest('[data-act]');
    if (!b) return;
    if (b.dataset.act === 'check') send('checkUpdates');
    if (b.dataset.act === 'install') send('installUpdate');
  });

  main.addEventListener('click', async (e) => {
    const b = e.target.closest('[data-act]');
    if (!b || b.disabled) return;
    const act = b.dataset.act;
    switch (act) {
      case 'go': S.page = b.dataset.page; refresh(true); break;
      case 'tab':
        S.tab = +b.dataset.i;
        refresh(true);
        if (S.live.running) send('action', { action: A.Select, target: S.tab }); // outline it in the headset
        break;
      case 'fine': S.fine = b.dataset.f === '1'; refresh(true); break;
      case 'do': {
        const r = await send('action', { action: +b.dataset.a, target: S.page === 'windows' ? S.tab : 0, fine: S.fine });
        if (r.ok && +b.dataset.a === A.Place) toast('Placed — check it in the headset');
        break;
      }
      case 'centre':
        if (S.live.running) {
          const r = await send('action', { action: A.Centre, target: 0 });
          if (r.ok) toast('Centre set');
        } else {
          toast('Start a game in VR, hold the wheel straight, then press this.', true);
        }
        break;
      case 'rim': send('set', { key: 'activeWheel', value: b.dataset.id }); break;
      case 'addWindow': {
        const c = cfg();
        if (!c) return;
        await send('addWindow', { name: 'Window ' + (c.windows.length + 1), width: 200, height: 120 });
        S.page = 'windows';
        S.tab = c.windows.length + 1;
        break;
      }
      case 'addKeyboard': {
        const c = cfg();
        if (!c) return;
        await send('addWindow', { name: 'Keyboard', width: 300, height: 120, onlyWhileAdjusting: true });
        S.page = 'windows';
        S.tab = c.windows.length + 1;
        toast('Keyboard window added. With a game running, look at your keyboard and press Place.');
        break;
      }
      case 'removeWindow': {
        const c = cfg();
        const name = c.windows[S.tab - 1] && c.windows[S.tab - 1].name;
        if (!confirm(`Remove the “${name}” window?`)) return;
        await send('removeWindow', { index: S.tab - 1 });
        S.tab = 0;
        break;
      }
      case 'colour': send('pickColour'); break;
      case 'runCheck': send('runCheck'); break;
      case 'openLog': send('openLog'); break;
      case 'openFolder': send('openFolder'); break;
    }
  });

  // Settings: switches and sliders save on change; number boxes on Enter or leaving the box.
  main.addEventListener('change', (e) => {
    const el = e.target;
    if (el.dataset.set) {
      const value = el.type === 'checkbox' ? el.checked : parseFloat(el.value);
      if (el.type !== 'checkbox' && isNaN(value)) return;
      send('set', { key: el.dataset.set, value });
    } else if (el.dataset.win) {
      const field = el.dataset.win;
      const value = el.type === 'checkbox' ? el.checked : field === 'name' ? el.value : parseFloat(el.value);
      if (field !== 'name' && el.type !== 'checkbox' && isNaN(value)) return;
      send('setWindow', { index: S.tab - 1, [field]: value });
    }
  });

  // Show slider values while dragging, before they are saved.
  main.addEventListener('input', (e) => {
    const el = e.target;
    if (el.type !== 'range') return;
    el.dataset.dragging = '1';
    const label = el.parentElement.querySelector('.t span:last-child');
    if (label) label.textContent = el.value + (el.dataset.fmt === 'pct' ? '%' : ' mm');
  });
  main.addEventListener('pointerup', (e) => { if (e.target.type === 'range') delete e.target.dataset.dragging; });
  main.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && e.target.tagName === 'INPUT') e.target.blur();
  });

  send('refresh');
})();
