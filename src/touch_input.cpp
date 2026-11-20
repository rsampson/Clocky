#include "touch_input.h"
#include "config.h"
#include "clocky.h"  // webPrint()
#include <TouchPad.h>
#include <Ticker.h>
#include <ESPAsyncWebServer.h>
#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

static TouchPad touchPad(DriverPin, SensorPin);
static Ticker touchTicker;
static volatile uint8_t pendingPresses = 0;
static uint32_t pressCount = 0;   // presses taken by loop() since boot
static uint32_t lastPressMs = 0;

#ifdef TOUCH_DIAG
// Every raw reading goes into a ring buffer that GET /api/touchraw?since=<seq>
// returns as JSON, so tools/touch_watch.py can record the pad over Wi-Fi --
// the only way to see it on wall power, since a USB cable changes the
// grounding being investigated.
static constexpr uint32_t DIAG_RING = 128;
static constexpr int SNAP_N = 12;  // raw readings logged with each press
struct TouchDiagRec { uint32_t seq, ms, raw, filt; bool touched; };
static TouchDiagRec touchDiag[DIAG_RING];
static volatile uint32_t touchDiagSeq = 0;  // seq of the newest record
static uint32_t pressSnap[SNAP_N];          // raw readings (ns) leading up to the latest press

static void touchDiagHook(uint32_t raw, uint32_t filt, bool touched) {
  uint32_t seq = touchDiagSeq + 1;
  touchDiag[seq % DIAG_RING] = {seq, millis(), raw, filt, touched};
  touchDiagSeq = seq;
}

static void handleTouchRaw(AsyncWebServerRequest *request) {
  uint32_t since = request->hasParam("since") ? request->getParam("since")->value().toInt() : 0;
  uint32_t newest = touchDiagSeq;
  if (newest - since > DIAG_RING - 1) since = newest - (DIAG_RING - 1);  // older ones are overwritten
  AsyncResponseStream *out = request->beginResponseStream("application/json");
  out->printf("{\"seq\":%lu,\"base\":%lu,\"presses\":%lu,\"nearMisses\":%lu,\"s\":[",
              (unsigned long)newest, (unsigned long)touchPad.baselineNs(), (unsigned long)pressCount,
              (unsigned long)touchPad.nearMisses());
  for (uint32_t q = since + 1; q <= newest; q++) {
    const TouchDiagRec r = touchDiag[q % DIAG_RING];
    if (r.seq != q) continue;  // overwritten while we were reading
    out->printf("%s[%lu,%lu,%lu,%lu,%d]", q == since + 1 ? "" : ",", (unsigned long)r.seq,
                (unsigned long)r.ms, (unsigned long)r.raw, (unsigned long)r.filt, r.touched);
  }
  out->print("]}");
  request->send(out);
}
#endif

static void sampleTouchPad() {
  if (touchPad.update() && pendingPresses < 255) {
    pendingPresses++;
#ifdef TOUCH_DIAG
    uint32_t seq = touchDiagSeq;
    for (int i = 0; i < SNAP_N; i++) pressSnap[i] = touchDiag[(seq - (SNAP_N - 1) + i) % DIAG_RING].raw;
#endif
  }
}

void touchInputBegin() {
  touchPad.begin();
#ifdef TOUCH_DIAG
  touchPad.onSample(touchDiagHook);
#endif
  touchTicker.attach_ms(1, sampleTouchPad);  // update() itself paces readings to sampleMs (3 ms)
}

void touchInputAddRoutes(AsyncWebServer &srv) {
#ifdef TOUCH_DIAG
  srv.on("/api/touchraw", HTTP_GET, handleTouchRaw);
#else
  (void)srv;
#endif
}

bool touchInputTakePress() {
  if (pendingPresses == 0) return false;
  noInterrupts();
  pendingPresses--;
  interrupts();
  lastPressMs = millis();
  pressCount++;
  return true;
}

void touchInputLogPress() {
  webPrint("Touch #%lu: %luns (baseline %luns)\n", (unsigned long)pressCount,
           (unsigned long)touchPad.readingNs(), (unsigned long)touchPad.baselineNs());
#ifdef TOUCH_DIAG
  char line[SNAP_N * 6 + 32] = "  raw before press (us):";
  size_t len = strlen(line);
  for (int i = 0; i < SNAP_N; i++)
    len += snprintf(line + len, sizeof(line) - len, " %lu", (unsigned long)(pressSnap[i] / 1000));
  webPrint("%s\n  at %lus uptime, WiFi status %d\n", line, millis() / 1000, (int)WiFi.status());
#endif
}

void touchInputLogHealth() {
  // Rises that didn't make it to a press show whether the pad is set too
  // insensitive. At most once a minute: Wi-Fi bursts at boot and a hovering
  // hand produce several, and the dashboard log is short.
  static uint32_t loggedNearMisses = 0;
  static uint32_t lastNearMissLogMs = 0;
  if (touchPad.nearMisses() != loggedNearMisses &&
      (lastNearMissLogMs == 0 || millis() - lastNearMissLogMs >= 60000)) {
    webPrint("Touch near-misses: %lu since last report, latest peak %luns (baseline %luns)\n",
             (unsigned long)(touchPad.nearMisses() - loggedNearMisses),
             (unsigned long)touchPad.lastNearMissNs(), (unsigned long)touchPad.baselineNs());
    loggedNearMisses = touchPad.nearMisses();
    lastNearMissLogMs = millis() | 1;
  }
  // A stuck pad reads as "never touched"; log when that starts and stops.
  static bool padStuck = false;
  if (touchPad.stuck() != padStuck) {
    padStuck = touchPad.stuck();
    webPrint(padStuck ? "Touch pad not responding (check wiring)\n" : "Touch pad responding again\n");
  }
}

uint32_t touchInputMsSinceLastPress() { return millis() - lastPressMs; }
