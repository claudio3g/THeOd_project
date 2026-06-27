#pragma once

/*
 * ============================================================================
 * web_page.h — Dashboard HTML servita da GET /
 *
 * MODIFICHE v3:
 *   - Titolo aggiornato a "THeOd LoRa Hub"
 *   - Sezione "LoRa / Meshtastic" aggiunta tra Batteria e Sistema:
 *       Card RSSI con indicatore visivo a barre (3 barre CSS-only)
 *       Card SNR e qualità segnale
 *       Badge "Meshtastic ready" (stub, attivato quando loraReady=true)
 *   - Touch e Hall mostrano il valore FILTRATO (touchFilt/hallFilt) in grande
 *     e il valore raw in piccolo per debug
 *   - CSS: aggiunto stile .lora-bars per indicatore visivo RSSI
 *
 * ARCHITETTURA DATI (invariata):
 *   GET /data    (ogni 2 s)  → JSON con loraRssi, loraSnr, loraLabel aggiunti
 *   GET /battery (ogni 30 s) → JSON storico %
 *   GET /lora    (ogni 5 s)  → JSON LoRa completo (nuovo v3)
 *   GET /oled    (on click)  → toggle OLED
 * ============================================================================
 */

const char PAGE_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="it">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>THeOd LoRa Hub</title>
<style>
:root {
  --bg:#eef2f5; --card:#ffffff; --text:#2c3a47; --muted:#7c8a98;
  --border:rgba(44,58,71,.09); --accent:#4f8a8b; --a-soft:rgba(79,138,139,.13);
  --warn:#c6873a; --danger:#c0392b; --ok:#27ae60; --off:#c0c8d0;
  --radius:13px; --sh:0 2px 10px rgba(44,58,71,.08);
  --lora:#6b48c8;  /* viola LoRa */
}
@media(prefers-color-scheme:dark){
  :root{
    --bg:#121820; --card:#1c2530; --text:#cdd4db; --muted:#576370;
    --border:rgba(255,255,255,.07); --accent:#5a9a9b; --a-soft:rgba(79,138,139,.18);
    --off:#2e3c4a; --sh:0 2px 10px rgba(0,0,0,.35); --lora:#9b7fe8;
  }
}
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
  padding:18px 14px 48px;-webkit-font-smoothing:antialiased;font-size:14px}
.wrap{max-width:480px;margin:0 auto}
header{text-align:center;margin-bottom:18px}
header h1{font-size:1.2rem;font-weight:600;letter-spacing:-.01em}
header p{color:var(--muted);font-size:.75rem;margin-top:2px}
.sec{font-size:.65rem;font-weight:700;text-transform:uppercase;
  letter-spacing:.06em;color:var(--muted);margin:16px 0 7px}
.card{background:var(--card);border-radius:var(--radius);box-shadow:var(--sh);padding:14px 12px}

/* Griglia sensori 3 colonne */
.g3{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.sensor-lbl{font-size:.62rem;font-weight:700;text-transform:uppercase;
  letter-spacing:.05em;color:var(--muted);margin-bottom:3px}
.sensor-val{font-size:1.55rem;font-weight:700;line-height:1;transition:color .25s}
.sensor-sub{font-size:.7rem;color:var(--muted);margin-top:3px;min-height:1em}
.sensor-raw{font-size:.6rem;color:var(--muted);margin-top:1px}
.c-ok  {color:var(--accent)}
.c-warn{color:var(--warn)}

/* Batteria */
.bat-row{display:flex;align-items:center;gap:14px}
.bat-icon{display:flex;align-items:center;flex:none}
.bat-shell{width:54px;height:26px;border:2.5px solid var(--accent);
  border-radius:5px;padding:3px;overflow:hidden;position:relative}
.bat-fill{height:100%;border-radius:3px;background:var(--accent);
  width:0%;transition:width .7s ease,background .4s}
.bat-nub{width:5px;height:11px;background:var(--accent);
  border-radius:0 3px 3px 0;margin-left:-1px;flex:none;transition:background .4s}
.bat-info{flex:1;min-width:0}
.bat-pct{font-size:1.9rem;font-weight:700;line-height:1;letter-spacing:-.03em}
.bat-pct span{font-size:1rem;font-weight:400;color:var(--muted)}
.bat-volt{font-size:.78rem;color:var(--muted);margin-top:1px}
.bat-status{font-size:.8rem;margin-top:5px}
.bat-eta{font-size:.73rem;color:var(--muted);margin-top:2px;min-height:1em}

/* Grafico */
.graph-card{padding:12px 12px 10px}
canvas#g{width:100%;height:130px;display:block}

/* LoRa card */
.lora-row{display:flex;align-items:center;gap:14px}
.lora-bars{display:flex;align-items:flex-end;gap:3px;height:28px;flex:none}
.lora-bar{width:8px;border-radius:2px 2px 0 0;background:var(--off);transition:background .4s}
.lora-bar.b1{height:9px}
.lora-bar.b2{height:18px}
.lora-bar.b3{height:28px}
.lora-bars.good  .lora-bar{background:var(--ok)}
.lora-bars.fair  .lora-bar.b1,.lora-bars.fair .lora-bar.b2{background:var(--warn)}
.lora-bars.weak  .lora-bar.b1{background:var(--danger)}
.lora-bars.none  .lora-bar{background:var(--off)}
.lora-info{flex:1;min-width:0}
.lora-rssi{font-size:1.55rem;font-weight:700;line-height:1}
.lora-label{font-size:.78rem;color:var(--muted);margin-top:2px}
.lora-snr{font-size:.73rem;color:var(--muted);margin-top:1px}
.lora-mesh{display:inline-block;margin-top:6px;padding:2px 8px;
  border-radius:20px;font-size:.63rem;font-weight:600;text-transform:uppercase;
  letter-spacing:.04em;background:var(--a-soft);color:var(--accent)}

/* Sistema */
.sys-list{overflow:hidden}
.sys-row{display:flex;align-items:center;justify-content:space-between;
  padding:13px 14px;border-bottom:1px solid var(--border)}
.sys-row:last-child{border-bottom:none}
.sys-lbl{font-size:.88rem;font-weight:500}
.sys-sub{font-size:.72rem;color:var(--muted);margin-top:1px}
.sys-right{display:flex;align-items:center;gap:10px;flex:none}
.tog{width:44px;height:24px;background:var(--off);border-radius:12px;
  position:relative;cursor:pointer;transition:background .2s;flex:none}
.tog.on{background:var(--accent)}
.tog-k{position:absolute;width:18px;height:18px;background:#fff;border-radius:50%;
  top:3px;left:3px;transition:transform .2s;box-shadow:0 1px 3px rgba(0,0,0,.28)}
.tog.on .tog-k{transform:translateX(20px)}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;
  background:var(--off);transition:background .3s;flex:none}
.dot.on{background:var(--accent)}
footer{text-align:center;margin-top:18px;font-size:.68rem;color:var(--muted);line-height:1.6}
</style>
</head>
<body>
<div class="wrap">

<header>
  <h1>&#128225; THeOd LoRa Hub</h1>
  <p>Heltec WiFi LoRa 32 V2 &middot; v3</p>
</header>

<!-- SENSORI -->
<div class="sec">Sensori</div>
<div class="g3">
  <div class="card">
    <div class="sensor-lbl">Touch</div>
    <div class="sensor-val" id="tVal">--</div>
    <div class="sensor-sub" id="tSub">--</div>
    <div class="sensor-raw" id="tRaw"></div>
  </div>
  <div class="card">
    <div class="sensor-lbl">Hall</div>
    <div class="sensor-val" id="hVal">--</div>
    <div class="sensor-sub" id="hSub">--</div>
    <div class="sensor-raw" id="hRaw"></div>
  </div>
  <div class="card">
    <div class="sensor-lbl">Pulsante</div>
    <div class="sensor-val" id="bVal">--</div>
    <div class="sensor-sub">&nbsp;</div>
  </div>
</div>

<!-- BATTERIA -->
<div class="sec">Batteria</div>
<div class="card" style="margin-bottom:8px">
  <div class="bat-row">
    <div class="bat-icon">
      <div class="bat-shell"><div class="bat-fill" id="bFill"></div></div>
      <div class="bat-nub" id="bNub"></div>
    </div>
    <div class="bat-info">
      <div class="bat-pct"><span id="bPct">--</span><span>%</span></div>
      <div class="bat-volt" id="bVolt">--</div>
      <div class="bat-status" id="bStat">--</div>
      <div class="bat-eta" id="bEta">&nbsp;</div>
    </div>
  </div>
</div>

<!-- GRAFICO STORICO -->
<div class="sec">Storico carica (4 h)</div>
<div class="card graph-card">
  <canvas id="g"></canvas>
</div>

<!-- LORA / MESHTASTIC — NUOVO v3 -->
<div class="sec">LoRa &amp; Meshtastic</div>
<div class="card">
  <div class="lora-row">
    <div class="lora-bars none" id="loraBars">
      <div class="lora-bar b1"></div>
      <div class="lora-bar b2"></div>
      <div class="lora-bar b3"></div>
    </div>
    <div class="lora-info">
      <div class="lora-rssi" id="loraRssiVal">--</div>
      <div class="lora-label" id="loraLabel">In attesa segnale...</div>
      <div class="lora-snr"  id="loraSnrVal"></div>
      <div><span class="lora-mesh" id="loraMesh">Meshtastic: stub</span></div>
    </div>
  </div>
</div>

<!-- SISTEMA -->
<div class="sec">Sistema</div>
<div class="card sys-list">
  <div class="sys-row">
    <div>
      <div class="sys-lbl">Display OLED</div>
      <div class="sys-sub">Spegni per risparmiare corrente</div>
    </div>
    <div class="tog" id="oTog" onclick="toggleOled()">
      <div class="tog-k"></div>
    </div>
  </div>
  <div class="sys-row">
    <div>
      <div class="sys-lbl"><span class="dot" id="lDot"></span>&nbsp;LED onboard</div>
      <div class="sys-sub" id="lStat">--</div>
    </div>
  </div>
  <div class="sys-row">
    <div class="sys-lbl">Client Wi-Fi</div>
    <div class="sys-right" id="cliInfo">--</div>
  </div>
</div>

<footer id="ft">
  Sensori: 2&nbsp;s &middot; Grafico: 30&nbsp;s &middot; LoRa: 5&nbsp;s<br>
  <span id="ipFt"></span>
</footer>

</div><!-- .wrap -->

<script>
'use strict';
var oledOn    = true;
var histData  = [];
var histInt   = 30;
var histLowPc = 45;

function fmtMin(m) {
  if (m < 0)   return '\u2014';
  if (m === 0) return '< 1 min';
  if (m < 60)  return m + ' min';
  var h = Math.floor(m/60), mm = m%60;
  return mm ? h+'h '+mm+'m' : h+'h';
}
function fmtSec(s) {
  if (s < 60)   return s+'s';
  if (s < 3600) return Math.floor(s/60)+'m';
  var h=Math.floor(s/3600), m=Math.floor((s%3600)/60);
  return m ? h+'h'+m+'m' : h+'h';
}
function batColor(pct) {
  return pct < 20 ? 'var(--danger)' : pct < 40 ? 'var(--warn)' : 'var(--accent)';
}

// ── Aggiornamento sensori + batteria + LoRa (ogni 2 s) ──────────────────────
async function refreshData() {
  var d;
  try { d = await fetch('/data').then(function(r){return r.json()}); }
  catch(e) { return; }

  // Touch — mostra valore FILTRATO in grande, raw in piccolo
  var tv = document.getElementById('tVal');
  var ts = document.getElementById('tSub');
  var tr = document.getElementById('tRaw');
  tv.textContent = (typeof d.touchFilt === 'number') ? Math.round(d.touchFilt) : d.touch;
  tv.className = 'sensor-val' + (d.touchActive ? ' c-ok' : '');
  ts.textContent = d.touchActive ? 'Attivo' : 'Libero';
  ts.className = 'sensor-sub' + (d.touchActive ? ' c-ok' : '');
  tr.textContent = 'raw: ' + d.touch;

  // Hall — mostra valore FILTRATO in grande, raw in piccolo
  var hv = document.getElementById('hVal');
  var hs = document.getElementById('hSub');
  var hr = document.getElementById('hRaw');
  hv.textContent = (typeof d.hallFilt === 'number') ? d.hallFilt.toFixed(1) : d.hall;
  hv.className = 'sensor-val' + (d.magnet ? ' c-warn' : '');
  hs.textContent = d.magnet ? 'Magnete!' : 'Assente';
  hs.className = 'sensor-sub' + (d.magnet ? ' c-warn' : '');
  hr.textContent = 'raw: ' + d.hall;

  // Pulsante
  var bv = document.getElementById('bVal');
  bv.textContent = d.button ? '\u25CF' : '\u25CB';
  bv.className = 'sensor-val' + (d.button ? ' c-ok' : '');

  // Batteria gauge
  var pct = Math.max(0, Math.min(100, d.batPct));
  var clr = batColor(pct);
  var fill = document.getElementById('bFill');
  fill.style.width = (d.batPct >= 0 ? pct : 0) + '%';
  fill.style.background = clr;
  document.getElementById('bNub').style.background = clr;
  document.getElementById('bPct').textContent = d.batPct >= 0 ? Math.round(pct) : '--';
  document.getElementById('bVolt').textContent = d.batV >= 0 ? d.batV.toFixed(2)+' V' : 'N/D';
  var st = document.getElementById('bStat');
  if (d.batWarn) {
    st.textContent = '\u26A0\uFE0F Batteria bassa \u2014 deep sleep imminente';
    st.style.color = 'var(--danger)';
  } else if (d.batChg) {
    st.textContent = '\u2191 In carica';
    st.style.color = 'var(--ok)';
  } else {
    st.textContent = '\u2193 In uso';
    st.style.color = 'var(--muted)';
  }
  var et = document.getElementById('bEta');
  if (d.batEst) {
    et.textContent = 'Calcolo in corso\u2026';
  } else if (d.batChg && d.batEta > 0) {
    et.textContent = 'Carica completa: ' + fmtMin(d.batEta);
  } else if (!d.batChg && d.batEta > 0) {
    et.textContent = 'Autonomia stimata: ' + fmtMin(d.batEta);
  } else {
    et.textContent = '\u00A0';
  }

  // LoRa RSSI (aggiornamento leggero da /data, dettaglio da refreshLora)
  updateLoraUI(d.loraRssi, d.loraSnr, d.loraLabel, d.loraReady);

  // OLED toggle
  oledOn = d.oled;
  document.getElementById('oTog').classList.toggle('on', d.oled);

  // LED pattern
  var dot = document.getElementById('lDot');
  dot.classList.toggle('on', d.ledPat !== 'off');
  var patLabels = {
    'off':'Spento','dim':'Dim \u2014 preavviso scarica',
    'fade':'Fade \u2014 client connessi','full':'Acceso \u2014 carica completa',
    'puls':'Pulse \u2014 in carica'
  };
  document.getElementById('lStat').textContent = patLabels[d.ledPat] || d.ledPat;

  // Client
  var n = d.clients;
  document.getElementById('cliInfo').textContent =
    n === 0 ? 'Nessun client' : n === 1 ? '1 client connesso' : n+' client connessi';

  document.getElementById('ipFt').textContent = 'AP: '+d.ip;
}

// ── Aggiornamento UI LoRa (condiviso tra /data e /lora) ─────────────────────
function updateLoraUI(rssi, snr, label, ready) {
  var bars  = document.getElementById('loraBars');
  var rVal  = document.getElementById('loraRssiVal');
  var lbl   = document.getElementById('loraLabel');
  var snrEl = document.getElementById('loraSnrVal');
  var mesh  = document.getElementById('loraMesh');

  if (!ready) {
    bars.className = 'lora-bars none';
    rVal.textContent = 'N/D';
    lbl.textContent  = 'Modulo LoRa non disponibile';
    snrEl.textContent = '';
    mesh.textContent  = 'Meshtastic: non attivo';
    return;
  }

  if (!rssi || rssi === 0) {
    bars.className = 'lora-bars none';
    rVal.textContent = '---';
    lbl.textContent  = 'In ascolto...';
    snrEl.textContent = '';
    mesh.textContent  = 'Meshtastic: stub attivo';
    return;
  }

  rVal.textContent = rssi + ' dBm';

  // Classe qualità per animazione barre CSS
  var cls = 'none';
  if      (label === 'Buono')  cls = 'good';
  else if (label === 'OK')     cls = 'fair';
  else if (label === 'Debole') cls = 'weak';
  bars.className = 'lora-bars ' + cls;

  lbl.textContent = label || '---';
  snrEl.textContent = (snr && snr !== 0) ? 'SNR: ' + snr.toFixed(1) + ' dB' : '';
  mesh.textContent  = 'Meshtastic: stub attivo';
}

// ── Aggiornamento LoRa dettagliato da /lora (ogni 5 s) ──────────────────────
async function refreshLora() {
  var d;
  try { d = await fetch('/lora').then(function(r){return r.json()}); }
  catch(e) { return; }
  updateLoraUI(d.rssi, d.snr, d.label, d.ready);
}

// ── Aggiornamento storico batteria + grafico (ogni 30 s) ────────────────────
async function refreshHistory() {
  var d;
  try { d = await fetch('/battery').then(function(r){return r.json()}); }
  catch(e) { return; }
  histData  = d.history  || [];
  histInt   = d.interval || 30;
  histLowPc = (typeof d.lowPct === 'number') ? d.lowPct : histLowPc;
  drawGraph();
}

// ── Grafico Canvas HiDPI-aware ───────────────────────────────────────────────
function drawGraph() {
  var canvas = document.getElementById('g');
  var dpr = window.devicePixelRatio || 1;
  var rect = canvas.getBoundingClientRect();
  var needW = Math.round(rect.width  * dpr);
  var needH = Math.round(rect.height * dpr);
  if (canvas.width !== needW || canvas.height !== needH) {
    canvas.width  = needW;
    canvas.height = needH;
  }
  var ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  var W = rect.width, H = rect.height;
  var pad = {t:8, r:8, b:22, l:30};
  var gW = W - pad.l - pad.r;
  var gH = H - pad.t - pad.b;
  var n = histData.length;

  var dark = window.matchMedia('(prefers-color-scheme:dark)').matches;
  var cTxt  = dark ? '#576370' : '#7c8a98';
  var cGrid = dark ? 'rgba(255,255,255,.055)' : 'rgba(0,0,0,.05)';
  var cLine = dark ? '#5a9a9b' : '#4f8a8b';
  var cWarn = dark ? 'rgba(198,135,58,.5)'   : 'rgba(198,135,58,.55)';
  var cFT   = dark ? 'rgba(79,138,139,.32)'  : 'rgba(79,138,139,.2)';
  var cFB   = 'rgba(79,138,139,0)';

  ctx.clearRect(0, 0, W, H);
  ctx.font = '9px -apple-system,system-ui,sans-serif';

  if (n < 2) {
    ctx.fillStyle = cTxt;
    ctx.textAlign = 'center';
    ctx.fillText('Raccogliendo dati\u2026', W/2, H/2 - 2);
    if (n === 1) ctx.fillText('(1 campione su '+Math.min(histData.length+1,480)+')', W/2, H/2+10);
    return;
  }

  var ix = function(i){ return pad.l + (i/(n-1)) * gW; };
  var py = function(p){ return pad.t + (1 - p/100) * gH; };

  ctx.lineWidth = 1;
  [0, 25, 50, 75, 100].forEach(function(p) {
    var y = py(p);
    ctx.beginPath();
    ctx.strokeStyle = cGrid;
    ctx.moveTo(pad.l, y); ctx.lineTo(pad.l+gW, y);
    ctx.stroke();
    ctx.fillStyle = cTxt;
    ctx.textAlign = 'right';
    ctx.fillText(p+'%', pad.l-3, y+3);
  });

  var ly = py(histLowPc);
  ctx.beginPath();
  ctx.strokeStyle = cWarn;
  ctx.lineWidth = 1;
  ctx.setLineDash([4,3]);
  ctx.moveTo(pad.l, ly); ctx.lineTo(pad.l+gW, ly);
  ctx.stroke();
  ctx.setLineDash([]);

  var grad = ctx.createLinearGradient(0, pad.t, 0, pad.t+gH);
  grad.addColorStop(0, cFT);
  grad.addColorStop(1, cFB);
  ctx.beginPath();
  for (var i=0; i<n; i++) {
    var x=ix(i), y=py(histData[i]);
    i===0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
  }
  ctx.lineTo(ix(n-1), pad.t+gH);
  ctx.lineTo(pad.l,   pad.t+gH);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  ctx.beginPath();
  for (var i=0; i<n; i++) {
    var x=ix(i), y=py(histData[i]);
    i===0 ? ctx.moveTo(x,y) : ctx.lineTo(x,y);
  }
  ctx.strokeStyle = cLine;
  ctx.lineWidth = 2;
  ctx.lineJoin = 'round';
  ctx.lineCap  = 'round';
  ctx.stroke();

  var lx=ix(n-1), lv=histData[n-1], ly2=py(lv);
  ctx.beginPath();
  ctx.arc(lx, ly2, 3.5, 0, Math.PI*2);
  ctx.fillStyle = cLine;
  ctx.fill();
  if (lx > pad.l + 30) {
    ctx.fillStyle = cLine;
    ctx.textAlign = lx > W - 35 ? 'right' : 'center';
    ctx.fillText(Math.round(lv)+'%', lx + (lx > W-35 ? -6 : 0), ly2 - 6);
  }

  var tot = (n-1)*histInt;
  ctx.fillStyle = cTxt;
  ctx.textAlign = 'center';
  [[0,'-'+fmtSec(tot)],[1/3,'-'+fmtSec(Math.round(tot*2/3))],
   [2/3,'-'+fmtSec(Math.round(tot/3))],[1,'Ora']].forEach(function(e) {
    ctx.fillText(e[1], pad.l + e[0]*gW, H-4);
  });
}

// ── Toggle OLED ──────────────────────────────────────────────────────────────
async function toggleOled() {
  var next = !oledOn;
  try {
    await fetch('/oled?state='+(next?'on':'off'));
    oledOn = next;
    document.getElementById('oTog').classList.toggle('on', next);
  } catch(e) {}
}

// ── Avvio ────────────────────────────────────────────────────────────────────
refreshData();
refreshHistory();
refreshLora();
setInterval(refreshData,    2000);
setInterval(refreshHistory, 30000);
setInterval(refreshLora,    5000);   // Polling LoRa dettagliato ogni 5 s
window.addEventListener('resize', function() {
  if (histData.length > 1) drawGraph();
});
</script>
</body>
</html>
)rawliteral";
