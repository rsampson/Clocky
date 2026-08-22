#include "web_assets.h"

// The entire web dashboard, stored as three PROGMEM string constants (raw
// HTML/CSS/JS text baked into flash rather than a filesystem) and served
// as-is by web_server.cpp's GET routes for "/", "/style.css", and "/app.js".
// PROGMEM keeps these off the small amount of RAM available on the ESP8266.

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Clocky</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<header>
  <h1>&#8987; Clocky</h1>
  <span id="conn" class="pill offline">offline</span>
</header>

<nav class="tabs">
  <button class="tab-btn active" data-tab="status">Status</button>
  <button class="tab-btn" data-tab="alarm">Alarm</button>
  <button class="tab-btn" data-tab="setup">Setup</button>
</nav>

<main>
  <section id="tab-status" class="tab active">
    <div class="grid">
      <div class="card">
        <div class="card-label">Time</div>
        <div id="time" class="card-value">--:--:--</div>
        <div id="tz" class="card-sub"></div>
      </div>
      <div class="card">
        <div class="card-label">WiFi Signal</div>
        <div id="rssi" class="card-value">-- dBm</div>
      </div>
      <div class="card">
        <div class="card-label">Next Alarm</div>
        <div id="next-alarm" class="card-value small">--</div>
      </div>
    </div>

    <div class="card wide">
      <div class="card-label">Log</div>
      <pre id="log"></pre>
    </div>
  </section>

  <section id="tab-alarm" class="tab">
    <div class="card wide">
      <div class="switch-row">
        <div>
          <div class="card-label">Alarm</div>
          <div id="alarm-state" class="card-sub"></div>
        </div>
        <label class="switch">
          <input type="checkbox" id="alarm-toggle">
          <span class="slider"></span>
        </label>
      </div>
    </div>

    <div class="card wide">
      <div class="card-label">Alarm Time</div>
      <div class="form-row">
        <label for="run-hour">Hour</label>
        <input type="number" id="run-hour" min="0" max="23">
        <label for="run-minute">Minute</label>
        <input type="number" id="run-minute" min="0" max="59">
      </div>
      <div class="card-label">Days</div>
      <div id="day-checkboxes" class="day-checkboxes"></div>
      <button id="save-schedule" class="btn primary">Save Alarm Time</button>
      <span id="save-schedule-status" class="save-status"></span>
    </div>
  </section>

  <section id="tab-setup" class="tab">
    <div class="card wide">
      <div class="card-label">WiFi Credentials</div>
      <div class="form-row">
        <label for="wifi-ssid">SSID</label>
        <input type="text" id="wifi-ssid" maxlength="32">
      </div>
      <div class="form-row">
        <label for="wifi-pass">Password</label>
        <input type="password" id="wifi-pass" maxlength="64">
      </div>
      <button id="save-wifi" class="btn primary">Save</button>
      <span id="save-wifi-status" class="save-status"></span>
    </div>

    <div class="card wide">
      <div class="card-label">Time Zone</div>
      <div class="form-row">
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
        </select>
      </div>
    </div>

    <div class="card wide">
      <div class="card-label">Maintenance</div>
      <div class="form-row">
        <a href="/update" class="btn">Firmware Update</a>
        <button id="reboot" class="btn danger">Reboot</button>
      </div>
    </div>
  </section>
</main>

<script src="/app.js"></script>
</body>
</html>
)HTML";

const char STYLE_CSS[] PROGMEM = R"CSS(
:root {
  --bg: #f2f4f3;
  --card-bg: #ffffff;
  --text: #1a1f1c;
  --text-sub: #6b7570;
  --accent: #2e5d7d;
  --accent-contrast: #ffffff;
  --border: #e1e5e2;
  --danger: #c0392b;
  --online: #2e7d5b;
  --offline: #b0392b;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #14171a;
    --card-bg: #1e2226;
    --text: #eceff1;
    --text-sub: #97a1a8;
    --accent: #4596c9;
    --accent-contrast: #0b0f12;
    --border: #2b3136;
    --danger: #e57373;
    --online: #45b587;
    --offline: #e57373;
  }
}
* { box-sizing: border-box; }
body {
  margin: 0;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  background: var(--bg);
  color: var(--text);
}
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 1rem 1.25rem;
}
header h1 { font-size: 1.25rem; margin: 0; }
.pill {
  font-size: 0.75rem;
  padding: 0.25rem 0.6rem;
  border-radius: 1rem;
  font-weight: 600;
}
.pill.online { background: var(--online); color: var(--accent-contrast); }
.pill.offline { background: var(--offline); color: var(--accent-contrast); }

.tabs {
  display: flex;
  gap: 0.5rem;
  padding: 0 1rem;
  border-bottom: 1px solid var(--border);
}
.tab-btn {
  background: none;
  border: none;
  color: var(--text-sub);
  font-size: 0.95rem;
  padding: 0.6rem 0.9rem;
  cursor: pointer;
  border-bottom: 2px solid transparent;
}
.tab-btn.active {
  color: var(--accent);
  border-bottom-color: var(--accent);
  font-weight: 600;
}
main { padding: 1rem; max-width: 720px; margin: 0 auto; }
.tab { display: none; }
.tab.active { display: block; }

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
  gap: 0.75rem;
  margin-bottom: 0.75rem;
}
.card {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: 0.75rem;
  padding: 0.9rem 1rem;
}
.card.wide { margin-bottom: 0.75rem; }
.card-label {
  font-size: 0.75rem;
  color: var(--text-sub);
  text-transform: uppercase;
  letter-spacing: 0.03em;
  margin-bottom: 0.35rem;
}
.card-value { font-size: 1.5rem; font-weight: 600; }
.card-value.small { font-size: 1.1rem; }
.card-sub { font-size: 0.8rem; color: var(--text-sub); margin-top: 0.2rem; }

.switch-row { display: flex; align-items: center; justify-content: space-between; }
.switch { position: relative; display: inline-block; width: 48px; height: 28px; }
.switch input { opacity: 0; width: 0; height: 0; }
.switch .slider {
  position: absolute; cursor: pointer; inset: 0;
  background: var(--border); border-radius: 28px; transition: 0.15s;
}
.switch .slider::before {
  content: ""; position: absolute; height: 22px; width: 22px; left: 3px; bottom: 3px;
  background: var(--card-bg); border-radius: 50%; transition: 0.15s;
}
.switch input:checked + .slider { background: var(--accent); }
.switch input:checked + .slider::before { transform: translateX(20px); }

pre#log {
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  font-size: 0.75rem;
  white-space: pre-wrap;
  word-break: break-word;
  max-height: 200px;
  overflow-y: auto;
  margin: 0;
  color: var(--text-sub);
}

.form-row {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  flex-wrap: wrap;
  margin-bottom: 0.6rem;
}
.form-row label { font-size: 0.85rem; color: var(--text-sub); min-width: 4.5rem; }
.form-row input[type="text"],
.form-row input[type="password"],
.form-row input[type="number"],
.form-row select {
  flex: 1;
  min-width: 6rem;
  padding: 0.5rem 0.6rem;
  border: 1px solid var(--border);
  border-radius: 0.5rem;
  background: var(--bg);
  color: var(--text);
  font-size: 0.9rem;
}
.form-row input[type="number"] { max-width: 5rem; flex: none; }

.day-checkboxes {
  display: flex;
  flex-wrap: wrap;
  gap: 0.4rem;
  margin-bottom: 0.8rem;
}
.day-btn {
  border: 1px solid var(--border);
  background: var(--card-bg);
  color: var(--text);
  border-radius: 0.5rem;
  padding: 0.5rem 0.7rem;
  font-size: 0.85rem;
  cursor: pointer;
}
.day-btn.active {
  background: var(--accent);
  color: var(--accent-contrast);
  border-color: var(--accent);
}

.btn {
  display: inline-block;
  background: var(--card-bg);
  color: var(--text);
  border: 1px solid var(--border);
  border-radius: 0.5rem;
  padding: 0.55rem 1rem;
  font-size: 0.9rem;
  cursor: pointer;
  text-decoration: none;
}
.btn.primary { background: var(--accent); color: var(--accent-contrast); border-color: var(--accent); }
.btn.danger { background: var(--danger); color: var(--accent-contrast); border-color: var(--danger); }
.save-status { font-size: 0.8rem; color: var(--text-sub); margin-left: 0.6rem; }
)CSS";

const char APP_JS[] PROGMEM = R"JS(
// Single-page dashboard, no framework/build step. Polls GET /api/status once a
// second (see refresh() at the bottom) and POSTs to /api/* on user actions.
// Everything here runs from this one IIFE, sharing state via the closure
// variables below instead of globals.
(() => {
  const $ = (id) => document.getElementById(id);
  let apMode = false;      // mirrors the server's ap_mode; drives syncBrowserTime()
  let timeSynced = false;  // true once we've POSTed the browser clock this page load
  let activeDays = 0x7F;   // local copy of the day-of-week alarm mask; bit 0 = Sunday
  let daysBuilt = false;   // guards the one-time day-button DOM construction below
  // True while the user has an unsaved edit in the hour/minute fields. Prevents
  // the 1s status poll from overwriting what they just typed before they hit
  // Save — see the fix note on the run-hour/run-minute polling below.
  let scheduleDirty = false;
  const DAY_NAMES = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];  // index i == bit i of activeDays

  // Tab switching: each button's data-tab attribute (e.g. "alarm") names the
  // section to show, matched by id ("tab-alarm"). Purely a CSS class toggle —
  // all three tabs' markup is always in the DOM, just hidden via .tab.active.
  document.querySelectorAll('.tab-btn').forEach((btn) => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.tab-btn').forEach((b) => b.classList.remove('active'));
      document.querySelectorAll('.tab').forEach((t) => t.classList.remove('active'));
      btn.classList.add('active');
      $('tab-' + btn.dataset.tab).classList.add('active');
    });
  });

  // Builds the Sun..Sat day-toggle buttons once. Each button's own click
  // handler flips its bit in `activeDays` directly and updates its own
  // highlight — there's no separate "read the buttons back" step, `activeDays`
  // is the single source of truth these buttons write into and read from.
  function buildDayCheckboxes() {
    const el = $('day-checkboxes');
    el.innerHTML = '';
    DAY_NAMES.forEach((name, i) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'day-btn';
      btn.id = 'day-btn-' + i;
      btn.textContent = name;
      btn.addEventListener('click', () => {
        activeDays ^= (1 << i);
        btn.classList.toggle('active', !!(activeDays & (1 << i)));
      });
      el.appendChild(btn);
    });
  }

  // Sets activeDays (and the buttons' highlighted state) from a server value.
  // Only called once, right after buildDayCheckboxes() — see the `!daysBuilt`
  // guard in applyStatus() — so a day the user is actively toggling can't get
  // silently reverted by the next status poll.
  function applyActiveDays(mask) {
    activeDays = mask;
    DAY_NAMES.forEach((_, i) => {
      const btn = $('day-btn-' + i);
      if (btn) btn.classList.toggle('active', !!(mask & (1 << i)));
    });
  }

  // Computes the "Next Alarm" status card text (e.g. "Tomorrow 7:30 AM") from
  // a /api/status response. Walks forward day-by-day from today (s.weekday)
  // looking for the next active day; for today specifically, it also checks
  // whether the alarm time has already passed, in which case today doesn't
  // count and the search continues to tomorrow. offset goes up to 8, not 7,
  // so that if every day is active, today (offset 0) can be skipped for
  // already-passed and the loop still finds next week's "today" at offset 7.
  function describeNextAlarm(s) {
    if (s.disabled || !(s.activeDays & 0x7F)) return 'Off';
    for (let offset = 0; offset < 8; offset++) {
      const day = (s.weekday - 1 + offset) % 7;  // weekday: 1=Sun..7=Sat -> 0-indexed
      if (!(s.activeDays & (1 << day))) continue;
      if (offset === 0) {
        // s.time is "HH:MM:SS ..."; compare minute-of-day so "already passed
        // today" skips to the next active day instead of showing a time
        // that's already gone by.
        const nowMinutes = parseInt(s.time.slice(0, 2), 10) * 60 + parseInt(s.time.slice(3, 5), 10);
        if (s.runHour * 60 + s.runMinute <= nowMinutes) continue;
      }
      const h = s.runHour % 12 === 0 ? 12 : s.runHour % 12;
      const ampm = s.runHour < 12 ? 'AM' : 'PM';
      const label = offset === 0 ? 'Today' : offset === 1 ? 'Tomorrow' : DAY_NAMES[day];
      return label + ' ' + h + ':' + String(s.runMinute).padStart(2, '0') + ' ' + ampm;
    }
    return 'Off';  // no active day had a still-upcoming time (shouldn't normally happen)
  }

  // Gives the device a rough clock when it has no internet access (AP mode,
  // so no NTP). One-shot per page load (guarded by timeSynced) — the device
  // ignores this once it's on real Wi-Fi, see handleSetTime() in
  // web_server.cpp.
  function syncBrowserTime() {
    if (timeSynced) return;
    timeSynced = true;
    fetch('/api/settime', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ epoch: Math.floor(Date.now() / 1000) }),
    });
  }

  // Applies one /api/status response to the whole page. Called every second
  // by refresh() below.
  function applyStatus(s) {
    $('conn').textContent = 'online';
    $('conn').className = 'pill online';

    $('time').textContent = s.time;
    $('tz').textContent = s.timezone;
    $('rssi').textContent = s.rssi + ' dBm';

    $('alarm-toggle').checked = !s.disabled;
    $('alarm-state').textContent = s.disabled ? 'Alarm OFF' : 'Alarm ON';
    $('next-alarm').textContent = describeNextAlarm(s);

    $('log').textContent = s.log;

    // Only overwrite the hour/minute inputs from the server while the user
    // isn't mid-edit. Without this guard, this 1-second poll would stomp
    // whichever field currently doesn't have focus — e.g. editing the hour
    // field, then tabbing to minute, would get the hour field reverted back
    // to its old value by the next poll tick, before Save was ever clicked.
    if (!scheduleDirty) {
      $('run-hour').value = s.runHour;
      $('run-minute').value = s.runMinute;
    }

    // Day buttons and activeDays are seeded from the server exactly once, the
    // first time status loads — after that the buttons themselves own
    // activeDays until Save is clicked (see buildDayCheckboxes()).
    if (!daysBuilt) {
      buildDayCheckboxes();
      applyActiveDays(s.activeDays);
      daysBuilt = true;
    }

    $('wifi-ssid').placeholder = s.ssid || '';
    if (s.timezoneCode) $('timezone').value = s.timezoneCode;

    apMode = s.apMode;
    if (apMode) syncBrowserTime();
  }

  // The heartbeat: fetches /api/status and applies it, or shows "offline" if
  // the device didn't respond (e.g. it's mid-reboot). Called immediately and
  // then every second, at the bottom of this file.
  function refresh() {
    fetch('/api/status')
      .then((r) => r.json())
      .then(applyStatus)
      .catch(() => {
        $('conn').textContent = 'offline';
        $('conn').className = 'pill offline';
      });
  }

  $('alarm-toggle').addEventListener('change', (e) => {
    fetch('/api/alarm', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ disable: !e.target.checked }),
    }).then(refresh);
  });

  // Mark the schedule fields dirty the moment the user types in either one,
  // so the next status poll (up to 1s away) doesn't overwrite what they're
  // editing. Cleared again once Save succeeds, below.
  $('run-hour').addEventListener('input', () => { scheduleDirty = true; });
  $('run-minute').addEventListener('input', () => { scheduleDirty = true; });

  $('save-schedule').addEventListener('click', () => {
    const body = {
      hour: parseInt($('run-hour').value, 10),
      minute: parseInt($('run-minute').value, 10),
      activeDays: activeDays,
    };
    fetch('/api/schedule', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    }).then(() => {
      scheduleDirty = false;  // safe to let polling overwrite the fields again
      $('save-schedule-status').textContent = 'Saved';
      setTimeout(() => { $('save-schedule-status').textContent = ''; }, 2000);
    });
  });

  $('save-wifi').addEventListener('click', () => {
    fetch('/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ssid: $('wifi-ssid').value,
        pass: $('wifi-pass').value,
      }),
    }).then(() => {
      $('save-wifi-status').textContent = 'Saved';
      setTimeout(() => { $('save-wifi-status').textContent = ''; }, 2000);
    });
  });

  $('timezone').addEventListener('change', (e) => {
    fetch('/api/timezone', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ tz: e.target.value }),
    }).then(refresh);
  });

  $('reboot').addEventListener('click', () => {
    if (confirm('Reboot Clocky now?')) {
      fetch('/api/reboot', { method: 'POST' });
    }
  });

  refresh();                    // populate the page immediately on load...
  setInterval(refresh, 1000);   // ...then keep it live, once a second
})();
)JS";
