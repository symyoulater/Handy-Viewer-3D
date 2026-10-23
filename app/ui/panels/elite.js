// Elite Dangerous status: the app reads the game's Status.json and sends it here.
(() => {
  const $ = (id) => document.getElementById(id);
  const CHIPS = [
    ['gear', 'Landing gear', ''], ['hardpoints', 'Hardpoints', 'warn'], ['scoop', 'Cargo scoop', ''],
    ['lights', 'Lights', ''], ['silent', 'Silent running', 'warn'], ['faOff', 'Flight assist off', 'warn'],
    ['massLocked', 'Mass locked', ''], ['fsdCharging', 'FSD charging', 'warn'], ['fsdCooldown', 'FSD cooldown', ''],
    ['overheating', 'Heat', 'bad'], ['danger', 'Danger', 'bad'], ['interdicted', 'Interdiction', 'bad'],
  ];
  $('chips').innerHTML = CHIPS.map(([k, label, kind]) => `<div class="chip ${kind}" id="c_${k}">${label}</div>`).join('');
  const pips = (el, halfPips) => {
    [...el.querySelectorAll('i')].forEach((p, i) => {
      const v = halfPips - i * 2;
      p.className = v >= 2 ? 'full' : v === 1 ? 'half' : '';
    });
  };

  function render(d) {
    $('wait').hidden = !!d.ok;
    if (!d.ok) { $('wait').textContent = d.message || 'Waiting for Elite Dangerous'; return; }
    $('state').textContent = d.docked ? 'Docked' : d.landed ? 'Landed' : d.inSrv ? 'In SRV' : d.supercruise ? 'Supercruise' : d.fuelScooping ? 'Fuel scooping' : 'Normal space';
    $('legal').textContent = d.legal || '';
    $('legal').className = 'legal' + (/Wanted|Hostile|Warrant/i.test(d.legal || '') ? ' bad' : '');
    const fuel = $('fuel');
    fuel.innerHTML = (d.fuelMain != null ? d.fuelMain.toFixed(1) : '–') + '<small>t</small>';
    fuel.className = 'big num' + (d.lowFuel ? ' low' : '');
    $('res').textContent = d.fuelReservoir != null ? d.fuelReservoir.toFixed(2) + ' t' : '–';
    if (d.pips) { pips($('pSys'), d.pips[0]); pips($('pEng'), d.pips[1]); pips($('pWep'), d.pips[2]); }
    CHIPS.forEach(([k]) => $('c_' + k).classList.toggle('on', !!d[k]));
    $('cargo').textContent = Math.round(d.cargo || 0) + ' t';
    $('bal').textContent = d.balance >= 0 ? Math.round(d.balance).toLocaleString('en-GB') + ' CR' : '–';
    $('dest').textContent = d.destination || '–';
    $('altRow').hidden = d.altitude == null; $('hdgRow').hidden = d.heading == null;
    if (d.altitude != null) $('alt').textContent = d.altitude >= 1000 ? (d.altitude / 1000).toFixed(1) + ' km' : Math.round(d.altitude) + ' m';
    if (d.heading != null) $('hdg').textContent = Math.round(d.heading) + '°';
  }
  if (window.chrome && window.chrome.webview) window.chrome.webview.addEventListener('message', (e) => render(e.data));
})();
