#pragma once

// Clocky's glue around lib/TouchPad: owns the pad, samples it from a 1 ms
// Ticker, hands presses to loop(), and does the touch logging (plus the
// TOUCH_DIAG Wi-Fi diagnostics when built with -D TOUCH_DIAG).
//
// Why a Ticker and not loop(): loop() blocks for ~2 s at a time (the stock
// fetch's TLS, the hourly chime). Ticker callbacks still run while those wait
// on the network or in delay(), so a touch -- or the release between two
// taps -- during a fetch isn't lost. The callback only counts presses;
// loop() acts on them, possibly a moment late.

#include <Arduino.h>

class AsyncWebServer;

void touchInputBegin();                        // pins + untouched baseline (keep hands off) + sampling Ticker
void touchInputAddRoutes(AsyncWebServer &srv); // TOUCH_DIAG: GET /api/touchraw; no-op otherwise
bool touchInputTakePress();                    // true once per press; call until false
void touchInputLogPress();                     // log the press just taken (after acting on it)
void touchInputLogHealth();                    // rate-limited near-miss / stuck-pad logging
uint32_t touchInputMsSinceLastPress();         // since loop() last took a press
