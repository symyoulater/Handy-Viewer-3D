// Thin Place VR - the built-in guide (the Guide page). Plain HTML, shown by app.js.
// Screens in guide/*.png are real screenshots of this app with sample settings.
window.TPV_GUIDE = (() => {
  const key = (k) => `<kbd>${k}</kbd>`;

  // The idea in one picture: the game, with holes where you see your real kit.
  const headsetView = `
  <svg viewBox="0 0 640 320" role="img" aria-label="Drawing of the view in the headset: the game, with cut-outs showing the real wheel, Stream Deck and a Discord panel" class="g-fig">
    <defs>
      <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2B3A55"/><stop offset="1" stop-color="#151A24"/></linearGradient>
      <pattern id="real" width="10" height="10" patternUnits="userSpaceOnUse" patternTransform="rotate(35)"><rect width="10" height="10" fill="#C8B79E"/><line x1="0" y1="0" x2="0" y2="10" stroke="#B3A188" stroke-width="4"/></pattern>
    </defs>
    <rect width="640" height="320" rx="14" fill="url(#sky)"/>
    <path d="M0 175 L250 120 L390 120 L640 175 L640 320 L0 320 Z" fill="#2D2F33"/>
    <path d="M300 120 L340 120 L420 320 L220 320 Z" fill="#3A3D42"/>
    <path d="M0 210 Q320 150 640 210 L640 320 L0 320 Z" fill="#0F1013"/>
    <g transform="translate(320 240)">
      <path d="M-95 -28 Q-110 -55 -70 -60 L70 -60 Q110 -55 95 -28 L88 20 Q60 46 30 30 L-30 30 Q-60 46 -88 20 Z" fill="url(#real)" stroke="#ECEEF2" stroke-width="2" stroke-dasharray="6 5"/>
      <circle r="18" fill="#3B3328"/>
      <text y="68" text-anchor="middle" fill="#ECEEF2" font-size="13" font-family="Inter, Segoe UI, sans-serif">your real wheel and hands</text>
    </g>
    <g transform="translate(470 215)">
      <rect x="-42" y="-28" width="84" height="56" rx="8" fill="url(#real)" stroke="#ECEEF2" stroke-width="2" stroke-dasharray="6 5"/>
      <g fill="#2A2522">${[0, 1, 2].map((r) => [0, 1, 2, 3, 4].map((c) => `<rect x="${-34 + c * 14}" y="${-20 + r * 14}" width="11" height="11" rx="2"/>`).join('')).join('')}</g>
      <text y="46" text-anchor="middle" fill="#ECEEF2" font-size="13" font-family="Inter, Segoe UI, sans-serif">Stream Deck</text>
    </g>
    <g transform="translate(120 70)">
      <rect x="-80" y="-48" width="160" height="104" rx="6" fill="#36393F" stroke="#B794F6" stroke-width="2"/>
      <rect x="-80" y="-48" width="38" height="104" rx="6" fill="#2B2D31"/>
      ${[0, 1, 2, 3].map((i) => `<rect x="-34" y="${-38 + i * 22}" width="${90 - i * 12}" height="8" rx="4" fill="#5C6068"/>`).join('')}
      <text y="74" text-anchor="middle" fill="#ECEEF2" font-size="13" font-family="Inter, Segoe UI, sans-serif">a panel: Discord, a web page…</text>
    </g>
  </svg>`;

  // How the cut-outs work: magenta shapes in the game, replaced by Virtual Desktop.
  const howItWorks = `
  <svg viewBox="0 0 640 165" role="img" aria-label="Three steps: Thin Place VR draws magenta shapes, Virtual Desktop swaps magenta for the camera view, you see your real wheel" class="g-fig">
    <g font-family="Inter, Segoe UI, sans-serif" font-size="13" fill="#ECEEF2" text-anchor="middle">
      <rect x="10" y="10" width="180" height="100" rx="10" fill="#1E2430"/>
      <path d="M45 75 Q40 50 70 47 L130 47 Q160 50 155 75 L150 95 Q130 105 115 98 L85 98 Q70 105 50 95 Z" fill="#FF00FF"/>
      <text x="100" y="132">1. Thin Place VR draws</text><text x="100" y="150">a magenta shape</text>
      <path d="M200 60 L230 60" stroke="#8E95A1" stroke-width="2"/>
      <rect x="240" y="10" width="180" height="100" rx="10" fill="#1E2430"/>
      <path d="M275 75 Q270 50 300 47 L360 47 Q390 50 385 75 L380 95 Q360 105 345 98 L315 98 Q300 105 280 95 Z" fill="#C8B79E"/>
      <text x="330" y="132">2. Virtual Desktop shows</text><text x="330" y="150">the camera view there</text>
      <path d="M430 60 L460 60" stroke="#8E95A1" stroke-width="2"/>
      <rect x="470" y="10" width="160" height="100" rx="10" fill="#1E2430"/>
      <text x="550" y="55" font-size="15">You see your</text><text x="550" y="75" font-size="15">real kit, in place</text>
      <text x="550" y="132">3. The wheel window</text><text x="550" y="150">turns as you steer</text>
    </g>
  </svg>`;

  // The setup panel you see in the headset (a simplified drawing).
  const setupPanel = `
  <svg viewBox="0 0 340 330" role="img" aria-label="Drawing of the setup panel in the headset" class="g-fig g-narrow">
    <g font-family="Inter, Segoe UI, sans-serif">
      <rect width="340" height="330" rx="14" fill="#15171C" stroke="#30343C"/>
      <text x="16" y="32" fill="#ECEEF2" font-size="17" font-weight="600">Thin Place VR</text>
      <rect x="248" y="16" width="78" height="22" rx="11" fill="#3A2E54"/><text x="287" y="31" fill="#B794F6" font-size="11" font-weight="600" text-anchor="middle">EDITING</text>
      <text x="16" y="62" fill="#8E95A1" font-size="10" font-weight="600">WINDOWS FOR AC EVO · TAB SELECTS</text>
      <rect x="10" y="70" width="320" height="22" fill="#242030"/>
      <text x="16" y="86" fill="#B794F6" font-size="12">▶</text><text x="32" y="86" fill="#ECEEF2" font-size="12" font-weight="600">Wheel</text><text x="120" y="86" fill="#A9AFBA" font-size="12">Conspit 300GT · 100%</text>
      <text x="32" y="110" fill="#ECEEF2" font-size="12">Stream Deck</text><text x="120" y="110" fill="#A9AFBA" font-size="12">130 × 95 mm · rounded</text>
      <text x="32" y="132" fill="#ECEEF2" font-size="12">Discord</text><text x="120" y="132" fill="#A9AFBA" font-size="12">desktop window · Discord</text>
      <text x="32" y="156" fill="#A9AFBA" font-size="11">62 cm away   tilt −38°   turn 0°   roll 0°</text>
      <line x1="16" y1="168" x2="324" y2="168" stroke="#30343C"/>
      <text x="16" y="188" fill="#A9AFBA" font-size="12">Wheel base: Simucube 2 Pro   −32°</text>
      <line x1="16" y1="200" x2="324" y2="200" stroke="#30343C"/>
      ${[['P', 'place where you look'], ['Tab', 'next window'], ['← ↑ → ↓', 'move'], ['R  F', 'away  closer'], ['M', 'face me'], ['=  −', 'bigger  smaller']]
        .map(([k, t], i) => `<text x="${16 + (i % 2) * 180}" y="${222 + Math.floor(i / 2) * 22}" fill="#ECEEF2" font-size="11" font-family="Consolas, monospace" font-weight="600">${k}</text><text x="${74 + (i % 2) * 180}" y="${222 + Math.floor(i / 2) * 22}" fill="#A9AFBA" font-size="11">${t}</text>`).join('')}
      <text x="16" y="306" fill="#8E95A1" font-size="10">Shift = fine steps · Esc = finish · Ctrl+Alt+K = hide</text>
    </g>
  </svg>`;

  const shot = (file, alt, caption) =>
    `<figure class="g-shot"><img src="guide/${file}" alt="${alt}" loading="lazy"><figcaption>${caption}</figcaption></figure>`;

  const sections = [
    ['what', 'What it does', `
      <p>Thin Place VR cuts windows into your VR game so you can see your <b>real wheel, hands, Stream Deck and keyboard</b> while you drive. The wheel window turns with your wheel, so your hands stay visible on the rim. Windows can also show things from your PC, such as Discord, a web page, a PDF, your camera or a live dashboard, as panels floating in the cockpit.</p>
      ${headsetView}
      <p class="g-muted">How the cut-outs work: Thin Place VR draws magenta (bright pink) shapes in the game, and Virtual Desktop replaces anything magenta with the headset's camera view.</p>
      ${howItWorks}`],

    ['start', 'First time: set up Virtual Desktop', `
      <p>Do this once. If Virtual Desktop updates, check these settings again, because updates can reset them.</p>
      <ol class="g-steps">
        <li><b>On the PC</b>, open the <b>Virtual Desktop Streamer</b> app and set the OpenXR runtime to <b>VDXR</b>. You don't need SteamVR.</li>
        <li><b>In the headset</b>, open Virtual Desktop's menu, go to <b>Streaming</b> and set the frame rate to <b>90 fps or lower</b>. Passthrough can't be switched on above 90.</li>
        <li>In the passthrough section, tick <b>Environment</b>. Leave Tracked keyboard, Hands and Desk portal off.</li>
        <li>Press <b>Configure</b> and set the colour to <b>Red 255, Green 0, Blue 255</b>, with <b>Similarity 4%</b>, <b>Smoothness 10%</b> and <b>Opacity 100%</b>.</li>
        <li>Back in this app, the <b>Passthrough</b> page shows a checklist of these settings.</li>
      </ol>
      ${shot('passthrough.png', 'The Passthrough page of the app', 'The Passthrough page: the colour Virtual Desktop must remove, and a checklist.')}`],

    ['wheel', 'Your wheel', `
      <ol class="g-steps">
        <li>Open the <b>Wheel</b> page. Your wheel base should say <b>Connected</b>. Turn the wheel and the <b>live angle</b> should follow. You don't need the headset or a game for this.</li>
        <li>Choose your <b>rim</b>. Standard rims are built in, and <b>Add a rim</b> makes one from a front-on photo of yours.</li>
        <li>Set the <b>steering range</b> to match your wheel software (for example Simucube Tuner). Hold the wheel straight and click <b>Set centre</b>.</li>
        <li>If the window turns the wrong way in VR, switch on <b>Flip direction</b>.</li>
      </ol>
      ${shot('wheel.png', 'The Wheel page of the app', 'The Wheel page: rim, wheel base, steering range and the live angle.')}
      <p class="g-tip"><b>Wheel base says “Not found” or the angle doesn't move?</b> Check its power and USB cable, and close Simucube Tuner (or your base's own app) while this page is open. Thin Place VR steps aside when Simucube's software is running.</p>`],

    ['first', 'Your first drive', `
      <ol class="g-steps">
        <li>Start the game in VR from Virtual Desktop's <b>Games</b> list. A few seconds in, the wheel window appears in front of you.</li>
        <li>Press ${key('Ctrl')}+${key('Alt')}+${key('H')} for <b>edit mode</b>. The <b>setup panel</b> appears beside you, listing your windows and the keys.</li>
        <li>The wheel is selected first. Look at your real wheel and press ${key('P')}. The window jumps to where you're looking.</li>
        <li>Fine-tune it with the arrow keys, ${key('R')}/${key('F')} (away/closer) and ${key('=')}/${key('−')} (size). Hold ${key('Shift')} for small steps.</li>
        <li>Press ${key('Tab')} to select the next window (Stream Deck, keyboard…) and do the same. ${key('M')} turns a window to face you, and ${key('T')}/${key('G')} tilts it to match the real thing.</li>
        <li>Press ${key('Esc')} to finish. Everything is saved automatically, separately for each game.</li>
      </ol>
      ${setupPanel}
      <p class="g-muted">The setup panel stays where it appeared. Press ${key('B')} to bring it back in front of you.</p>`],

    ['app', 'Adjusting from the app', `
      <p>Everything in edit mode can also be done from the <b>Windows</b> page while the game runs. Changes appear in the headset straight away. To use it without taking the headset off, open your desktop inside Virtual Desktop (menu → Desktop) and click the buttons with the controller.</p>
      <div class="g-row">${shot('position.png', 'The Position controls', 'Position: place, move all, nudge, tilt, turn, roll and face me.')}
      ${shot('windows-panel.png', 'The Window settings with a panel', 'Window: what it shows, its size, and when it appears.')}</div>`],

    ['panels', 'Panels: Discord, web pages, PDFs, camera, game data', `
      <ol class="g-steps">
        <li>On the <b>Windows</b> page, click <b>+</b> to add a window, then change <b>Shows</b> from “Real world” to what you want.</li>
        <li><b>A desktop window:</b> pick it from the list (↻ refreshes it). It can sit behind other windows but mustn't be minimised.</li>
        <li><b>A web page:</b> type the address and click <b>Show</b>. <b>A PDF:</b> choose the file. Scroll either with ${key('[')} and ${key(']')} in edit mode, or the arrows in the app.</li>
        <li><b>Game data:</b> a live dashboard, either AC EVO (gear, delta, laps, fuel, tyres) or Elite Dangerous (fuel, pips, status lights).</li>
        <li>New windows appear in front of you next time you're in VR. Place them like any other window.</li>
      </ol>
      <p class="g-tip">Panels need <b>this app to stay open</b>, because it copies the picture into the headset. The <b>Status</b> line on the window's settings says whether it's working.</p>`],

    ['keys', 'All the keys', `
      <table class="g-keys"><tbody>
        <tr><td>${key('Ctrl')}+${key('Alt')}+${key('H')}</td><td>Edit mode on / off (also ends after 30 s without a key)</td></tr>
        <tr><td>${key('Ctrl')}+${key('Alt')}+${key('V')}</td><td>Show / hide all windows</td></tr>
        <tr><td>${key('Ctrl')}+${key('Alt')}+${key('K')}</td><td>Setup panel on / off</td></tr>
        <tr class="g-sub"><td colspan="2">In edit mode</td></tr>
        <tr><td>${key('Tab')}</td><td>Select the next window</td></tr>
        <tr><td>${key('P')}</td><td>Place the selected window where you're looking</td></tr>
        <tr><td>${key('M')}</td><td>Turn it to face you</td></tr>
        <tr><td>${key('A')}</td><td>Move <b>all</b> windows together, back in front of you (after the headset has slept)</td></tr>
        <tr><td>${key('←')} ${key('↑')} ${key('→')} ${key('↓')}</td><td>Move</td></tr>
        <tr><td>${key('R')} / ${key('F')}</td><td>Away / closer</td></tr>
        <tr><td>${key('T')} / ${key('G')}</td><td>Tilt</td></tr>
        <tr><td>${key('Q')} / ${key('E')}</td><td>Turn</td></tr>
        <tr><td>${key('Z')} / ${key('C')}</td><td>Roll</td></tr>
        <tr><td>${key('=')} / ${key('−')}</td><td>Bigger / smaller</td></tr>
        <tr><td>${key('O')}</td><td>Selected window on / off</td></tr>
        <tr><td>${key('S')}</td><td>Change its shape</td></tr>
        <tr><td>${key('W')}</td><td>Next rim</td></tr>
        <tr><td>${key('X')} / ${key('I')}</td><td>Centre steering / flip direction</td></tr>
        <tr><td>${key('[')} / ${key(']')}</td><td>Scroll a panel</td></tr>
        <tr><td>${key('B')}</td><td>Bring the setup panel in front of you</td></tr>
        <tr><td>${key('Shift')}</td><td>Hold for small steps</td></tr>
        <tr><td>${key('Esc')}</td><td>Finish</td></tr>
      </tbody></table>`],

    ['help', 'If something goes wrong', `
      <dl class="g-faq">
        <dt>No windows appear in the game</dt>
        <dd>Check the <b>Home</b> page says the VR layer is active and the runtime is Virtual Desktop. The game must run through OpenXR (VDXR). Games with anti-cheat, such as Le Mans Ultimate, block Thin Place VR for now.</dd>
        <dt>I see bright pink shapes instead of my wheel</dt>
        <dd>Virtual Desktop's passthrough isn't on, or the colour doesn't match. Go through <b>First time: set up Virtual Desktop</b> again.</dd>
        <dt>All the windows have moved</dt>
        <dd>The headset slept or was recentred and its idea of the room moved. Look at your wheel and press ${key('Ctrl')}+${key('Alt')}+${key('H')} then ${key('A')}.</dd>
        <dt>The wheel window doesn't turn</dt>
        <dd>Check the <b>Wheel</b> page: the base must say Connected and the live angle must move.</dd>
        <dt>A panel says “Waiting for the Thin Place VR app”</dt>
        <dd>Open this app on the PC. For a desktop window, make sure it's open and not minimised.</dd>
        <dt>Still stuck?</dt>
        <dd>Click <b>Send a report</b> at the bottom of the sidebar. It saves a zip to your desktop with the logs and settings. Send it, with a sentence on what happened, to whoever gave you Thin Place VR.</dd>
      </dl>`],
  ];

  return `
    <div class="head"><div><div class="eyebrow">Guide</div><h1>How to use Thin Place VR</h1></div></div>
    <nav class="g-toc" aria-label="Guide sections">${sections.map(([id, t]) => `<a href="#g-${id}" data-gid="${id}">${t}</a>`).join('')}</nav>
    <div class="g-body">${sections.map(([id, t, body]) => `<section class="card g-sec" id="g-${id}" aria-labelledby="g-${id}-h"><h2 id="g-${id}-h">${t}</h2>${body}</section>`).join('')}</div>`;
})();
