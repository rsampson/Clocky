# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Clocky — a desk clock firmware for ESP8266 (ESP12-F, board id `esp12e`) or ESP32 (Wemos Lolin32), built with PlatformIO / Arduino framework. It syncs time over NTP with DST correction, drives a 128x32 SSD1306 OLED, chimes hourly and on a settable alarm via a piezo beeper, has a capacitive touch pad (no physical button) for toggling sleep/desk-clock mode, and exposes an ESPUI web interface for Wi-Fi credentials, time zone, alarm, and OTA firmware updates.

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
- **`clocky.h`** / **`Clocky.cpp`** — entry point (`setup()`/`loop()`), owns the shared `Preferences` (NVM) object, the `CircularBuffer` used for the debug/heap-history graph, and the alarm hour/minute state. Aggregates the other modules' headers.
- **`wifi_manager.h`** / **`wifi_manager.cpp`** — Wi-Fi connection state machine (`WifiState`: disconnected/connecting/connected/AP mode). Falls back to a self-hosted AP named `HOSTNAME` when stored credentials fail, so the user can reconfigure via the web UI.
- **`time_manager.h`** / **`time_manager.cpp`** — NTP client, DST-aware `Timezone` conversion (via the `Timezone` and `TimeLib` libraries), and the set of predefined `Timezone` objects the UI lets the user pick from.
- **`ui.h`** / **`gui.cpp`** — ESPUI control handles (`struct UI`) and the code that builds the web UI tabs (Wi-Fi credentials, schedule/alarm, time zone, debug).
- **`callback.cpp`** — ESPUI callback implementations (`generalCallback`, `textCallback`, `hourCallback`, `minuteCallback`, `switchCallback`, `SaveWifiDetailsCallback`, `SaveScheduleCallback`, `TZcallback`, `ESPReset`) that persist settings to `Preferences` and update in-memory state.
- **`debug.cpp`** — heap/debug telemetry rendered on the web UI's debug tab.

The touch pad is read via an RC-delay trick, not a capacitive library: `DriverPin` (D3) drives a signal through a 300k resistor to `SensorPin` (D7); touching the pad adds body capacitance that measurably slows the rise time, which the firmware detects against `treshold` in `config.h`.

Persistent settings (Wi-Fi SSID/password, alarm hour/minute, time zone) are stored via the `Preferences` library (NVS-backed key/value store), not `EEPROM.h` directly.

`DEBUG_ESP_OOM` and the IRAM/2nd-heap MMU option are required build flags on ESP8266 when using ESPUI — see the `#error`/`#warning` guards at the top of `Clocky.cpp` and the corresponding `build_flags` in `platformio.ini`.

## Repository layout note

`platformio.ini`, `include/`, `lib/`, `test/` live at the repo root; actual firmware source is under `src/`. `src/.gitignore` is a leftover from the pre-PlatformIO layout (generic C++ object-file patterns) and is largely redundant with the root `.gitignore`, which also excludes `.pio/`, `src/build/` (stale Arduino-IDE build output), and `compile_commands.json`.
