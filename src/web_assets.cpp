#include "web_assets.h"

// The entire web dashboard, stored as three PROGMEM string constants (raw
// HTML/CSS/JS text baked into flash rather than a filesystem) and served
// as-is by web_server.cpp's GET routes for "/", "/style.css", and "/app.js".
// PROGMEM keeps these off the small amount of RAM available on the ESP8266.
//
// This is the "travel alarm clock" dashboard: split-flap style digits are
// the primary display, warm cream-and-brass styling stands in for the clock's
// case, and the Alarm tab carries a small twin-bell motif. It talks to
// exactly the same /api/* routes as the original dashboard — no firmware
// changes outside this file.

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Clocky</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="case">

<header>
  <div class="brand">Clocky</div>
  <nav>
    <a href="#" class="tab-btn on" data-tab="status">Status</a>
    <a href="#" class="tab-btn" data-tab="alarm">Alarm</a>
    <a href="#" class="tab-btn" data-tab="setup">Setup</a>
  </nav>
  <div class="conn"><span id="dot" class="dot off"></span><span id="conn" class="mono">connecting</span></div>
</header>

<section id="tab-status" class="body on">
  <div class="main">
    <div class="flipclock">
      <div class="flipgroup"><span id="h0" class="flip">-</span><span id="h1" class="flip">-</span></div>
      <div class="colon"><i></i><i></i></div>
      <div class="flipgroup"><span id="m0" class="flip">-</span><span id="m1" class="flip">-</span></div>
      <div class="flipgroup small"><span id="s0" class="flip sm">-</span><span id="s1" class="flip sm">-</span></div>
    </div>
    <div class="ctr">
      <div id="dateLong" class="date">&mdash;</div>
      <div id="tzName" class="tz">&mdash;</div>
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
    <svg class="bell" viewBox="0 0 100 60" aria-hidden="true">
      <circle cx="28" cy="34" r="22" fill="none" stroke="currentColor" stroke-width="4"></circle>
      <circle cx="72" cy="34" r="22" fill="none" stroke="currentColor" stroke-width="4"></circle>
      <circle cx="50" cy="9" r="5" fill="currentColor"></circle>
      <line x1="50" y1="14" x2="50" y2="23" stroke="currentColor" stroke-width="4" stroke-linecap="round"></line>
    </svg>
    <div class="flipclock">
      <div class="flipgroup"><span id="ah0" class="flip">-</span><span id="ah1" class="flip">-</span></div>
      <div class="colon static"><i></i><i></i></div>
      <div class="flipgroup"><span id="am0" class="flip">-</span><span id="am1" class="flip">-</span></div>
    </div>
    <div class="steppers">
      <div class="stepgrp"><button type="button" id="hDown" class="knob">&minus;</button><span class="steplbl">Hour</span><button type="button" id="hUp" class="knob">+</button></div>
      <div class="stepgrp"><button type="button" id="mDown" class="knob">&minus;</button><span class="steplbl">Minute</span><button type="button" id="mUp" class="knob">+</button></div>
    </div>
    <div id="alarmClause" class="date"></div>
  </div>
  <aside>
    <div class="switch-row">
      <div>
        <div class="lbl">Alarm</div>
        <div id="alarmStateLong" class="val-sm">&mdash;</div>
      </div>
      <div id="toggle" class="switch"><span class="knobdot"></span></div>
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
:root{
  --cream-bg:#ede0c8;
  --case:#fbf4e7;
  --ink:#3a2a1e;
  --ink-soft:#8a7255;
  --brass:#b8863b;
  --brass-deep:#8f6526;
  --line:#e3d2ae;
  --red:#b1442e;
  --red-soft:#e7c9b9;
  --olive:#6e7b4a;
}
*{box-sizing:border-box}
body{margin:0;background:var(--cream-bg);color:var(--ink);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
a{color:var(--brass-deep);text-decoration:none}
.case{max-width:1040px;margin:24px auto;min-height:calc(100vh - 48px);background:var(--case);border:1px solid var(--line);border-top:4px solid var(--brass);border-radius:18px;box-shadow:0 24px 48px -28px rgba(58,42,30,.35);overflow:hidden}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
.nb{white-space:nowrap}

header{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:18px 24px;flex-wrap:wrap;border-bottom:1px solid var(--line)}
.brand{font:700 15px/1 system-ui;color:var(--brass-deep);letter-spacing:.01em}
header nav{display:flex;gap:18px;align-items:baseline;flex-wrap:wrap}
.tab-btn{font:500 12.5px/1 system-ui;color:var(--ink-soft);padding-bottom:4px;border-bottom:2px solid transparent}
.tab-btn.on{color:var(--brass-deep);border-bottom-color:var(--brass)}
.conn{display:flex;align-items:center;gap:7px;font-size:11px;color:var(--ink-soft);white-space:nowrap}
.dot{width:6px;height:6px;border-radius:50%;display:block;background:var(--olive)}
.dot.off{background:var(--red)}

.body{display:none;grid-template-columns:minmax(0,1fr) 320px}
.body.on{display:grid;border-top:1px solid var(--line)}
#tab-status.body.on{border-top:none}
.main{padding:40px 30px 38px;display:flex;flex-direction:column;align-items:center;gap:20px;border-right:1px solid var(--line)}
.main.setup{align-items:stretch;gap:26px;padding:32px 30px 38px}
aside{padding:26px 24px;display:flex;flex-direction:column;gap:22px}

.bell{width:60px;height:36px;color:var(--brass);margin-bottom:4px}

.flipclock{display:flex;align-items:flex-end;justify-content:center}
.flipgroup{display:flex}
.flipgroup.small{margin-left:14px;margin-bottom:3px;opacity:.8}
.flip{display:inline-flex;align-items:center;justify-content:center;width:60px;height:84px;margin:0 3px;
  background:linear-gradient(var(--case) 50%,#f3e8d2 50%);color:var(--ink);
  font:700 50px/1 ui-monospace,SFMono-Regular,"Courier New",monospace;font-variant-numeric:tabular-nums;
  border-radius:10px;border:1px solid var(--line);position:relative;
  box-shadow:0 2px 0 rgba(58,42,30,.08),inset 0 0 0 1px rgba(255,255,255,.5)}
.flip::after{content:"";position:absolute;left:6px;right:6px;top:50%;height:1px;background:rgba(58,42,30,.22)}
.flip.sm{width:32px;height:44px;font-size:24px;border-radius:7px}
.flip.dim{color:var(--ink-soft)}
.colon{display:flex;flex-direction:column;justify-content:center;gap:9px;margin:0 8px 0;align-self:center;padding-bottom:2px}
.colon i{width:8px;height:8px;border-radius:50%;background:var(--brass);display:block;animation:blink 2s steps(1) infinite}
.colon.static i{animation:none}
@keyframes blink{0%,49%{opacity:1}50%,100%{opacity:.25}}
@media (prefers-reduced-motion:reduce){.colon i{animation:none}}

.ctr{margin-top:6px;text-align:center}
.date{font:600 15px/1.3 system-ui}
.tz{font:400 12px/1.3 system-ui;color:var(--ink-soft);margin-top:2px}
.prose{font:400 12.5px/1.5 system-ui;color:var(--ink-soft);max-width:46ch}
.ink{color:var(--ink);font-size:11.5px}
.lbl{font:600 11px/1 system-ui;color:var(--brass-deep);letter-spacing:.01em;margin-bottom:9px}
.lblrow{display:flex;align-items:center;justify-content:space-between}
.lblrow .lbl{margin-bottom:9px}
.live{font-size:10.5px;color:var(--ink-soft);margin-bottom:9px}
.val{font:600 26px/1.1 system-ui;letter-spacing:-.01em}
.val-sm{font:600 16px/1 system-ui}
.sub2{font:400 12px/1 system-ui;color:var(--ink-soft);margin-top:8px}
.rule{height:1px;background:var(--line)}
.rows{display:flex;flex-direction:column;gap:11px}
.row{display:flex;justify-content:space-between;gap:12px;font:400 12.5px/1 system-ui}
.row .k{color:var(--ink-soft);white-space:nowrap}
.num{font-variant-numeric:tabular-nums;white-space:nowrap}
.row .mono{font-size:11.5px}
pre#log{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:10.5px;line-height:1.7;white-space:pre-wrap;word-break:break-word;max-height:200px;overflow-y:auto;margin:0;color:var(--ink-soft)}

.steppers{display:flex;gap:34px;margin-top:8px}
.stepgrp{display:flex;align-items:center;gap:10px}
.steplbl{font:600 11px/1 system-ui;color:var(--ink-soft);min-width:44px;text-align:center}
.knob{width:34px;height:34px;border-radius:50%;border:1px solid var(--brass);background:var(--case);color:var(--brass-deep);font:400 16px/1 system-ui;cursor:pointer}
.knob:hover{background:var(--brass);color:#fff}

.switch-row{display:flex;align-items:center;justify-content:space-between;gap:14px}
.switch-row .lbl{margin-bottom:8px}
.switch{position:relative;width:52px;height:30px;border-radius:16px;flex:none;cursor:pointer;background:var(--line);transition:background .15s}
.switch.on{background:var(--olive)}
.knobdot{position:absolute;top:3px;left:3px;width:24px;height:24px;border-radius:50%;background:var(--case);box-shadow:0 1px 2px rgba(58,42,30,.3);transition:transform .15s;display:block}
.switch.on .knobdot{transform:translateX(22px)}

.chips{display:flex;flex-wrap:wrap;gap:7px}
.chips button{width:34px;height:34px;border-radius:50%;font:600 11px/1 system-ui;cursor:pointer;border:1px solid var(--line);background:var(--case);color:var(--ink)}
.chips button.on{background:var(--brass);color:#fff;border-color:var(--brass)}
.chips+.sub2{margin-top:12px}

.form{display:flex;flex-direction:column;gap:14px;max-width:420px}
.form label{display:flex;flex-direction:column;gap:7px}
.flbl{font:400 12px/1 system-ui;color:var(--ink-soft)}
input,select{padding:10px 12px;border:1px solid var(--line);border-radius:8px;background:var(--case);color:var(--ink);font:400 13.5px/1.2 system-ui;width:100%}
input:focus,select:focus{outline:none;border-color:var(--brass)}
.act{display:flex;align-items:center;gap:12px}
.ok{font:400 12px/1 system-ui;color:var(--olive)}
.note{font:400 11.5px/1.6 system-ui;color:var(--ink-soft);margin-top:10px}
.stack{display:flex;flex-direction:column;gap:9px;align-items:flex-start}
.btn{display:inline-block;background:var(--case);color:var(--ink);border:1px solid var(--line);border-radius:8px;padding:10px 16px;font:500 13px/1 system-ui;cursor:pointer;white-space:nowrap;flex:none}
.btn:hover{border-color:var(--brass)}
.btn.primary{background:var(--brass);color:#fff;border-color:var(--brass)}
.btn.primary:hover{background:var(--brass-deep);border-color:var(--brass-deep)}
.btn.danger{color:var(--red);border-color:var(--red-soft)}
.btn.danger:hover{background:var(--red);color:#fff;border-color:var(--red)}

@media (max-width:760px){
  .case{margin:0;border-radius:0;min-height:100vh}
  .body.on{grid-template-columns:minmax(0,1fr)}
  .main{border-right:0;border-bottom:1px solid var(--line);padding:28px 20px 30px}
  aside{padding:22px 20px 28px}
  header nav{gap:14px}
  .flip{width:48px;height:68px;font-size:38px}
  .flip.sm{width:26px;height:36px;font-size:19px}
}
)CSS";

const char APP_JS[] PROGMEM = R"JS(
// Travel-clock dashboard. No framework, no build step.
//
// Two independent loops:
//   - the network loop polls GET /api/status once a second and applies the
//     snapshot to the page (refresh(), applyStatus()),
//   - the render loop runs on requestAnimationFrame and reads a locally
//     extrapolated clock, so the flip-digit seconds readout keeps advancing
//     smoothly between polls instead of jumping once a second.
(() => {
  const $ = (id) => document.getElementById(id);
  const DAY = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const DAYFULL = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const MONTH = ['January', 'February', 'March', 'April', 'May', 'June',
                 'July', 'August', 'September', 'October', 'November', 'December'];
  const p2 = (n) => String(n).padStart(2, '0');

  // Writes a two-digit string into a pair of flip-card elements whose ids are
  // `${prefix}0` (tens) and `${prefix}1` (ones).
  function setDigits(prefix, str) {
    $(prefix + '0').textContent = str[0];
    $(prefix + '1').textContent = str[1];
  }

  // --- device clock, extrapolated between polls -----------------------------
  // baseSec is the device's second-of-day from the last /api/status; baseAt is
  // the performance.now() reading at the moment we received it. deviceSec()
  // adds the elapsed wall time since, so the seconds readout keeps advancing
  // even though the device is only polled at 1Hz.
  let baseSec = null, baseAt = 0;
  let weekday = 0, month = 1, mday = 1;   // from the last status; refreshed each poll

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
    setDigits('ah', p2(ah));
    setDigits('am', p2(am));
    ['ah0', 'ah1', 'am0', 'am1'].forEach((id) => $(id).classList.toggle('dim', !alarmOn));

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
      const sec = Math.floor(t) % 60, mins = Math.floor(t / 60) % 60, hrs = Math.floor(t / 3600) % 24;
      setDigits('h', p2(hrs));
      setDigits('m', p2(mins));
      setDigits('s', p2(sec));
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
    $('conn').textContent = 'online · ' + s.rssi + ' dBm';
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
    $('ssid').textContent = s.ssid || '—';
    $('ssid2').textContent = s.ssid || '—';
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
    $('rebootNote').textContent = 'Rebooting… the page will go offline for a few seconds.';
    setTimeout(() => { $('rebootNote').textContent = 'Takes about 8 seconds. Settings are kept.'; }, 12000);
  });

  renderAlarm();
  refresh();
  setInterval(refresh, 1000);
})();
)JS";
