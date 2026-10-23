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
    } else if (m.type === 'window') {
      document.getElementById('maxbtn').setAttribute('aria-label', m.maximised ? 'Restore' : 'Maximise');
      document.getElementById('maxbtn').innerHTML = m.maximised
        ? '<svg width="12" height="12" viewBox="0 0 12 12" aria-hidden="true"><rect x="2.5" y="4" width="5.5" height="5.5" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M4.5 4V2.5h5v5H8" fill="none" stroke="currentColor" stroke-width="1.2"/></svg>'
        : '<svg width="12" height="12" viewBox="0 0 12 12" aria-hidden="true"><rect x="2.5" y="2.5" width="7" height="7" fill="none" stroke="currentColor" stroke-width="1.2"/></svg>';
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
          In VR: <span class="mono" style="color:var(--text)">Ctrl+Alt+H</span> adjust windows · <span class="mono" style="color:var(--text)">Ctrl+Alt+V</span> hide them · <span class="mono" style="color:var(--text)">Ctrl+Alt+K</span> help board</p>
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
           ${windowShapeSvg(win)}</svg>
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
        <div class="field"><label for="wshape">Shape</label>
          <select id="wshape" class="text-in" style="max-width:190px" data-win="shape">
            <option value="rect" ${win.shape === 'rect' ? 'selected' : ''}>Rectangle</option>
            <option value="rounded" ${win.shape === 'rounded' ? 'selected' : ''}>Rounded</option>
            <option value="ellipse" ${win.shape === 'ellipse' ? 'selected' : ''}>Circle / oval</option>
            ${win.shape === 'custom' ? '<option value="custom" selected>Traced outline</option>' : ''}
          </select></div>
        <button class="btn" data-act="traceWindow">Trace its outline from a photo…</button>
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

  function windowShapeSvg(win) {
    const style = `fill="var(--accent)" fill-opacity="0.55" stroke="#ECEEF2" stroke-width="${Math.max(win.width, win.height) / 300}"`;
    if (win.shape === 'ellipse') return `<ellipse rx="${win.width / 2}" ry="${win.height / 2}" ${style}/>`;
    if (win.shape === 'rounded') return `<rect x="${-win.width / 2}" y="${-win.height / 2}" width="${win.width}" height="${win.height}" rx="${Math.min(win.width, win.height) * 0.175}" ${style}/>`;
    if (win.shape === 'custom' && win.shapePath) {
      const nums = win.shapePath.match(/-?\d+(\.\d+)?/g).map(Number);
      const xs = nums.filter((_, i) => i % 2 === 0), ys = nums.filter((_, i) => i % 2 === 1);
      const w = Math.max(...xs) - Math.min(...xs) || 1, h = Math.max(...ys) - Math.min(...ys) || 1;
      const mx = (Math.max(...xs) + Math.min(...xs)) / 2, my = (Math.max(...ys) + Math.min(...ys)) / 2;
      return `<g transform="scale(${win.width / w} ${win.height / h}) translate(${-mx} ${-my})"><path d="${win.shapePath}" ${style} vector-effect="non-scaling-stroke"/></g>`;
    }
    return `<rect x="${-win.width / 2}" y="${-win.height / 2}" width="${win.width}" height="${win.height}" rx="2" ${style}/>`;
  }

  async function traceWindow() {
    const index = S.tab - 1;
    const r = await send('traceWindow', { index });
    if (!r.ok || !r.data) return;
    const d = r.data;
    const modal = document.createElement('div');
    modal.className = 'modal';
    modal.innerHTML = `
      <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="tw-t">
        <h2 id="tw-t">Window outline</h2>
        <p class="muted" style="margin:0">Check the outline follows the object. A front-on photo works best.</p>
        <div class="photo"><svg viewBox="0 0 ${d.width} ${d.height}" role="img" aria-label="Photo with traced outline">
          <image href="${d.image}" width="${d.width}" height="${d.height}"/>
          <polygon points="${d.outline}" fill="rgba(183,148,246,.25)" stroke="#E25CE2" stroke-width="${Math.max(2, d.width / 300)}"/></svg></div>
        <div class="field"><label class="lbl" for="tww"><span>Real width</span><span class="muted">Across the widest point; height follows the photo</span></label>
          <span style="display:flex;align-items:center;gap:6px"><input id="tww" class="num-in" type="number" min="20" max="800" value="${Math.round(cfg().windows[index].width)}"><span class="muted">mm</span></span></div>
        <div style="display:flex;justify-content:flex-end;gap:10px">
          <button class="btn ghost" data-m="cancel">Cancel</button>
          <button class="btn primary" data-m="save">Use this outline</button>
        </div>
      </div>`;
    document.body.appendChild(modal);
    modal.addEventListener('click', async (ev) => {
      const m = ev.target.closest('[data-m]');
      if (ev.target === modal || (m && m.dataset.m === 'cancel')) { modal.remove(); return; }
      if (m && m.dataset.m === 'save') {
        const res = await send('saveWindowShape', { widthMm: parseFloat(modal.querySelector('#tww').value) || 150 });
        if (res.ok) { modal.remove(); toast('Outline saved.'); }
      }
    });
    modal.addEventListener('keydown', (ev) => { if (ev.key === 'Escape') modal.remove(); });
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
            ${w.userAdded ? `<span class="linkbtn" role="button" tabindex="0" data-act="removeRim" data-id="${esc(w.id)}" style="align-self:flex-start;color:var(--text-3)">Remove</span>` : ''}
          </button>`).join('')}
          <button class="rim" data-act="addRim" style="border-style:dashed;align-items:center;justify-content:center;text-align:center">
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="var(--text-2)" stroke-width="1.6" aria-hidden="true"><path d="M12 5v14M5 12h14"/></svg>
            <span style="color:var(--text-2)">Add a rim</span><span class="muted">From a front-on photo — a cut-out or plain background works best</span>
          </button>
        </div>
      </section>
      <div class="grid2">
        <section class="card" aria-labelledby="steer-h">
          <h2 id="steer-h">Steering</h2>
          <div class="field"><div class="lbl"><span class="muted">Wheel base</span><span style="font-weight:500" data-live="base">—</span></div><span class="muted" data-live="baseState"></span></div>
          <div class="field"><label for="basepick">Use</label>
            <select id="basepick" class="text-in" style="max-width:260px" data-act-change="base">${baseOptions(c.deviceMatch)}</select></div>
          <div class="field"><span class="muted" style="max-width:240px">Not sure which it is? Click Find, then turn the wheel lock to lock.</span>
            <button class="btn" data-act="findBase">Find it — turn your wheel</button></div>
          <div class="field"><label class="lbl" for="range"><span>Steering range</span><span class="muted">Match your wheel software (e.g. Simucube Tuner, Fanatec App, Pit House)</span></label>
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
            <div style="display:flex;flex-direction:column;gap:4px"><span class="mono" style="font-size:34px;letter-spacing:-1px" data-live="steer">—</span><span class="muted" data-v="rangeHalf"></span><span class="muted" data-live="steerHint"></span></div>
          </div>
          <button class="btn" data-act="centre">Set centre — hold the wheel straight</button>
        </section>
      </div>`;
  }

  // Wheel base choices: automatic, then every controller found, marking known wheel bases.
  function baseOptions(current) {
    const list = S.bases || [];
    const auto = !current || current.toLowerCase() === 'auto';
    let html = `<option value="auto" ${auto ? 'selected' : ''}>Automatic — any known wheel base</option>`;
    let found = auto;
    for (const b of list) {
      const sel = !auto && b.name.toLowerCase().includes(current.toLowerCase());
      if (sel) found = true;
      html += `<option value="${esc(b.name)}" ${sel ? 'selected' : ''}>${esc(b.name)}${b.known ? '' : ' (not a known wheel base)'}</option>`;
    }
    if (!found) html += `<option value="${esc(current)}" selected>${esc(current)} (not connected)</option>`;
    return html;
  }

  async function loadBases() {
    const r = await send('listBases');
    if (r.ok && r.data) { S.bases = r.data; S.key = ''; refresh(); }
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
            <button class="btn primary" data-act="report">Send a report</button>
            <button class="btn" data-act="runCheck">Run a check</button>
            <button class="btn" data-act="openLog">Open log</button>
            <button class="btn" data-act="openFolder">Open settings folder</button>
          </div>
          <div class="field" style="padding-top:12px;border-top:1px solid var(--line)">
            <span class="muted">Shortcuts in VR</span>
            <span style="display:flex;gap:8px;flex-wrap:wrap" class="mono"><span class="btn ghost" style="padding:6px 10px;font-size:12px">Adjust · Ctrl+Alt+H</span><span class="btn ghost" style="padding:6px 10px;font-size:12px">Hide · Ctrl+Alt+V</span><span class="btn ghost" style="padding:6px 10px;font-size:12px">Help board · Ctrl+Alt+K</span></span>
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
    baseDot: () => ['dot', S.live.wheelFound ? 'good' : 'warn'],
    baseText: () => (S.live.wheelFound ? S.live.wheelBase || 'Found' : 'Not found'),
    baseSub: () => (!S.live.wheelFound ? 'Check it is switched on, or choose it on the Wheel page'
      : S.live.hasAngle === false ? 'Connected — turn the wheel to see the angle' : 'Steering ' + deg(S.live.steering)),
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
    const hasAngle = L.wheelFound && L.hasAngle !== false;
    document.querySelectorAll('[data-live="steer"]').forEach((el) => { el.textContent = hasAngle ? deg(L.steering) : '—'; });
    document.querySelectorAll('[data-live="steerHint"]').forEach((el) => {
      el.textContent = !L.wheelFound ? 'No headset or game needed — the app reads the base directly'
        : !hasAngle ? 'Turn the wheel a little — some bases only report when they move'
        : L.running ? 'Read by the game' : 'Read directly from the base';
    });
    document.querySelectorAll('[data-live="base"]').forEach((el) => { el.textContent = L.wheelFound ? (L.wheelBase || 'Connected') : 'Not found'; });
    document.querySelectorAll('[data-live="baseState"]').forEach((el) => {
      el.innerHTML = L.wheelFound ? '<span class="good-t">Connected</span>' : '<span class="warn-t">Not found</span>';
    });
    const a = L.wheelFound ? L.steering : 0;
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
      S.bases && S.bases.map((b) => b.name),
      c && [c.deviceMatch, c.activeWheel, c.wheelPlaced, c.invert, c.enabled, c.keyColour, Math.round(c.margin),
        c.wheels.map((w) => [w.id, w.scale]), c.windows.map((w) => [w.name, w.width, w.height, w.enabled, w.onlyWhileAdjusting, w.shape, w.shapePath])],
      S.state && (S.state.games || []).map((g) => g.exe + g.lastUsed)]);
  }

  function refresh(force) {
    renderVersion();
    if (!S.state) return;
    const key = structureKey();
    const typing = document.activeElement && main.contains(document.activeElement) &&
      ((document.activeElement.tagName === 'INPUT' && document.activeElement.type !== 'checkbox' && document.activeElement.type !== 'range') || document.activeElement.tagName === 'SELECT');
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
    if (S.page === 'wheel') loadBases();
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
      case 'addRim': addRim(); break;
      case 'traceWindow': traceWindow(); break;
      case 'findBase': {
        b.disabled = true;
        const label = b.textContent;
        b.textContent = 'Turn the wheel now…';
        const r = await send('findBase');
        b.disabled = false;
        b.textContent = label;
        if (r.ok && r.data) { toast('Using ' + r.data.name); loadBases(); }
        break;
      }
      case 'removeRim': {
        e.stopPropagation();
        const w = cfg().wheels.find((x) => x.id === b.dataset.id);
        if (w && confirm(`Remove the “${w.name}” rim?`)) send('removeRim', { id: w.id });
        break;
      }
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
      case 'report': {
        const r = await send('makeReport');
        if (r.ok && r.data) toast('Saved “' + r.data.name + '” to your desktop. Send that file to whoever is helping you.');
        break;
      }
      case 'openLog': send('openLog'); break;
      case 'openFolder': send('openFolder'); break;
    }
  });

  // ------------------------------------------------------------- add a rim ----
  // Standard rim types, drawn around the hub (mm, y up). Close enough to drive with; the hand margin
  // covers the difference. For an exact outline, trace a photo instead.
  const RIM_TYPES = {
    round: { label: 'Round', ratio: 1, rotates: false },
    flat: { label: 'Flat-bottom (D-shape)', ratio: 0.9, rotates: true },
    gt: { label: 'GT', ratio: 0.56, rotates: true },
    formula: { label: 'Formula', ratio: 0.5, rotates: true },
  };
  // Named rims with their published sizes.
  const RIM_PRESETS = [
    { name: 'Fanatec ClubSport Steering Wheel RS', type: 'round', width: 320 },
    { name: 'Fanatec ClubSport Formula V2.5', type: 'formula', width: 270 },
    { name: 'Fanatec ClubSport Formula V3', type: 'formula', width: 290 },
    { name: 'Conspit 310 APEX', type: 'round', width: 310 },
  ];

  function rimOutline(type, width) {
    const a = width / 2, pts = [];
    if (type === 'round' || type === 'flat') {
      const cut = type === 'flat' ? -0.8 * a : -a - 1; // flat bottom: a chord across the lower part
      for (let i = 0; i < 96; i++) {
        const t = (i / 96) * Math.PI * 2;
        let x = a * Math.cos(t), y = a * Math.sin(t);
        if (y < cut) y = cut; // points below the cut lie along the flat edge
        pts.push([x, y]);
      }
    } else {
      // A rounded-rectangle-like curve (superellipse): squarer for formula rims.
      const b = a * RIM_TYPES[type].ratio, n = type === 'formula' ? 4.5 : 3;
      for (let i = 0; i < 96; i++) {
        const t = (i / 96) * Math.PI * 2, c = Math.cos(t), s2 = Math.sin(t);
        pts.push([a * Math.sign(c) * Math.pow(Math.abs(c), 2 / n), b * Math.sign(s2) * Math.pow(Math.abs(s2), 2 / n)]);
      }
    }
    return pts.map(([x, y]) => [Math.round(x * 10) / 10, Math.round(y * 10) / 10]);
  }

  function addRim() {
    if (!cfg()) return;
    const modal = document.createElement('div');
    modal.className = 'modal';
    modal.innerHTML = `
      <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="ar-t">
        <h2 id="ar-t">Add a rim</h2>
        <div class="grid2">
          <button class="rim" data-m="photo" style="align-items:flex-start">
            <span style="font-weight:500">From a photo</span>
            <span class="muted">Traces the exact outline. A front-on cut-out or plain background works best.</span></button>
          <button class="rim" data-m="standard" style="align-items:flex-start">
            <span style="font-weight:500">Standard shape</span>
            <span class="muted">Pick a known rim or a type (round, D-shape, GT, formula) and its width.</span></button>
        </div>
        <div style="display:flex;justify-content:flex-end"><button class="btn ghost" data-m="cancel">Cancel</button></div>
      </div>`;
    document.body.appendChild(modal);
    modal.addEventListener('click', (ev) => {
      const m = ev.target.closest('[data-m]');
      if (ev.target === modal || (m && m.dataset.m === 'cancel')) { modal.remove(); return; }
      if (!m) return;
      modal.remove();
      if (m.dataset.m === 'photo') addRimFromPhoto();
      if (m.dataset.m === 'standard') addRimStandard();
    });
  }

  function addRimStandard() {
    const modal = document.createElement('div');
    modal.className = 'modal';
    const opts = RIM_PRESETS.map((p, i) => `<option value="p${i}">${esc(p.name)} — ${p.width} mm</option>`).join('') +
      '<option disabled>──────────</option>' +
      Object.entries(RIM_TYPES).map(([k, t]) => `<option value="t-${k}">Other rim — ${t.label}</option>`).join('');
    modal.innerHTML = `
      <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="sr-t">
        <h2 id="sr-t">Standard rim</h2>
        <div class="field"><label for="srpick">Rim</label><select id="srpick" class="text-in" style="max-width:360px">${opts}</select></div>
        <div class="photo"><svg id="srprev" viewBox="-200 -200 400 400" style="max-height:220px" role="img" aria-label="Rim shape preview"></svg></div>
        <div class="field"><label for="srname">Name</label><input id="srname" class="text-in" style="max-width:300px"></div>
        <div class="field"><label class="lbl" for="srw"><span>Width</span><span class="muted">Across the widest point</span></label>
          <span style="display:flex;align-items:center;gap:6px"><input id="srw" class="num-in" type="number" min="150" max="450"><span class="muted">mm</span></span></div>
        <p class="muted" style="margin:0">A close match, not an exact outline — the space for hands covers the difference. Use a photo for an exact fit.</p>
        <div style="display:flex;justify-content:flex-end;gap:10px">
          <button class="btn ghost" data-m="cancel">Cancel</button>
          <button class="btn primary" data-m="save">Add rim</button>
        </div>
      </div>`;
    document.body.appendChild(modal);
    const $ = (sel) => modal.querySelector(sel);
    let type = 'round';
    const draw = () => {
      const w = parseFloat($('#srw').value) || 300;
      const pts = rimOutline(type, w);
      $('#srprev').setAttribute('viewBox', `${-w / 2 - 20} ${-w / 2 - 20} ${w + 40} ${w + 40}`);
      $('#srprev').innerHTML = `<polygon points="${pts.map(([x, y]) => x + ',' + -y).join(' ')}" fill="rgba(183,148,246,.25)" stroke="#B794F6" stroke-width="3"/>`;
    };
    const pick = () => {
      const v = $('#srpick').value;
      if (v.startsWith('p')) {
        const p = RIM_PRESETS[+v.slice(1)];
        type = p.type; $('#srname').value = p.name; $('#srw').value = p.width;
      } else {
        type = v.slice(2); $('#srname').value = RIM_TYPES[type].label + ' rim'; $('#srw').value = 300;
      }
      draw();
    };
    pick();
    $('#srpick').addEventListener('change', pick);
    $('#srw').addEventListener('input', draw);
    modal.addEventListener('click', async (ev) => {
      const m = ev.target.closest('[data-m]');
      if (ev.target === modal || (m && m.dataset.m === 'cancel')) { modal.remove(); return; }
      if (m && m.dataset.m === 'save') {
        const w = parseFloat($('#srw').value) || 300;
        const res = await send('saveRimOutline', { name: $('#srname').value, outline: rimOutline(type, w), rotates: RIM_TYPES[type].rotates });
        if (res.ok) { modal.remove(); toast('Rim added and selected. Fine-tune it from the Windows page.'); }
      }
    });
    modal.addEventListener('keydown', (ev) => { if (ev.key === 'Escape') modal.remove(); });
  }

  async function addRimFromPhoto() {
    const r = await send('traceRim');
    if (!r.ok || !r.data) return; // cancelled, or an error already shown
    const d = r.data;
    const modal = document.createElement('div');
    modal.className = 'modal';
    modal.innerHTML = `
      <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="rim-t">
        <h2 id="rim-t">New rim</h2>
        <p class="muted" style="margin:0">Check the outline follows the rim. If it doesn't, try a photo with a plainer background.</p>
        <div class="photo"><svg viewBox="0 0 ${d.width} ${d.height}" aria-label="Photo with traced outline" role="img">
          <image href="${d.image}" width="${d.width}" height="${d.height}"/>
          <polygon points="${d.outline}" fill="rgba(183,148,246,.25)" stroke="#E25CE2" stroke-width="${Math.max(2, d.width / 300)}"/>
          <line id="hubline" x1="0" x2="${d.width}" stroke="#ECEEF2" stroke-dasharray="6 6" stroke-width="${Math.max(1, d.width / 600)}"/>
        </svg></div>
        <div class="field"><label for="rname">Name</label><input id="rname" class="text-in" style="max-width:260px" value="${esc(d.name)}"></div>
        <div class="field"><label class="lbl" for="rwidth"><span>Width</span><span class="muted">Across the widest point, from the maker's specs</span></label>
          <span style="display:flex;align-items:center;gap:6px"><input id="rwidth" class="num-in" type="number" min="150" max="450" value="300"><span class="muted">mm</span></span></div>
        <div class="field"><label class="lbl" for="rhub"><span>Hub height</span><span class="muted">Dashed line: where the wheel turns. Move it to the centre of the quick release</span></label>
          <span style="display:flex;align-items:center;gap:6px"><input id="rhub" class="num-in" type="number" min="-80" max="80" step="1" value="0"><span class="muted">mm up</span></span></div>
        <div class="field"><span>Turns with the wheel</span>${sw('rrot', Math.abs(d.boundsW - d.boundsH) / d.boundsW > 0.08, '')}</div>
        <div style="display:flex;justify-content:flex-end;gap:10px">
          <button class="btn ghost" data-m="cancel">Cancel</button>
          <button class="btn primary" data-m="save">Add rim</button>
        </div>
      </div>`;
    document.body.appendChild(modal);
    const $ = (sel) => modal.querySelector(sel);
    // The turning point starts at the middle of the outline; "Hub height" moves it up or down.
    const ys = d.outline.split(' ').map((p) => +p.split(',')[1]);
    const mid = (Math.min(...ys) + Math.max(...ys)) / 2;
    const hubLine = () => {
      const pxPerMm = d.boundsW / (parseFloat($('#rwidth').value) || 300);
      const y = mid - (parseFloat($('#rhub').value) || 0) * pxPerMm;
      $('#hubline').setAttribute('y1', y);
      $('#hubline').setAttribute('y2', y);
    };
    hubLine();
    $('#rhub').addEventListener('input', hubLine);
    $('#rwidth').addEventListener('input', hubLine);
    $('#rname').focus();
    modal.addEventListener('click', async (ev) => {
      const m = ev.target.closest('[data-m]');
      if (ev.target === modal || (m && m.dataset.m === 'cancel')) { modal.remove(); return; }
      if (m && m.dataset.m === 'save') {
        const res = await send('saveRim', {
          name: $('#rname').value, widthMm: parseFloat($('#rwidth').value),
          hubUpMm: parseFloat($('#rhub').value) || 0, rotates: $('#rrot').checked,
        });
        if (res.ok) { modal.remove(); toast('Rim added and selected. Place it from the Windows page.'); }
      }
    });
    modal.addEventListener('keydown', (ev) => { if (ev.key === 'Escape') modal.remove(); });
  }

  // Settings: switches and sliders save on change; number boxes on Enter or leaving the box.
  main.addEventListener('change', (e) => {
    const el = e.target;
    if (el.dataset.actChange === 'base') { send('set', { key: 'deviceMatch', value: el.value }); return; }
    if (el.dataset.set) {
      const value = el.type === 'checkbox' ? el.checked : parseFloat(el.value);
      if (el.type !== 'checkbox' && isNaN(value)) return;
      send('set', { key: el.dataset.set, value });
    } else if (el.dataset.win) {
      const field = el.dataset.win;
      const text = field === 'name' || field === 'shape';
      const value = el.type === 'checkbox' ? el.checked : text ? el.value : parseFloat(el.value);
      if (!text && el.type !== 'checkbox' && isNaN(value)) return;
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

  // ---- title bar: drag to move, double-click to maximise, window buttons ----
  const bar = document.getElementById('titlebar');
  bar.addEventListener('mousedown', (e) => {
    if (e.button !== 0 || e.target.closest('button')) return;
    window.chrome.webview.postMessage({ id: 0, cmd: 'winDrag' });
  });
  bar.addEventListener('dblclick', (e) => { if (!e.target.closest('button')) send('winMax'); });
  bar.querySelectorAll('[data-win]').forEach((b) => b.addEventListener('click', () => {
    const w = b.dataset.win;
    if (w === 'min') send('winMin');
    if (w === 'max') send('winMax');
    if (w === 'close') send('quit');
  }));

  // ---- sidebar: report and quit ----
  document.querySelector('.side-foot').addEventListener('click', async (e) => {
    const b = e.target.closest('[data-act-side]');
    if (!b) return;
    if (b.dataset.actSide === 'quit') send('quit');
    if (b.dataset.actSide === 'report') {
      const r = await send('makeReport');
      if (r.ok && r.data) toast('Saved “' + r.data.name + '” to your desktop. Send that file to whoever is helping you.');
    }
  });

  // Esc closes the dialog on top, wherever the focus is.
  document.addEventListener('keydown', (e) => {
    if (e.key !== 'Escape') return;
    const open = document.querySelectorAll('.modal');
    if (open.length) open[open.length - 1].remove();
  });

  send('refresh');
})();
