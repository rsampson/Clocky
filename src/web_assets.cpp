#include "web_assets.h"

// The entire web dashboard, stored as three PROGMEM string constants (raw
// HTML/CSS/JS text baked into flash rather than a filesystem) and served
// as-is by web_server.cpp's GET routes for "/", "/style.css", and "/app.js".
// PROGMEM keeps these off the small amount of RAM available on the ESP8266.
//
// This is the "wall clock" dashboard: an analog sweep-second face is the
// primary display, with device state and controls in a quiet right-hand
// column. It talks to exactly the same /api/* routes as the original
// dashboard — no firmware changes outside this file.

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Clocky</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="wrap">

<header>
  <nav>
    <span class="brand">Clocky</span>
    <a href="#" class="tab-btn on" data-tab="status">Status</a>
    <a href="#" class="tab-btn" data-tab="alarm">Alarm</a>
    <a href="#" class="tab-btn" data-tab="setup">Setup</a>
  </nav>
  <div class="conn"><span id="dot" class="dot off"></span><span id="conn" class="mono">connecting</span></div>
</header>

<section id="tab-status" class="body on">
  <div class="main">
    <svg viewBox="0 0 200 200" class="face big-face">
      <circle cx="100" cy="100" r="96" fill="#fff" stroke="#e1e5e2" stroke-width="1"></circle>
      <path class="minor" stroke="#c3ccc8" stroke-width="1.5" stroke-linecap="round"></path>
      <path class="major" stroke="#1a1f1c" stroke-width="3.2" stroke-linecap="round"></path>
      <line id="hh" x1="100" y1="100" x2="100" y2="48" stroke="#1a1f1c" stroke-width="7" stroke-linecap="round"></line>
      <line id="mh" x1="100" y1="100" x2="100" y2="24" stroke="#1a1f1c" stroke-width="4.2" stroke-linecap="round"></line>
      <line id="sh" x1="100" y1="118" x2="100" y2="20" stroke="#c0392b" stroke-width="1.5" stroke-linecap="round"></line>
      <circle cx="100" cy="100" r="5" fill="#1a1f1c"></circle>
      <circle cx="100" cy="100" r="1.8" fill="#fff"></circle>
    </svg>
    <div class="ctr">
      <div id="clockHM" class="huge">--:--</div>
      <div class="sub"><span id="dateLong">&mdash;</span> &middot; <span id="tzName" class="nb"></span></div>
    </div>
  </div>
  <aside>
    <div>
      <div class="lbl">Next alarm</div>
      <div id="nextAlarm" class="val">&mdash;</div>
      <div id="alarmDays" class="sub2"></div>
    </div>
    <div class="rule"></div>
    <div class="rows">
      <div class="row"><span class="k">Signal</span><span id="rssi" class="num">&mdash;</span></div>
      <div class="row"><span class="k">Network</span><span id="ssid" class="mono">&mdash;</span></div>
      <div class="row"><span class="k">Alarm</span><span id="alarmState">&mdash;</span></div>
    </div>
    <div class="rule"></div>
    <div>
      <div class="lblrow"><div class="lbl">Log</div><span class="mono live">live</span></div>
      <pre id="log"></pre>
    </div>
  </aside>
</section>

<section id="tab-alarm" class="body">
  <div class="main">
    <svg viewBox="0 0 200 200" class="face set-face">
      <circle cx="100" cy="100" r="96" fill="#fff" stroke="#e1e5e2" stroke-width="1"></circle>
      <path class="minor" stroke="#e1e5e2" stroke-width="1.5" stroke-linecap="round"></path>
      <path class="major" stroke="#c3ccc8" stroke-width="3.2" stroke-linecap="round"></path>
      <line id="ghh" x1="100" y1="100" x2="100" y2="52" stroke="#dfe4e2" stroke-width="6" stroke-linecap="round"></line>
      <line id="gmh" x1="100" y1="100" x2="100" y2="30" stroke="#dfe4e2" stroke-width="3.6" stroke-linecap="round"></line>
      <line id="ahh" x1="100" y1="100" x2="100" y2="48" stroke="#2e5d7d" stroke-width="7" stroke-linecap="round"></line>
      <line id="amh" x1="100" y1="100" x2="100" y2="24" stroke="#2e5d7d" stroke-width="4.2" stroke-linecap="round"></line>
      <circle id="acap" cx="100" cy="100" r="5" fill="#2e5d7d"></circle>
      <circle cx="100" cy="100" r="1.8" fill="#fff"></circle>
    </svg>
    <div class="steppers">
      <div class="stepper">
        <div class="lbl">Hour</div>
        <div id="ahStr" class="digits">--</div>
        <div class="pm"><button type="button" id="hDown">&minus;</button><button type="button" id="hUp">+</button></div>
      </div>
      <div class="colon">:</div>
      <div class="stepper">
        <div class="lbl">Minute</div>
        <div id="amStr" class="digits">--</div>
        <div class="pm"><button type="button" id="mDown">&minus;</button><button type="button" id="mUp">+</button></div>
      </div>
    </div>
    <div id="alarmClause" class="sub"></div>
  </div>
  <aside>
    <div class="switch-row">
      <div>
        <div class="lbl">Alarm</div>
        <div id="alarmStateLong" class="val-sm">&mdash;</div>
      </div>
      <div id="toggle" class="switch"><span class="knob"></span></div>
    </div>
    <div class="rule"></div>
    <div>
      <div class="lbl">Days</div>
      <div id="dayChips" class="chips"></div>
      <div id="alarmDays2" class="sub2"></div>
    </div>
    <div class="rule"></div>
    <div class="act">
      <button type="button" id="saveSchedule" class="btn primary">Save alarm time</button>
      <span id="saveStatus" class="ok"></span>
    </div>
    <div class="note">Stored to NVS, so it survives a reboot. The hourly chime is separate and always on.</div>
  </aside>
</section>

<section id="tab-setup" class="body">
  <div class="main setup">
    <div>
      <div class="lbl">WiFi credentials</div>
      <div class="sub prose">Currently joined to <span id="ssid2" class="mono ink">&mdash;</span>. Saving new credentials reconnects the device; if it fails it falls back to its own access point.</div>
    </div>
    <div class="form">
      <label><span class="flbl">SSID</span><input type="text" id="wifiSsid" maxlength="32"></label>
      <label><span class="flbl">Password</span><input type="password" id="wifiPass" maxlength="64" placeholder="&bull;&bull;&bull;&bull;&bull;&bull;&bull;&bull;"></label>
      <div class="act">
        <button type="button" id="saveWifi" class="btn primary">Save</button>
        <span id="wifiStatus" class="ok"></span>
      </div>
    </div>
    <div class="rule"></div>
    <div class="form">
      <div class="lbl">Time zone</div>
      <select id="timezone">
        <option value="AEST">Australia Eastern</option>
        <option value="MSK">Moscow</option>
        <option value="CE">Central European</option>
        <option value="GMT">GMT/British</option>
        <option value="UTC">Universal (UTC)</option>
        <option value="EST">US Eastern</option>
        <option value="CST">US Central</option>
        <option value="MST">US Mountain</option>
        <option value="AZT">Arizona</option>
        <option value="PST">US Pacific</option>
        <option value="BRT">Brazil (S&atilde;o Paulo)</option>
        <option value="SAST">South Africa</option>
        <option value="GST">Gulf (Dubai)</option>
        <option value="IST">India</option>
        <option value="CNST">China</option>
        <option value="JST">Japan</option>
      </select>
      <div class="note">Applied immediately &mdash; no save needed. DST is handled by the firmware's rules.</div>
    </div>
  </div>
  <aside>
    <div>
      <div class="lbl">Device</div>
      <div class="rows">
        <div class="row"><span class="k">Hostname</span><span class="mono">clocky.local</span></div>
        <div class="row"><span class="k">Signal</span><span id="rssi2" class="num">&mdash;</span></div>
        <div class="row"><span class="k">Clock source</span><span id="clockSrc">&mdash;</span></div>
      </div>
    </div>
    <div class="rule"></div>
    <div>
      <div class="lbl">Maintenance</div>
      <div class="stack">
        <a href="/update" class="btn">Firmware update</a>
        <button type="button" id="reboot" class="btn danger">Reboot</button>
      </div>
      <div id="rebootNote" class="note">Takes about 8 seconds. Settings are kept.</div>
    </div>
  </aside>
</section>

</div>
<script src="/app.js"></script>
</body>
</html>
)HTML";

const char STYLE_CSS[] PROGMEM = R"CSS(
*{box-sizing:border-box}
body{margin:0;background:#f2f4f3;color:#1a1f1c;font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
a{color:#2e5d7d;text-decoration:none}
.wrap{max-width:1040px;margin:0 auto;min-height:100vh}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
.nb{white-space:nowrap}

header{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:16px 22px;flex-wrap:wrap}
header nav{display:flex;gap:18px;align-items:baseline;flex-wrap:wrap}
.brand{font:700 15px/1 system-ui}
.tab-btn{font:400 12px/1 system-ui;color:#6b7570;padding-bottom:4px;border-bottom:2px solid transparent}
.tab-btn.on{color:#2e5d7d;border-bottom-color:#2e5d7d}
.conn{display:flex;align-items:center;gap:7px;font-size:11px;color:#6b7570;white-space:nowrap}
.dot{width:6px;height:6px;border-radius:50%;display:block;background:#2e7d5b}
.dot.off{background:#c0392b}

.body{display:none;grid-template-columns:minmax(0,1fr) 320px;border-top:1px solid #e1e5e2}
.body.on{display:grid}
.main{padding:34px 30px 38px;display:flex;flex-direction:column;align-items:center;gap:20px;border-right:1px solid #e1e5e2}
.main.setup{align-items:stretch;gap:26px;padding:32px 30px 38px}
aside{padding:26px 24px;display:flex;flex-direction:column;gap:22px}

.face{display:block;width:100%;height:auto}
.big-face{max-width:280px}
.set-face{max-width:246px}
.ctr{text-align:center}
.huge{font:300 44px/1 system-ui;letter-spacing:-.03em;font-variant-numeric:tabular-nums}
.sub{font:400 12.5px/1.5 system-ui;color:#6b7570;letter-spacing:.02em}
.ctr .sub{margin-top:10px}
.prose{max-width:46ch}
.ink{color:#1a1f1c;font-size:11.5px}
.lbl{font:600 10px/1 system-ui;color:#6b7570;text-transform:uppercase;letter-spacing:.09em;margin-bottom:9px}
.lblrow{display:flex;align-items:center;justify-content:space-between}
.lblrow .lbl{margin-bottom:9px}
.live{font-size:10.5px;color:#6b7570;margin-bottom:9px}
.val{font:500 26px/1.1 system-ui;letter-spacing:-.01em}
.val-sm{font:500 16px/1 system-ui}
.sub2{font:400 12px/1 system-ui;color:#6b7570;margin-top:8px}
.rule{height:1px;background:#e1e5e2}
.rows{display:flex;flex-direction:column;gap:11px}
.row{display:flex;justify-content:space-between;gap:12px;font:400 12.5px/1 system-ui}
.row .k{color:#6b7570;white-space:nowrap}
.num{font-variant-numeric:tabular-nums;white-space:nowrap}
.row .mono{font-size:11.5px}
pre#log{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:10.5px;line-height:1.7;white-space:pre-wrap;word-break:break-word;max-height:200px;overflow-y:auto;margin:0;color:#6b7570}

.steppers{display:flex;align-items:flex-start;gap:22px}
.stepper{display:flex;flex-direction:column;align-items:center;gap:10px}
.stepper .lbl{margin:0}
.digits{font:300 56px/1 system-ui;letter-spacing:-.03em;font-variant-numeric:tabular-nums}
.colon{font:300 56px/1 system-ui;color:#c3ccc8;padding-top:26px}
.pm{display:flex;gap:6px}
.pm button{width:36px;height:32px;border:1px solid #e1e5e2;background:#fff;color:#1a1f1c;border-radius:8px;font:400 15px/1 system-ui;cursor:pointer}
.pm button:hover{border-color:#c3ccc8}

.switch-row{display:flex;align-items:center;justify-content:space-between;gap:14px}
.switch-row .lbl{margin-bottom:8px}
.switch{position:relative;width:48px;height:28px;border-radius:28px;flex:none;cursor:pointer;background:#e1e5e2;transition:background .15s}
.switch.on{background:#2e5d7d}
.knob{position:absolute;top:3px;left:3px;width:22px;height:22px;border-radius:50%;background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.18);transition:transform .15s;display:block}
.switch.on .knob{transform:translateX(20px)}

.chips{display:flex;flex-wrap:wrap;gap:6px}
.chips button{border-radius:8px;padding:8px 10px;font:500 12px/1 system-ui;cursor:pointer;border:1px solid #e1e5e2;background:#fff;color:#1a1f1c}
.chips button.on{background:#2e5d7d;color:#fff;border-color:#2e5d7d}
.chips+.sub2{margin-top:12px}

.form{display:flex;flex-direction:column;gap:14px;max-width:420px}
.form label{display:flex;flex-direction:column;gap:7px}
.flbl{font:400 12px/1 system-ui;color:#6b7570}
input,select{padding:10px 12px;border:1px solid #e1e5e2;border-radius:8px;background:#fff;color:#1a1f1c;font:400 13.5px/1.2 system-ui;width:100%}
input:focus,select:focus{outline:none;border-color:#2e5d7d}
.act{display:flex;align-items:center;gap:12px}
.ok{font:400 12px/1 system-ui;color:#2e7d5b}
.note{font:400 11.5px/1.6 system-ui;color:#6b7570;margin-top:10px}
.stack{display:flex;flex-direction:column;gap:9px;align-items:flex-start}
.btn{display:inline-block;background:#fff;color:#1a1f1c;border:1px solid #e1e5e2;border-radius:8px;padding:10px 16px;font:500 13px/1 system-ui;cursor:pointer;white-space:nowrap;flex:none}
.btn:hover{border-color:#c3ccc8}
.btn.primary{background:#2e5d7d;color:#fff;border-color:#2e5d7d}
.btn.primary:hover{background:#27506c;border-color:#27506c}
.btn.danger{color:#c0392b;border-color:#eccfcb}
.btn.danger:hover{background:#c0392b;color:#fff;border-color:#c0392b}

@media (max-width:760px){
  .body.on{grid-template-columns:minmax(0,1fr)}
  .main{border-right:0;border-bottom:1px solid #e1e5e2;padding:26px 20px 30px}
  aside{padding:22px 20px 28px}
  header nav{gap:14px}
}
)CSS";

const char APP_JS[] PROGMEM = R"JS(
// Wall-clock dashboard. No framework, no build step.
//
// Two independent loops:
//   - the network loop polls GET /api/status once a second and applies the
//     snapshot to the page (refresh(), applyStatus()),
//   - the render loop runs on requestAnimationFrame and moves the clock hands
//     from a locally extrapolated clock, so the second hand sweeps smoothly
//     between polls instead of jumping once a second.
(() => {
  const $ = (id) => document.getElementById(id);
  const DAY = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const DAYFULL = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const MONTH = ['January', 'February', 'March', 'April', 'May', 'June',
                 'July', 'August', 'September', 'October', 'November', 'December'];
  const p2 = (n) => String(n).padStart(2, '0');

  // --- device clock, extrapolated between polls -----------------------------
  // baseSec is the device's second-of-day from the last /api/status; baseAt is
  // the performance.now() reading at the moment we received it. deviceSec()
  // adds the elapsed wall time since, so the hands keep moving (and the second
  // hand keeps sweeping) even though the device is only polled at 1Hz.
  let baseSec = null, baseAt = 0;
  let weekday = 0, month = 1, mday = 1;   // from the last status; refreshed each poll
  let haveStatus = false;

  function deviceSec() {
    if (baseSec === null) return null;
    return baseSec + (performance.now() - baseAt) / 1000;
  }

  // --- local (unsaved) alarm edit state -------------------------------------
  // The day chips and hour/minute steppers write here and stay local until
  // "Save alarm time" is pressed; `dirty` stops the 1s poll from reverting an
  // in-progress edit. The on/off switch is not part of this — it POSTs
  // immediately, so it is always read straight from the server.
  let ah = 7, am = 30, days = 0x7F, alarmOn = true;
  let seeded = false, dirty = false;
  let apMode = false, timeSynced = false;

  // --- static geometry ------------------------------------------------------
  // Both faces share the same tick ring; built once here and written into
  // every .minor/.major path in the document.
  (function buildTicks() {
    const minor = [], major = [];
    for (let i = 0; i < 60; i++) {
      const a = (i * 6 - 90) * Math.PI / 180;
      const isMajor = i % 5 === 0;
      const r1 = isMajor ? 74 : 80;
      const x1 = (100 + Math.cos(a) * r1).toFixed(2), y1 = (100 + Math.sin(a) * r1).toFixed(2);
      const x2 = (100 + Math.cos(a) * 88).toFixed(2), y2 = (100 + Math.sin(a) * 88).toFixed(2);
      (isMajor ? major : minor).push('M' + x1 + ' ' + y1 + 'L' + x2 + ' ' + y2);
    }
    document.querySelectorAll('.minor').forEach((el) => el.setAttribute('d', minor.join('')));
    document.querySelectorAll('.major').forEach((el) => el.setAttribute('d', major.join('')));
  })();

  const rot = (el, deg) => el.setAttribute('transform', 'rotate(' + deg.toFixed(3) + ' 100 100)');

  // --- tabs -----------------------------------------------------------------
  document.querySelectorAll('.tab-btn').forEach((btn) => {
    btn.addEventListener('click', (e) => {
      e.preventDefault();
      document.querySelectorAll('.tab-btn').forEach((b) => b.classList.remove('on'));
      document.querySelectorAll('.body').forEach((s) => s.classList.remove('on'));
      btn.classList.add('on');
      $('tab-' + btn.dataset.tab).classList.add('on');
    });
  });

  // --- text helpers ---------------------------------------------------------
  function daysPhrase() {
    if (!alarmOn) return 'Alarm off';
    const on = DAY.filter((_, i) => days & (1 << i));
    if (on.length === 7) return 'Every day';
    if (on.length === 0) return 'No days selected';
    if (on.length === 5 && !(days & 1) && !(days & 64)) return 'Weekdays';
    return on.join(', ');
  }

  // Port of describeNextAlarm() from the original dashboard: walks forward from
  // today looking for the next active day, skipping today if its alarm time
  // has already gone by. offset runs to 8 so that with every day active,
  // today-already-passed still resolves to next week's same weekday.
  function describeNextAlarm() {
    if (!alarmOn || !(days & 0x7F) || baseSec === null) return 'Off';
    const nowMin = Math.floor(deviceSec() / 60) % 1440;
    for (let offset = 0; offset < 8; offset++) {
      const day = (weekday - 1 + offset) % 7;   // status.weekday is 1=Sun..7=Sat
      if (!(days & (1 << day))) continue;
      if (offset === 0 && ah * 60 + am <= nowMin) continue;
      const h12 = ah % 12 === 0 ? 12 : ah % 12;
      const label = offset === 0 ? 'Today' : offset === 1 ? 'Tomorrow' : DAY[day];
      return label + ' ' + h12 + ':' + p2(am) + ' ' + (ah < 12 ? 'AM' : 'PM');
    }
    return 'Off';
  }

  // Everything that depends on the local alarm edit state, in one place so the
  // steppers, chips and switch can all just call this after mutating it.
  function renderAlarm() {
    $('ahStr').textContent = p2(ah);
    $('amStr').textContent = p2(am);
    rot($('ahh'), (ah % 12) * 30 + am * 0.5);
    rot($('amh'), am * 6);
    const c = alarmOn ? '#2e5d7d' : '#c3ccc8';
    $('ahh').setAttribute('stroke', c);
    $('amh').setAttribute('stroke', c);
    $('acap').setAttribute('fill', c);

    const phrase = daysPhrase();
    $('alarmClause').textContent = alarmOn
      ? 'Rings ' + phrase.toLowerCase() + ' at ' + p2(ah) + ':' + p2(am)
      : 'Alarm is muted';
    $('alarmDays').textContent = phrase;
    $('alarmDays2').textContent = phrase;
    $('nextAlarm').textContent = describeNextAlarm();
    $('alarmState').textContent = alarmOn ? 'On' : 'Off';
    $('alarmStateLong').textContent = alarmOn ? 'Alarm on' : 'Alarm off';
    $('toggle').classList.toggle('on', alarmOn);
    document.querySelectorAll('#dayChips button').forEach((b, i) => {
      b.classList.toggle('on', !!(days & (1 << i)));
    });
  }

  // --- render loop ----------------------------------------------------------
  function tick() {
    const t = deviceSec();
    if (t !== null) {
      const sec = t % 60, mins = Math.floor(t / 60) % 60, hrs = Math.floor(t / 3600) % 24;
      rot($('hh'), (hrs % 12) * 30 + mins * 0.5);
      rot($('mh'), mins * 6 + sec * 0.1);
      rot($('sh'), sec * 6);
      $('clockHM').textContent = p2(hrs) + ':' + p2(mins);
      // Ghosted "current time" hands on the alarm-setting face
      rot($('ghh'), (hrs % 12) * 30 + mins * 0.5);
      rot($('gmh'), mins * 6);
    }
    requestAnimationFrame(tick);
  }
  requestAnimationFrame(tick);

  // --- day chips ------------------------------------------------------------
  DAY.forEach((name, i) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.textContent = name;
    b.addEventListener('click', () => { days ^= (1 << i); dirty = true; renderAlarm(); });
    $('dayChips').appendChild(b);
  });

  // --- status polling -------------------------------------------------------
  function post(url, body) {
    return fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
  }

  // Gives the device a rough clock when it has no internet (AP mode, no NTP).
  // One-shot per page load; ignored by the firmware once on real Wi-Fi.
  function syncBrowserTime() {
    if (timeSynced) return;
    timeSynced = true;
    post('/api/settime', { epoch: Math.floor(Date.now() / 1000) });
  }

  function applyStatus(s) {
    $('conn').textContent = 'online \u00b7 ' + s.rssi + ' dBm';
    $('dot').className = 'dot';

    // s.time is "HH:MM:SS MM/DD"
    const hh = +s.time.slice(0, 2), mm = +s.time.slice(3, 5), ss = +s.time.slice(6, 8);
    baseSec = hh * 3600 + mm * 60 + ss;
    baseAt = performance.now();
    weekday = s.weekday;
    month = +s.time.slice(9, 11);
    mday = +s.time.slice(12, 14);

    $('dateLong').textContent = DAYFULL[(s.weekday - 1) % 7] + ', ' + MONTH[month - 1] + ' ' + mday;
    $('tzName').textContent = s.timezone;
    $('rssi').textContent = s.rssi + ' dBm';
    $('rssi2').textContent = s.rssi + ' dBm';
    $('ssid').textContent = s.ssid || '\u2014';
    $('ssid2').textContent = s.ssid || '\u2014';
    $('clockSrc').textContent = s.apMode ? 'Browser (AP mode)' : 'NTP';
    $('log').textContent = s.log;
    $('wifiSsid').placeholder = s.ssid || '';
    if (s.timezoneCode) $('timezone').value = s.timezoneCode;

    // The switch POSTs immediately, so the server is always authoritative for
    // it. Hour/minute/days are seeded once and then owned by the UI until Save.
    alarmOn = !s.disabled;
    if (!seeded || !dirty) {
      ah = s.runHour;
      am = s.runMinute;
      days = s.activeDays;
      seeded = true;
    }
    renderAlarm();

    apMode = s.apMode;
    if (apMode) syncBrowserTime();
    haveStatus = true;
  }

  function refresh() {
    fetch('/api/status')
      .then((r) => r.json())
      .then(applyStatus)
      .catch(() => {
        $('conn').textContent = 'offline';
        $('dot').className = 'dot off';
      });
  }

  // --- controls -------------------------------------------------------------
  function flash(id, text) {
    const el = $(id);
    el.textContent = text;
    clearTimeout(el._t);
    el._t = setTimeout(() => { el.textContent = ''; }, 2000);
  }

  const wrap = (v, max) => (v + max + 1) % (max + 1);
  $('hUp').addEventListener('click', () => { ah = wrap(ah + 1, 23); dirty = true; renderAlarm(); });
  $('hDown').addEventListener('click', () => { ah = wrap(ah - 1, 23); dirty = true; renderAlarm(); });
  $('mUp').addEventListener('click', () => { am = wrap(am + 1, 59); dirty = true; renderAlarm(); });
  $('mDown').addEventListener('click', () => { am = wrap(am - 1, 59); dirty = true; renderAlarm(); });

  $('toggle').addEventListener('click', () => {
    alarmOn = !alarmOn;
    renderAlarm();
    post('/api/alarm', { disable: !alarmOn }).then(refresh);
  });

  $('saveSchedule').addEventListener('click', () => {
    post('/api/schedule', { hour: ah, minute: am, activeDays: days }).then(() => {
      dirty = false;   // safe to let polling own these fields again
      flash('saveStatus', 'Saved');
      refresh();
    });
  });

  $('saveWifi').addEventListener('click', () => {
    post('/api/wifi', { ssid: $('wifiSsid').value, pass: $('wifiPass').value })
      .then(() => flash('wifiStatus', 'Saved'));
  });

  $('timezone').addEventListener('change', (e) => {
    post('/api/timezone', { tz: e.target.value }).then(refresh);
  });

  $('reboot').addEventListener('click', () => {
    if (!confirm('Reboot Clocky now?')) return;
    fetch('/api/reboot', { method: 'POST' });
    $('rebootNote').textContent = 'Rebooting\u2026 the page will go offline for a few seconds.';
    setTimeout(() => { $('rebootNote').textContent = 'Takes about 8 seconds. Settings are kept.'; }, 12000);
  });

  renderAlarm();
  refresh();
  setInterval(refresh, 1000);
})();
)JS";
