# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Clocky — a desk clock firmware for ESP8266 (ESP12-F, board id `esp12e`) or ESP32 (Wemos Lolin32), built with PlatformIO / Arduino framework. It syncs time over NTP with DST correction, drives a 128x32 SSD1306 OLED, chimes hourly and on a settable alarm via a piezo beeper, has a capacitive touch pad (no physical button) for toggling sleep/desk-clock mode, and serves a custom web dashboard (via `ESPAsyncWebServer` + a JSON REST API, embedded HTML/CSS/JS — no ESPUI) for Wi-Fi credentials, time zone, alarm, and OTA firmware updates.

The project was recently converted from an Arduino-IDE sketch (`.ino` files) to a proper PlatformIO layout — `.ino` files were renamed to `.cpp`, and the git repo root was moved from `src/` up to the project root so `platformio.ini` is tracked alongside `include/`, `lib/`, `test/`, and `src/`.

## Commands

Build, upload, and monitor with the PlatformIO CLI (env name is `esp12e`):

```bash
pio run                      # build
pio run -t upload            # build and flash over serial
pio run -t upload -t monitor # flash then open serial monitor
pio device monitor            # serial monitor only (115200 baud)
pio run -t clean             # clean build artifacts
```

There is no test suite configured (`test/` only contains PlatformIO's placeholder README).

OTA updates (via ElegantOTA) can be pushed from the web UI at `http://clocky.local/update` once the device is on Wi-Fi, instead of a serial upload.

## Architecture

Single firmware image split into modules that communicate through `extern` globals declared in headers (no OOP/singleton pattern — this is a small embedded project, and modules just include each other's headers):

- **`config.h`** — hardware constants only: pin assignments (buzzer, touch pad sensor/driver, status LED), display dimensions, alarm tone frequencies, touch sensitivity threshold, hostname. Change this file to adapt to different wiring or a different board; the two supported boards (ESP8266 vs ESP32) are distinguished at compile time via `#if defined(ESP32)`.
- **`clocky.h`** / **`Clocky.cpp`** — entry point (`setup()`/`loop()`), owns the shared `Preferences` (NVM) object, the `CircularBuffer` used for the debug/heap-history graph, and the alarm hour/minute/active-days state. `runAlarm()` gates the settable alarm (not the hourly chime) on `activeDays`, a `uint8_t` bitmask with bit 0 = Sunday .. bit 6 = Saturday, matching TimeLib's `weekday()` (1=Sunday..7=Saturday) via `activeDays & (1 << weekday(t))`. Aggregates the other modules' headers.
- **`wifi_manager.h`** / **`wifi_manager.cpp`** — Wi-Fi connection state machine (`WifiState`: disconnected/connecting/connected/AP mode). Falls back to a self-hosted AP named `HOSTNAME` when stored credentials fail, so the user can reconfigure via the web dashboard.
- **`time_manager.h`** / **`time_manager.cpp`** — NTP client, DST-aware `Timezone` conversion (via the `Timezone` and `TimeLib` libraries), the set of predefined `Timezone` objects the dashboard lets the user pick from, and `tzName()`/`tzCode()` lookups used by the JSON status API.
- **`web_server.h`** / **`web_server.cpp`** — owns the single `AsyncWebServer server` instance (port 80) and `setUpWebServer()`, called from `setup()`. `ElegantOTA.begin(&server)` is called right after, on the same server instance, so `/update` keeps working unchanged. Serves the dashboard's HTML/CSS/JS (from `web_assets.cpp`, embedded PROGMEM strings — no filesystem/LittleFS involved) plus a small JSON REST API: `GET /api/status` (polled by the browser every second: time, timezone name/code, RSSI, alarm disabled flag, alarm hour/minute, `activeDays` bitmask, current `weekday` (1-7), stored SSID, AP-mode flag, debug log tail) and POST actions `/api/alarm` (enable/disable), `/api/schedule` (alarm hour/minute/`activeDays`, persisted via `preferences.putUChar("activeDays", ...)`, default `0x7F` = every day), `/api/wifi`, `/api/timezone`, `/api/settime` (browser pushes its clock once when `apMode` is true, since there's no NTP source to sync from), `/api/reboot` — each mirrors what an ESPUI callback used to do (same `Preferences` keys, same in-memory state updates), just invoked over HTTP instead of a control callback.
- **`web_assets.h`** / **`web_assets.cpp`** — the dashboard itself as three PROGMEM string constants (`INDEX_HTML`, `STYLE_CSS`, `APP_JS`). Single-page app with CSS-only card layout (light/dark via `prefers-color-scheme`) and vanilla JS (no framework) that polls `/api/status` on a 1s interval and POSTs actions via `fetch()`. Tabs: Status (time, WiFi signal, "next alarm" readout computed client-side in `describeNextAlarm()` from `runHour`/`runMinute`/`activeDays`/`weekday`, debug log), Alarm (enable switch, hour/minute, day-of-week toggle buttons built client-side from the `activeDays` bitmask — see `buildDayCheckboxes()`/`applyActiveDays()`), Setup (WiFi credentials, time zone, firmware update link, reboot).
- **`debug.cpp`** — `webPrint()` (printf-style logging that goes to both `Serial` and a circular buffer) and `fetchDebugText()`, which drains that buffer into `charBuf`; `web_server.cpp`'s `/api/status` handler calls this each request to include the log tail in the JSON response.

The touch pad is read via an RC-delay trick, not a capacitive library: `DriverPin` (D3) drives a signal through a 300k resistor to `SensorPin` (D7); touching the pad adds body capacitance that measurably slows the rise time, which the firmware detects against `treshold` in `config.h`.

Persistent settings (Wi-Fi SSID/password, alarm hour/minute, time zone) are stored via the `Preferences` library (NVS-backed key/value store), not `EEPROM.h` directly.

`DEBUG_ESP_OOM` and the IRAM/2nd-heap MMU option are required build flags on ESP8266 when using `AsyncWebServer` — see the `#error`/`#warning` guards at the top of `Clocky.cpp` and the corresponding `build_flags` in `platformio.ini`.

## Repository layout note

`platformio.ini`, `include/`, `lib/`, `test/` live at the repo root; actual firmware source is under `src/`. `src/.gitignore` is a leftover from the pre-PlatformIO layout (generic C++ object-file patterns) and is largely redundant with the root `.gitignore`, which also excludes `.pio/`, `src/build/` (stale Arduino-IDE build output), and `compile_commands.json`.
