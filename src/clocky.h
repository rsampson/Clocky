#pragma once

// Shared state and prototypes used across the whole firmware. There's no
// class/singleton here (deliberately, for a project this size) — every
// module includes this header and reads/writes the same handful of globals
// declared `extern` below. The one-line summary of who owns what:
//   - Clocky.cpp defines and updates all the alarm/state variables here.
//   - web_server.cpp reads them for /api/status and writes them from the
//     dashboard's POST handlers.
//   - Everything is persisted to `preferences` so it survives a reboot.

#include "wifi_manager.h"
#include "time_manager.h"
#include <Preferences.h>

#include <CircularBuffer.hpp>

inline constexpr size_t bufferSize = 400;

// --- Shared Utility Objects ---
extern Preferences preferences;       // NVS-backed settings store; see Preferences.begin("Settings") in Clocky.cpp
extern CircularBuffer<float, 24> dayBuffer;  // currently unused leftover from an earlier feature; safe to ignore
extern char charBuf[bufferSize];      // holds the drained debug log text; filled by fetchDebugText() in debug.cpp

// --- Settable Alarm State ---
// String forms of runHour/runMinute, for round-tripping through Preferences.
extern String stored_hour;
extern String stored_minute;
extern uint16_t runHour;   // 0-23
extern uint16_t runMinute; // 0-59
extern bool disable;       // true = alarm muted (hourly chime is separate, see runAlarm() in Clocky.cpp)
// Bitmask of alarm days, bit 0 = Sunday .. bit 6 = Saturday (matches TimeLib's weekday()).
extern uint8_t activeDays;

void fetchDebugText();     // debug.cpp: copies the log buffer into charBuf
void updateOledDisplay();  // Clocky.cpp: redraws the physical OLED
void webPrint(const char *format, ...);  // debug.cpp: printf-style logging, also visible on the dashboard
