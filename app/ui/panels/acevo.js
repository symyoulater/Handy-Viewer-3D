// AC EVO dashboard: the app sends the latest numbers ten times a second.
(() => {
  const $ = (id) => document.getElementById(id);
  const time = (ms) => {
    if (!ms || ms <= 0 || ms > 3600000) return '–';
    const m = Math.floor(ms / 60000), s = ((ms % 60000) / 1000).toFixed(3);
    return m + ':' + (s < 10 ? '0' : '') + s;
  };
  const names = ['FL', 'FR', 'RL', 'RR'];
  $('tyres').innerHTML = names.map((n, i) => `<div class="tyre" id="t${i}"><span>${n}</span><b>–</b><span>–</span></div>`).join('');

  function render(d) {
    $('wait').hidden = !!d.ok;
    if (!d.ok) { $('wait').textContent = d.message || 'Waiting for AC EVO'; return; }
    const rpm = $('rpm');
    rpm.firstElementChild.style.width = Math.max(0, Math.min(1, d.rpmPct)) * 100 + '%';
    rpm.className = 'rpm' + (d.shiftUp || d.limiter ? ' shift' : d.rpmPct > 0.92 ? ' red' : d.rpmPct > 0.8 ? ' amber' : '');
    $('gear').textContent = d.gear;
    $('speed').textContent = d.speed;
    $('tc').className = 'chip' + (d.tc ? ' on' : '');
    $('abs').className = 'chip' + (d.abs ? ' on' : '');
    $('gas').firstElementChild.style.width = d.gas * 100 + '%';
    $('brk').firstElementChild.style.width = d.brake * 100 + '%';
    const delta = $('delta');
    if (d.bestMs > 0 && Math.abs(d.deltaMs) < 600000) {
      delta.textContent = (d.deltaMs > 0 ? '+' : d.deltaMs < 0 ? '−' : '') + (Math.abs(d.deltaMs) / 1000).toFixed(2);
      delta.className = 'delta num ' + (d.deltaMs > 0 ? 'bad' : 'good');
    } else { delta.textContent = '–'; delta.className = 'delta num'; }
    $('lapT').textContent = time(d.lapMs);
    $('last').textContent = time(d.lastMs);
    $('best').textContent = time(d.bestMs);
    $('pred').textContent = time(d.predictedMs);
    $('pos').textContent = d.pos > 0 ? `P${d.pos}${d.cars > 0 ? '/' + d.cars : ''}` : '–';
    $('lap').textContent = d.lap >= 0 ? d.lap + 1 : '–';
    $('fuel').textContent = d.fuel.toFixed(1) + ' L';
    $('fuelLaps').textContent = d.fuelLaps > 0 && d.fuelLaps < 1000 ? d.fuelLaps.toFixed(1) : '–';
    $('fuelPer').textContent = d.fuelPerLap > 0 ? d.fuelPerLap.toFixed(2) + ' L' : '–';
    d.tyres.forEach((t, i) => {
      const el = $('t' + i);
      el.className = 'tyre' + (t.normTemp > 0 ? (t.normTemp < 0.9 ? ' cold' : t.normTemp > 1.1 ? ' hot' : ' ok') : '') + (t.lock ? ' lock' : '');
      el.children[1].textContent = Math.round(t.temp) + '°';
      el.children[2].textContent = t.psi.toFixed(1) + ' psi';
    });
  }
  if (window.chrome && window.chrome.webview) window.chrome.webview.addEventListener('message', (e) => render(e.data));
})();
