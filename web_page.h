#pragma once

/*
 * ============================================================================
 * web_page.h — Dashboard HTML servita da GET /
 *
 * v4 — Restyling layout: widget LED+Pulsante unificato
 *
 * CAMBIAMENTI v4 rispetto a v3:
 *   - Griglia sensori: 3 card separate → 2 card sensori + 1 widget unificato
 *   - Widget in alto a destra: LED animato + stato pulsante + override 3 tasti
 *   - Sezione Sistema: rimossa riga LED (ora in alto), rimane solo OLED + WiFi
 *   - Titolo aggiornato a v4
 *   - Nessuna modifica al firmware (.ino, .h) — solo web_page.h
 *
 * ARCHITETTURA DATI (invariata dalla v3):
 *   GET /data    (ogni 2 s)  → JSON completo sensori + batteria + LoRa + ledOverride
 *   GET /battery (ogni 30 s) → JSON storico %
 *   GET /lora    (ogni 5 s)  → JSON LoRa completo
 *   GET /oled    (on click)  → toggle OLED
 *   GET /led     (on click)  → override LED (auto|off|on)
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
  --lora:#6b48c8;
}
@media(prefers-color-scheme:dark){
  :root{
    --bg:#121820; --card:#1c2530; --text:#cdd4db; --muted:#8a96a3;
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

/* Griglia sensori: Touch | Hall | [LED+Button widget] */
.g3{display:grid;grid-template-columns:1fr 1fr 1.4fr;gap:8px}
.sensor-lbl{font-size:.62rem;font-weight:700;text-transform:uppercase;
  letter-spacing:.05em;color:var(--muted);margin-bottom:3px}
.sensor-val{font-size:1.55rem;font-weight:700;line-height:1;transition:color .25s}
.sensor-sub{font-size:.7rem;color:var(--muted);margin-top:3px;min-height:1em}
.sensor-raw{font-size:.6rem;color:var(--muted);margin-top:1px}
.c-ok  {color:var(--accent)}
.c-warn{color:var(--warn)}

/* Widget unificato LED + Pulsante (colonna destra griglia) */
.led-card{display:flex;flex-direction:column;justify-content:space-between;gap:6px}
.led-top{display:flex;align-items:center;gap:8px}
.led-bulb{width:28px;height:28px;border-radius:50%;background:var(--off);
  flex:none;transition:background .3s,box-shadow .3s}
.led-bulb.pat-solid{background:#27ae60;box-shadow:0 0 10px rgba(39,174,96,.55)}
.led-bulb.pat-fade{background:#27ae60;animation:ledFade 2s linear infinite}
.led-bulb.pat-pulse{animation:ledPulse 1.4s ease-in-out infinite}
.led-bulb.pat-dim{background:#2a3a2a}
.led-bulb.pat-off{background:var(--off);box-shadow:none}
/* Stati termici: stesso widget bulb, colori semantici */
.led-bulb.therm-normal{background:var(--ok);box-shadow:0 0 6px rgba(39,174,96,.4)}
.led-bulb.therm-elevated{background:#8bc34a}
.led-bulb.therm-warning{background:var(--warn);box-shadow:0 0 8px rgba(198,135,58,.5)}
.led-bulb.therm-critical{background:var(--danger);animation:ledPulse 1s ease-in-out infinite}
.led-bulb.therm-emergency{background:var(--danger);animation:ledPulse .4s ease-in-out infinite;
  box-shadow:0 0 14px rgba(192,57,43,.8)}
@keyframes ledFade{
  0%{background:#27ae60;box-shadow:0 0 8px rgba(39,174,96,.5)}
  100%{background:#1a2a1a;box-shadow:none}}
@keyframes ledPulse{
  0%,100%{background:#1a2a1a;box-shadow:none}
  50%{background:#27ae60;box-shadow:0 0 10px rgba(39,174,96,.6)}}
.led-meta{flex:1;min-width:0}
.led-pat-name{font-size:.75rem;font-weight:600;line-height:1.2}
.led-btn-row{display:flex;gap:4px}
.led-btn-row{margin-top:2px}
.led-btn{flex:1;padding:4px 0;border:1.5px solid var(--border);border-radius:7px;
  background:transparent;color:var(--muted);font-size:.6rem;font-weight:700;
  cursor:pointer;transition:all .2s;text-transform:uppercase;letter-spacing:.03em}
.led-btn.active{border-color:var(--accent);background:var(--a-soft);color:var(--accent)}
/* Stato pulsante fisico */
.btn-state{display:flex;align-items:center;gap:5px;margin-top:4px;
  font-size:.68rem;color:var(--muted)}
.btn-dot{width:7px;height:7px;border-radius:50%;background:var(--off);
  flex:none;transition:background .2s}
.btn-dot.pressed{background:var(--accent)}

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

/* LoRa */
.lora-row{display:flex;align-items:center;gap:14px}
.lora-bars{display:flex;align-items:flex-end;gap:3px;height:28px;flex:none}
.lora-bar{width:8px;border-radius:2px 2px 0 0;background:var(--off);transition:background .4s}
.lora-bar.b1{height:9px}.lora-bar.b2{height:18px}.lora-bar.b3{height:28px}
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
.tog{width:44px;height:24px;background:var(--off);border-radius:12px;
  position:relative;cursor:pointer;transition:background .2s;flex:none}
.tog.on{background:var(--accent)}
.tog-k{position:absolute;width:18px;height:18px;background:#fff;border-radius:50%;
  top:3px;left:3px;transition:transform .2s;box-shadow:0 1px 3px rgba(0,0,0,.28)}
.tog.on .tog-k{transform:translateX(20px)}

footer{text-align:center;margin-top:18px;font-size:.68rem;color:var(--muted);line-height:1.6}
</style>
</head>
<body>
<div class="wrap">

<header>
  <h1>&#128225; THeOd LoRa Hub</h1>
  <p>Heltec WiFi LoRa 32 V2.1 &middot; v4</p>
</header>

<!-- SENSORI + WIDGET LED/PULSANTE -->
<div class="sec">Sensori</div>
<div class="g3">

  <!-- Touch -->
  <div class="card">
    <div class="sensor-lbl">Touch</div>
    <div class="sensor-val" id="tVal">--</div>
    <div class="sensor-sub" id="tSub">--</div>
    <div class="sensor-raw" id="tRaw"></div>
  </div>

  <!-- Hall -->
  <div class="card">
    <div class="sensor-lbl">Hall</div>
    <div class="sensor-val" id="hVal">--</div>
    <div class="sensor-sub" id="hSub">--</div>
    <div class="sensor-raw" id="hRaw"></div>
  </div>

  <!-- Widget unificato: LED + Pulsante fisico -->
  <div class="card led-card">
    <div class="sensor-lbl">LED &amp; Tasto</div>
    <div class="led-top">
      <div class="led-bulb pat-off" id="ledBulb"></div>
      <div class="led-meta">
        <div class="led-pat-name" id="ledPatName">--</div>
      </div>
    </div>
    <!-- Stato pulsante fisico BOOT -->
    <div class="btn-state">
      <div class="btn-dot" id="btnDot"></div>
      <span id="btnLabel">Tasto rilasciato</span>
    </div>
    <!-- Override LED -->
    <div class="led-btn-row">
      <button class="led-btn active" id="ledBtnAuto" onclick="setLed('auto')">Auto</button>
      <button class="led-btn"        id="ledBtnOff"  onclick="setLed('off')">Off</button>
      <button class="led-btn"        id="ledBtnOn"   onclick="setLed('on')">On</button>
    </div>
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

<!-- LORA / MESHTASTIC -->
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

<!-- SISTEMA: solo OLED e WiFi -->
<!-- GPS BeITain BN-220 -->
<div class="sec">GPS</div>
<div class="card">
  <div class="lora-row">
    <div style="display:flex;flex-direction:column;align-items:center;gap:4px;flex:none;width:36px">
      <div style="font-size:1.4rem" id="gpsIcon">&#128satellite;</div>
      <div style="font-size:.6rem;font-weight:700;text-transform:uppercase;
                  letter-spacing:.04em;color:var(--muted)" id="gpsSatLabel">--</div>
    </div>
    <div class="lora-info">
      <div class="lora-rssi" id="gpsCoords">--</div>
      <div class="lora-label" id="gpsStatus">In attesa fix...</div>
      <div class="lora-snr" id="gpsDetail"></div>
      <div style="display:flex;align-items:center;gap:8px;margin-top:6px">
        <span class="lora-mesh" id="gpsTime">--:--:--</span>
        <div class="tog" id="gpsTog" onclick="toggleGps()" style="flex:none">
          <div class="tog-k"></div>
        </div>
      </div>
    </div>
  </div>
</div>

<!-- THERMAL MANAGER -->
<div class="sec">Temperatura</div>
<div class="card">
  <div class="lora-row">
    <div class="led-bulb pat-off" id="thermBulb" style="width:26px;height:26px"></div>
    <div class="lora-info">
      <div class="lora-rssi" id="thermTemp">--°C</div>
      <div class="lora-label" id="thermState">--</div>
      <div class="lora-snr" id="thermDetail"></div>
    </div>
  </div>
</div>

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
    <div class="sys-lbl">Client Wi-Fi</div>
    <div class="sys-right" id="cliInfo">--</div>
  </div>
</div>

<footer id="ft">
  Sensori: 2&nbsp;s &middot; Grafico: 30&nbsp;s &middot; LoRa: 5&nbsp;s &middot; GPS: 3&nbsp;s<br>
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

// ── Aggiornamento dati (ogni 2 s) ────────────────────────────────────────────
async function refreshData() {
  var d;
  try { d = await fetch('/data').then(function(r){return r.json()}); }
  catch(e) { return; }

  // Touch
  var tv=document.getElementById('tVal'), ts=document.getElementById('tSub'), tr=document.getElementById('tRaw');
  tv.textContent = (typeof d.touchFilt === 'number') ? Math.round(d.touchFilt) : d.touch;
  tv.className = 'sensor-val' + (d.touchActive ? ' c-ok' : '');
  ts.textContent = d.touchActive ? 'Attivo' : 'Libero';
  ts.className = 'sensor-sub' + (d.touchActive ? ' c-ok' : '');
  tr.textContent = 'raw: ' + d.touch;

  // Hall
  var hv=document.getElementById('hVal'), hs=document.getElementById('hSub'), hr=document.getElementById('hRaw');
  hv.textContent = (typeof d.hallFilt === 'number') ? d.hallFilt.toFixed(1) : d.hall;
  hv.className = 'sensor-val' + (d.magnet ? ' c-warn' : '');
  hs.textContent = d.magnet ? 'Magnete!' : 'Assente';
  hs.className = 'sensor-sub' + (d.magnet ? ' c-warn' : '');
  hr.textContent = 'raw: ' + d.hall;

  // LED widget
  var patMap = {
    'off':  { cls:'pat-off',   name:'LED spento' },
    'dim':  { cls:'pat-dim',   name:'Dim \u2014 bat. bassa' },
    'fade': { cls:'pat-fade',  name:'Fade \u2014 WiFi' },
    'full': { cls:'pat-solid', name:'Solid \u2014 carico \u2713' },
    'puls': { cls:'pat-pulse', name:'Pulse \u2014 in carica' }
  };
  var pm = patMap[d.ledPat] || { cls:'pat-off', name:d.ledPat };
  document.getElementById('ledBulb').className = 'led-bulb ' + pm.cls;
  document.getElementById('ledPatName').textContent = pm.name;

  // Pulsante fisico BOOT
  var dot = document.getElementById('btnDot');
  var lbl = document.getElementById('btnLabel');
  dot.classList.toggle('pressed', d.button);
  lbl.textContent = d.button ? 'Tasto premuto' : 'Tasto rilasciato';

  // Pulsanti override LED
  var ov = d.ledOverride;
  document.getElementById('ledBtnAuto').classList.toggle('active', ov === 0);
  document.getElementById('ledBtnOff').classList.toggle('active',  ov === 1);
  document.getElementById('ledBtnOn').classList.toggle('active',   ov === 2);

  // Batteria
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
    st.textContent = '\u26a0\ufe0f Batteria bassa \u2014 deep sleep imminente';
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
    et.textContent = '\u00a0';
  }

  // LoRa (aggiornamento leggero)
  updateLoraUI(d.loraRssi, d.loraSnr, d.loraLabel, d.loraReady);

  // OLED toggle
  oledOn = d.oled;
  document.getElementById('oTog').classList.toggle('on', d.oled);

  // Client WiFi
  var n = d.clients;
  document.getElementById('cliInfo').textContent =
    n === 0 ? 'Nessun client' : n === 1 ? '1 client connesso' : n+' client connessi';

  document.getElementById('ipFt').textContent = 'AP: '+d.ip;
}

// ── LoRa UI ──────────────────────────────────────────────────────────────────
function updateLoraUI(rssi, snr, label, ready) {
  var bars=document.getElementById('loraBars'),
      rVal=document.getElementById('loraRssiVal'),
      lbl=document.getElementById('loraLabel'),
      snrEl=document.getElementById('loraSnrVal'),
      mesh=document.getElementById('loraMesh');
  if (!ready) {
    bars.className='lora-bars none'; rVal.textContent='N/D';
    lbl.textContent='Modulo LoRa non disponibile'; snrEl.textContent='';
    mesh.textContent='Meshtastic: non attivo'; return;
  }
  if (!rssi || rssi === 0) {
    bars.className='lora-bars none'; rVal.textContent='---';
    lbl.textContent='In ascolto...'; snrEl.textContent='';
    mesh.textContent='Meshtastic: stub attivo'; return;
  }
  rVal.textContent = rssi+' dBm';
  var cls = rssi >= -90 ? 'good' : rssi >= -110 ? 'fair' : rssi > -120 ? 'weak' : 'none';
  bars.className = 'lora-bars '+cls;
  lbl.textContent = label || '---';
  snrEl.textContent = (snr && snr !== 0) ? 'SNR: '+snr.toFixed(1)+' dB' : '';
  mesh.textContent = 'Meshtastic: stub attivo';
}

async function refreshLora() {
  var d;
  try { d = await fetch('/lora').then(function(r){return r.json()}); }
  catch(e) { return; }
  updateLoraUI(d.rssi, d.snr, d.label, d.ready);
}

// ── Grafico canvas ────────────────────────────────────────────────────────────
async function refreshHistory() {
  var d;
  try { d = await fetch('/battery').then(function(r){return r.json()}); }
  catch(e) { return; }
  histData=d.history||[]; histInt=d.interval||30;
  histLowPc=(typeof d.lowPct==='number')?d.lowPct:histLowPc;
  drawGraph();
}

function drawGraph() {
  var canvas=document.getElementById('g');
  var dpr=window.devicePixelRatio||1;
  var rect=canvas.getBoundingClientRect();
  var nW=Math.round(rect.width*dpr), nH=Math.round(rect.height*dpr);
  if(canvas.width!==nW||canvas.height!==nH){canvas.width=nW;canvas.height=nH;}
  var ctx=canvas.getContext('2d');
  ctx.setTransform(dpr,0,0,dpr,0,0);
  var W=rect.width, H=rect.height, pad={t:8,r:8,b:22,l:30};
  var gW=W-pad.l-pad.r, gH=H-pad.t-pad.b, n=histData.length;
  var dark=window.matchMedia('(prefers-color-scheme:dark)').matches;
  var cTxt=dark?'#8a96a3':'#7c8a98', cGrid=dark?'rgba(255,255,255,.055)':'rgba(0,0,0,.05)';
  var cLine=dark?'#5a9a9b':'#4f8a8b', cWarn=dark?'rgba(198,135,58,.5)':'rgba(198,135,58,.55)';
  var cFT=dark?'rgba(79,138,139,.32)':'rgba(79,138,139,.2)', cFB='rgba(79,138,139,0)';
  ctx.clearRect(0,0,W,H);
  ctx.font='9px -apple-system,system-ui,sans-serif';
  if(n<2){
    ctx.fillStyle=cTxt; ctx.textAlign='center';
    ctx.fillText('Raccogliendo dati\u2026',W/2,H/2-2);
    if(n===1)ctx.fillText('(1 campione su 480)',W/2,H/2+10); return;
  }
  var ix=function(i){return pad.l+(i/(n-1))*gW;};
  var py=function(p){return pad.t+(1-p/100)*gH;};
  ctx.lineWidth=1;
  [0,25,50,75,100].forEach(function(p){
    var y=py(p);
    ctx.beginPath();ctx.strokeStyle=cGrid;ctx.moveTo(pad.l,y);ctx.lineTo(pad.l+gW,y);ctx.stroke();
    ctx.fillStyle=cTxt;ctx.textAlign='right';ctx.fillText(p+'%',pad.l-3,y+3);
  });
  var ly=py(histLowPc);
  ctx.beginPath();ctx.strokeStyle=cWarn;ctx.lineWidth=1;ctx.setLineDash([4,3]);
  ctx.moveTo(pad.l,ly);ctx.lineTo(pad.l+gW,ly);ctx.stroke();ctx.setLineDash([]);
  var grad=ctx.createLinearGradient(0,pad.t,0,pad.t+gH);
  grad.addColorStop(0,cFT);grad.addColorStop(1,cFB);
  ctx.beginPath();
  for(var i=0;i<n;i++){var x=ix(i),y=py(histData[i]);i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);}
  ctx.lineTo(ix(n-1),pad.t+gH);ctx.lineTo(pad.l,pad.t+gH);ctx.closePath();
  ctx.fillStyle=grad;ctx.fill();
  ctx.beginPath();
  for(var i=0;i<n;i++){var x=ix(i),y=py(histData[i]);i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);}
  ctx.strokeStyle=cLine;ctx.lineWidth=2;ctx.lineJoin='round';ctx.lineCap='round';ctx.stroke();
  var lx=ix(n-1),lv=histData[n-1],ly2=py(lv);
  ctx.beginPath();ctx.arc(lx,ly2,3.5,0,Math.PI*2);ctx.fillStyle=cLine;ctx.fill();
  if(lx>pad.l+30){
    ctx.fillStyle=cLine;ctx.textAlign=lx>W-35?'right':'center';
    ctx.fillText(Math.round(lv)+'%',lx+(lx>W-35?-6:0),ly2-6);
  }
  var tot=(n-1)*histInt;
  ctx.fillStyle=cTxt;ctx.textAlign='center';
  [[0,'-'+fmtSec(tot)],[1/3,'-'+fmtSec(Math.round(tot*2/3))],
   [2/3,'-'+fmtSec(Math.round(tot/3))],[1,'Ora']].forEach(function(e){
    ctx.fillText(e[1],pad.l+e[0]*gW,H-4);
  });
}

// ── Toggle OLED ───────────────────────────────────────────────────────────────
async function toggleOled() {
  var next = !oledOn;
  try {
    await fetch('/oled?state='+(next?'on':'off'));
    oledOn = next;
    document.getElementById('oTog').classList.toggle('on', next);
  } catch(e) {}
}

// ── Override LED ──────────────────────────────────────────────────────────────
async function setLed(state) {
  try {
    await fetch('/led?state='+state);
    var ov = state==='auto' ? 0 : state==='off' ? 1 : 2;
    document.getElementById('ledBtnAuto').classList.toggle('active', ov===0);
    document.getElementById('ledBtnOff').classList.toggle('active',  ov===1);
    document.getElementById('ledBtnOn').classList.toggle('active',   ov===2);
    if (state==='off') {
      document.getElementById('ledBulb').className='led-bulb pat-off';
      document.getElementById('ledPatName').textContent='LED spento (manuale)';
    } else if (state==='on') {
      document.getElementById('ledBulb').className='led-bulb pat-solid';
      document.getElementById('ledPatName').textContent='LED acceso (manuale)';
    }
  } catch(e) {}
}

// ── GPS ───────────────────────────────────────────────────────────────────────
var gpsOn = true;

async function refreshGps() {
  var d;
  try { d = await fetch('/gps').then(function(r){return r.json()}); }
  catch(e) { return; }

  gpsOn = d.enabled;
  document.getElementById('gpsTog').classList.toggle('on', d.enabled);

  var icon  = document.getElementById('gpsIcon');
  var coord = document.getElementById('gpsCoords');
  var stat  = document.getElementById('gpsStatus');
  var det   = document.getElementById('gpsDetail');
  var satLb = document.getElementById('gpsSatLabel');
  var time  = document.getElementById('gpsTime');

  satLb.textContent = d.sats + ' sat';

  if (!d.enabled) {
    icon.textContent  = '\uD83D\uDEF0\uFE0F';
    coord.textContent = 'GPS in standby';
    stat.textContent  = 'Power Save Mode attivo';
    det.textContent   = '';
    time.textContent  = '--:--:--';
    return;
  }

  if (d.fix) {
    icon.textContent = '\uD83D\uDEF0\uFE0F';
    // Coordinate in formato decimale compatto
    coord.textContent = d.lat.toFixed(5) + '\u00b0 ' + d.lon.toFixed(5) + '\u00b0';
    stat.textContent  = (d.speed > 0.5 ? d.speed.toFixed(1)+' km/h \u2022 ' : '') +
                        'Alt: ' + d.alt.toFixed(0) + ' m';
    det.textContent   = 'HDOP: ' + d.hdop.toFixed(1) +
                        (d.age >= 0 ? ' \u2022 fix ' + d.age + 's fa' : '');
    time.textContent  = d.time || '--:--:--';
  } else {
    icon.textContent = '\uD83D\uDEF0';
    coord.textContent = 'Nessun fix';
    stat.textContent  = d.sats > 0
      ? 'Agganciati ' + d.sats + ' satelliti — attendo...'
      : 'Ricerca satelliti...';
    det.textContent   = '';
    time.textContent  = d.time || '--:--:--';
  }
}

async function toggleGps() {
  var next = !gpsOn;
  try {
    await fetch('/gpstog?state=' + (next ? 'on' : 'off'));
    gpsOn = next;
    document.getElementById('gpsTog').classList.toggle('on', next);
  } catch(e) {}
}

// ── Thermal Manager ──────────────────────────────────────────────────────────
async function refreshThermal() {
  var d;
  try { d = await fetch('/thermal').then(function(r){return r.json()}); }
  catch(e) { return; }

  var stateCls = {
    0: 'therm-normal',   1: 'therm-elevated',
    2: 'therm-warning',  3: 'therm-critical',
    4: 'therm-emergency'
  };
  var stateLabel = {
    0: 'Normale',        1: 'Elevata',
    2: 'Attenzione',     3: 'Critica',
    4: 'EMERGENZA'
  };

  var bulb = document.getElementById('thermBulb');
  bulb.className = 'led-bulb ' + (stateCls[d.stateNum] || 'pat-off');

  document.getElementById('thermTemp').textContent = d.espTemp.toFixed(1) + '\u00b0C';
  document.getElementById('thermState').textContent = stateLabel[d.stateNum] || d.state;

  var trendIcon = d.trend > 0 ? '\u25b2' : d.trend < 0 ? '\u25bc' : '\u2014';
  var detail = 'Min: ' + d.espMin.toFixed(1) + '\u00b0C \u2022 Max: ' + d.espMax.toFixed(1) + '\u00b0C \u2022 ' + trendIcon;
  if (d.loraTemp > -999) {
    detail += ' \u2022 LoRa: ' + d.loraTemp.toFixed(1) + '\u00b0C';
  }
  document.getElementById('thermDetail').textContent = detail;
}

// ── Avvio ─────────────────────────────────────────────────────────────────────
refreshData();
refreshHistory();
refreshLora();
refreshGps();
refreshThermal();
setInterval(refreshData,    2000);
setInterval(refreshHistory, 30000);
setInterval(refreshLora,    5000);
setInterval(refreshGps,     3000);  // GPS polling ogni 3s
setInterval(refreshThermal, 5000);  // Thermal polling ogni 5s
window.addEventListener('resize', function(){ if(histData.length>1) drawGraph(); });
</script>
</body>
</html>
)rawliteral";
