/* =============================================================================
   index_html.h  -  the dashboard page served by the Tester
   -----------------------------------------------------------------------------
   WHY THIS IS A SEPARATE .h FILE AND NOT INSIDE THE .ino
     The Arduino IDE pre-processes every .ino file to auto-generate function
     prototypes, and that scanner does not understand C++ raw string literals.
     It sees the JavaScript "function ..." lines inside the page and tries to turn
     them into C++ prototypes, which fails to compile with errors like
         error: 'function' does not name a type
     Plain .h files are handed straight to the compiler and are never scanned that
     way, so the page lives here. This is the normal way to embed a web page in an
     Arduino sketch. Keep this markup in sync with code/dashboard.html.
   ============================================================================= */
#ifndef INDEX_HTML_H
#define INDEX_HTML_H

#include <Arduino.h>

const char PAGE_INDEX[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>Smart Medical Device Tester</title>
<style>
  :root{
    --bg1:#0d1b2a; --bg2:#12263a; --card:#ffffff; --panel:#0f2135;
    --ink:#12222f; --muted:#6b7c8f; --line:#e3e9f0; --soft:#f3f7fb;
    --brand:#0b6fb8; --brand-d:#0a5a95; --teal:#0ea5a3;
    --green:#1f9d55; --red:#e23b3b; --amber:#e08a00; --grid:#eef3f8;
  }
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:"Segoe UI",Roboto,Arial,sans-serif;color:#e8f0f7;
       background:linear-gradient(160deg,var(--bg1),var(--bg2));min-height:100vh;padding:16px}
  .num{font-variant-numeric:tabular-nums}
  header{max-width:1040px;margin:0 auto 14px;display:flex;align-items:center;
         justify-content:space-between;flex-wrap:wrap;gap:10px}
  .brand{display:flex;align-items:center;gap:12px}
  .logo{width:44px;height:44px;border-radius:12px;flex:none;background:linear-gradient(135deg,var(--brand),var(--teal));
        display:flex;align-items:center;justify-content:center;color:#fff;font-size:22px;font-weight:700}
  header h1{font-size:18px;font-weight:700;letter-spacing:.2px;color:#fff}
  header .sub{color:#9db6cc;font-size:12px;margin-top:2px}
  .conn{display:flex;align-items:center;gap:8px;font-size:13px;color:#9db6cc}
  .dot{width:11px;height:11px;border-radius:50%;background:var(--red);transition:.3s}
  .dot.on{background:var(--green);box-shadow:0 0 8px var(--green)}
  main{max-width:1040px;margin:0 auto}
  .card{background:var(--card);color:var(--ink);border-radius:16px;padding:16px 18px;
        box-shadow:0 10px 26px rgba(0,0,0,.20);margin-bottom:14px}
  h2{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:1.2px;margin-bottom:12px;font-weight:700}

  /* hero: overall verdict + state */
  .hero{display:flex;gap:16px;flex-wrap:wrap;align-items:center;justify-content:space-between}
  .verdict{font-size:44px;font-weight:800;letter-spacing:1px;padding:10px 26px;border-radius:14px}
  .v-pass{background:#e6f6ec;color:var(--green)}
  .v-fail{background:#fdeaea;color:var(--red)}
  .v-run {background:#eaf3fb;color:var(--brand)}
  .v-na  {background:var(--soft);color:var(--muted)}
  .statebox{display:flex;gap:22px;flex-wrap:wrap}
  .statebox div span{font-size:24px;font-weight:800}
  .statebox div small{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:1px;display:block}

  /* controls */
  .btnrow{display:flex;gap:10px;flex-wrap:wrap}
  button{font-family:inherit;font-size:15px;font-weight:700;cursor:pointer;border-radius:11px;
         padding:12px 20px;border:1px solid var(--line);background:var(--soft);color:var(--ink);flex:1;min-width:110px}
  .b-start{background:linear-gradient(135deg,var(--brand),var(--brand-d));color:#fff;border:none}
  .b-stop {background:#fff;color:var(--red);border:1px solid #f2c9c9}
  .seg{display:flex;gap:8px;margin-top:12px}
  .seg button.active{background:linear-gradient(135deg,var(--teal),var(--brand));color:#fff;border:none}
  .seglabel{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:1.2px;margin:4px 0;font-weight:700}

  /* live readout */
  .grid{display:grid;grid-template-columns:repeat(2,1fr);gap:14px}
  @media(max-width:640px){.grid{grid-template-columns:1fr}}
  .read{display:flex;justify-content:space-between;align-items:flex-end}
  .read .big{font-size:40px;font-weight:800}
  .read .u{color:var(--muted);font-size:13px;font-weight:600}
  .read .gen{text-align:right;color:var(--muted)}
  .read .gen b{font-size:20px;color:var(--ink)}
  .badge{display:inline-flex;align-items:center;gap:8px;padding:8px 13px;border-radius:11px;font-weight:700;font-size:14px}
  .badge .bd{width:11px;height:11px;border-radius:50%}
  .lo-ok{background:#e6f6ec;color:var(--green)} .lo-ok .bd{background:var(--green)}
  .lo-off{background:#fdeaea;color:var(--red)}  .lo-off .bd{background:var(--red)}

  /* step table */
  table{width:100%;border-collapse:collapse;font-size:13px}
  th,td{padding:8px 6px;text-align:center;border-bottom:1px solid var(--line)}
  th{color:var(--muted);text-transform:uppercase;font-size:10px;letter-spacing:.6px}
  td.v-pass{color:var(--green);font-weight:700}
  td.v-fail{color:var(--red);font-weight:700}
  td.v-na{color:var(--muted)}
  tr.cur{background:#eaf3fb}

  /* calibration */
  .calhead{display:flex;gap:22px;flex-wrap:wrap;margin-bottom:10px}
  .calhead div span{font-size:22px;font-weight:800}
  .calhead div small{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:1px;display:block}
  canvas{display:block;width:100%;height:260px;background:var(--soft);border-radius:10px}
  .foot{color:#9db6cc;font-size:11px;text-align:center;max-width:1040px;margin:6px auto 0;line-height:1.6}
</style>
</head>
<body>
<header>
  <div class="brand">
    <div class="logo">ECG</div>
    <div>
      <h1>Smart Medical Device Tester</h1>
      <div class="sub">Automated ECG monitor test &amp; calibration bench &bull; proof-of-concept</div>
    </div>
  </div>
  <div class="conn"><span id="dot" class="dot"></span><span id="connText">Connecting&hellip;</span></div>
</header>

<main>
  <!-- HERO -->
  <section class="card hero">
    <div id="verdict" class="verdict v-na">--</div>
    <div class="statebox">
      <div><span id="stState">--</span><small>state</small></div>
      <div><span id="stMode">--</span><small>mode</small></div>
      <div><span id="stStep">--</span><small>step</small></div>
      <div><span id="stPF">0 / 0</span><small>pass / fail</small></div>
      <div><span id="stUp">0s</span><small>uptime</small></div>
    </div>
  </section>

  <!-- CONTROLS -->
  <section class="card">
    <h2>Controls</h2>
    <div class="btnrow">
      <button class="b-start" id="btnStart">&#9658; Start</button>
      <button class="b-stop"  id="btnStop">&#9632; Stop</button>
    </div>
    <div class="seglabel">Mode</div>
    <div class="seg">
      <button id="mTest">Test plan</button>
      <button id="mCal">Calibrate</button>
    </div>
  </section>

  <!-- LIVE READOUT -->
  <div class="grid">
    <section class="card">
      <h2>Heart rate</h2>
      <div class="read">
        <div><span class="big num" id="measBpm">0</span> <span class="u">BPM measured</span></div>
        <div class="gen">generated<br><b class="num" id="genBpm">0</b> BPM</div>
      </div>
    </section>
    <section class="card">
      <h2>Amplitude</h2>
      <div class="read">
        <div><span class="big num" id="measAmp">0.00</span> <span class="u">mV measured</span></div>
        <div class="gen">generated<br><b class="num" id="genAmp">0.00</b> mV</div>
      </div>
    </section>
  </div>

  <div class="grid">
    <section class="card">
      <h2>Lead status</h2>
      <div id="loBadge" class="badge lo-ok"><span class="bd"></span><span id="loText">Leads connected</span></div>
    </section>
    <section class="card">
      <h2>Current step</h2>
      <div class="read"><div><span class="big num" id="curStep">0</span> <span class="u">of <span id="curTot">0</span></span></div>
      <div class="gen"><b id="curVerdict">--</b></div></div>
    </section>
  </div>

  <!-- STEP TABLE -->
  <section class="card">
    <h2>Automated test plan</h2>
    <table>
      <thead><tr><th>#</th><th>Gen BPM</th><th>Gen mV</th><th>Meas BPM</th><th>Meas mV</th><th>Verdict</th></tr></thead>
      <tbody id="stepBody"></tbody>
    </table>
  </section>

  <!-- CALIBRATION -->
  <section class="card">
    <h2>Calibration curve (reference vs measured BPM)</h2>
    <div class="calhead">
      <div><span class="num" id="calGain">1.000</span><small>gain</small></div>
      <div><span class="num" id="calOffset">0.00</span><small>offset (BPM)</small></div>
      <div><span class="num" id="calErr">0.00</span><small>max error (BPM)</small></div>
    </div>
    <canvas id="calCanvas" width="1000" height="520"></canvas>
  </section>
</main>

<div class="foot">
  Student research prototype &mdash; proof-of-concept ECG tester with a self-built stand-in monitor.
  <b>Not a medical device</b>, not a certified analyzer, not for clinical use. Low-voltage bench signal only.
</div>

<script>
const API = (location.protocol === "file:") ? "http://192.168.4.1" : "";
const $ = (id) => document.getElementById(id);

function setConn(ok){
  $("dot").classList.toggle("on", ok);
  $("connText").textContent = ok ? "Connected to MedTester" : "Tester offline";
}
function fmtUp(s){ s=Math.floor(s); if(s<60)return s+"s"; const m=(s/60)|0,r=s%60;
  if(m<60)return m+"m "+r+"s"; const h=(m/60)|0; return h+"h "+(m%60)+"m"; }

async function poll(){
  try{
    const res = await fetch(API+"/data",{cache:"no-store"});
    if(!res.ok) throw 0;
    render(await res.json());
    setConn(true);
  }catch(e){ setConn(false); }
}

function render(d){
  // overall verdict box
  const vb = $("verdict"); const ov = d.overall || "--";
  vb.textContent = ov;
  vb.className = "verdict " + (ov==="PASS"?"v-pass":ov==="FAIL"||ov==="FAULT"?"v-fail":ov==="RUNNING"?"v-run":"v-na");

  $("stState").textContent = d.state || "--";
  $("stMode").textContent  = d.mode || "--";
  $("stStep").textContent  = (d.step||0)+"/"+(d.total_steps||0);
  $("stPF").textContent    = (d.pass_count||0)+" / "+(d.fail_count||0);
  $("stUp").textContent    = fmtUp(d.uptime_s||0);

  $("measBpm").textContent = d.meas_bpm||0;
  $("genBpm").textContent  = Math.round(d.gen_bpm||0);
  $("measAmp").textContent = (Number(d.meas_amp)||0).toFixed(2);
  $("genAmp").textContent  = (Number(d.gen_amp)||0).toFixed(2);

  const lo = $("loBadge");
  if(Number(d.leads_off)===1){ lo.className="badge lo-off"; $("loText").textContent="Leads OFF detected"; }
  else { lo.className="badge lo-ok"; $("loText").textContent="Leads connected"; }

  $("curStep").textContent = d.step||0;
  $("curTot").textContent  = d.total_steps||0;
  $("curVerdict").textContent = d.verdict||"--";

  // mode buttons active state
  $("mTest").classList.toggle("active", d.mode==="TEST");
  $("mCal").classList.toggle("active", d.mode==="CAL");

  // step table
  const tb = $("stepBody"); tb.innerHTML="";
  (d.steps||[]).forEach(s=>{
    const tr=document.createElement("tr");
    if(s.n===d.step && (d.state==="TESTING")) tr.className="cur";
    const vc = s.v==="PASS"?"v-pass":s.v==="FAIL"?"v-fail":"v-na";
    tr.innerHTML = "<td>"+s.n+"</td><td>"+Math.round(s.gb)+"</td><td>"+Number(s.ga).toFixed(1)+
      "</td><td>"+(s.v==="--"?"-":s.mb)+"</td><td>"+(s.v==="--"?"-":Number(s.ma).toFixed(2))+
      "</td><td class='"+vc+"'>"+s.v+"</td>";
    tb.appendChild(tr);
  });

  // calibration
  $("calGain").textContent   = (Number(d.cal_gain)||0).toFixed(3);
  $("calOffset").textContent = (Number(d.cal_offset)||0).toFixed(2);
  $("calErr").textContent    = (Number(d.cal_max_err)||0).toFixed(2);
  drawCal(d.cal||[], Number(d.cal_gain)||1, Number(d.cal_offset)||0);
}

function drawCal(pts, gain, offset){
  const cv=$("calCanvas"), ctx=cv.getContext("2d");
  const W=cv.width, H=cv.height; ctx.clearRect(0,0,W,H);
  const pad=54, x0=pad, y0=H-pad, x1=W-20, y1=20;
  const lo=0, hi=260;                   // BPM axis range for both axes
  const sx=(v)=>x0+(v-lo)/(hi-lo)*(x1-x0);
  const sy=(v)=>y0-(v-lo)/(hi-lo)*(y0-y1);

  // grid + ticks
  ctx.strokeStyle="#e6edf4"; ctx.fillStyle="#8aa0b4"; ctx.lineWidth=1;
  ctx.font="12px Segoe UI, sans-serif"; ctx.textAlign="center";
  for(let v=0; v<=260; v+=40){
    ctx.beginPath(); ctx.moveTo(sx(v),y0); ctx.lineTo(sx(v),y1); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(x0,sy(v)); ctx.lineTo(x1,sy(v)); ctx.stroke();
    ctx.fillText(v, sx(v), y0+18);
    ctx.textAlign="right"; ctx.fillText(v, x0-8, sy(v)+4); ctx.textAlign="center";
  }
  // axis labels
  ctx.fillStyle="#5a7186"; ctx.fillText("reference BPM (generated)",(x0+x1)/2,H-14);
  ctx.save(); ctx.translate(16,(y0+y1)/2); ctx.rotate(-Math.PI/2);
  ctx.fillText("measured BPM (DUT)",0,0); ctx.restore();

  // ideal y=x line (dashed grey)
  ctx.strokeStyle="#b9c6d3"; ctx.setLineDash([6,5]); ctx.beginPath();
  ctx.moveTo(sx(0),sy(0)); ctx.lineTo(sx(260),sy(260)); ctx.stroke(); ctx.setLineDash([]);

  // fitted line (blue): meas = gain*ref + offset
  ctx.strokeStyle="#0b6fb8"; ctx.lineWidth=2.4; ctx.beginPath();
  ctx.moveTo(sx(0), sy(gain*0+offset)); ctx.lineTo(sx(260), sy(gain*260+offset)); ctx.stroke();

  // points
  pts.forEach(p=>{
    if(!p.meas) return;                 // skip not-yet-measured points
    ctx.fillStyle="#0ea5a3"; ctx.beginPath(); ctx.arc(sx(p.ref),sy(p.meas),5,0,7); ctx.fill();
    ctx.strokeStyle="#0a5a95"; ctx.stroke();
  });

  // legend
  ctx.textAlign="left"; ctx.font="12px Segoe UI, sans-serif";
  ctx.fillStyle="#0ea5a3"; ctx.fillText("\u25CF measured points", x0+8, y1+6);
  ctx.fillStyle="#0b6fb8"; ctx.fillText("\u2014 fitted line",      x0+8, y1+24);
  ctx.fillStyle="#b9c6d3"; ctx.fillText("-- ideal (y = x)",        x0+8, y1+42);
}

async function cmd(p){ try{ await fetch(API+p,{cache:"no-store"});}catch(e){} setTimeout(poll,150); }
$("btnStart").addEventListener("click",()=>cmd("/start"));
$("btnStop").addEventListener("click", ()=>cmd("/stop"));
$("mTest").addEventListener("click",  ()=>cmd("/mode?m=test"));
$("mCal").addEventListener("click",   ()=>cmd("/mode?m=cal"));

poll(); setInterval(poll,1000);
</script>
</body>
</html>
)HTMLPAGE";

#endif  // INDEX_HTML_H