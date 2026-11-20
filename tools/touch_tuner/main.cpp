// Touch-pad tuning firmware for Clocky (separate PlatformIO env: touch_tuner).
//
// Flash with:   pio run -e touch_tuner -t upload
// Capture with: ~/.platformio/penv/bin/python tools/touch_capture.py
//
// It measures the RC touch pad (DriverPin --300k--> pad <-- SensorPin) many
// times a second and streams every raw reading over serial, labelled with
// the phase of a guided test that the OLED walks the user through
// (HANDS OFF / TOUCH+HOLD / TAP x5 / ...). Because the device labels its own
// data, nobody has to synchronise "touch now" with the capture.
//
// Each sample is measured two ways and at several pad-discharge times, so one
// session answers: how much does a touch change the reading, how noisy is it,
// and how short can the discharge wait (TouchPad::Config::settleUs) be?
//   rise: exactly what the firmware measures -- TouchPad::measureNs() from
//         lib/TouchPad (IRAM, cycle counter, GPIO registers), one Config per
//         settle time
//   fall: pad charged to Vcc, driver goes LOW, time until SensorPin reads LOW
//         (not used by the firmware; kept for comparison)
//
// Wi-Fi is brought up with Clocky's stored credentials so the readings include
// the same radio interrupt noise the real firmware lives with.
//
// Serial protocol (115200 baud): send 's' to start the guided test, 'f' for
// free-running capture (phase "free") until 'x'. Output lines:
//   # comment
//   D,<phase>,<ms>,<discharge_us>,<rise_ns>,<fall_ns>     (0 = timed out)

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TouchPad.h>
#include "../../src/config.h"  // DriverPin, SensorPin, BUZZER_PIN: same wiring as the clock

#if !defined(ESP8266)
#error "touch_tuner uses ESP8266 GPIO registers"
#endif

static const uint32_t DRV_MASK = 1UL << DriverPin;  // GPIO n <-> bit n on ESP8266 (n < 16)
static const uint32_t SEN_MASK = 1UL << SensorPin;
static const uint32_t TIMEOUT_US = TouchPad::Config().timeoutUs;

// Discharge/charge waits to compare, around the firmware's current settleUs.
static const uint16_t SETTLE_US[] = {5, 10, 50, TouchPad::Config().settleUs, 500};
static const int NUM_SETTLE = sizeof(SETTLE_US) / sizeof(SETTLE_US[0]);

Adafruit_SSD1306 display(128, 32, &Wire, -1);
Preferences preferences;

struct Phase {
  const char *label;  // in the data
  const char *big;    // OLED line 1 (<= 10 chars at text size 2)
  const char *small;  // OLED line 2
  uint16_t seconds;
};

static const Phase PHASES[] = {
  {"off",   "HANDS OFF",  "don't touch the pad", 15},
  {"hold",  "TOUCH+HOLD", "whole fingertip",     10},
  {"off",   "HANDS OFF",  "",                     8},
  {"tap",   "TAP x5",     "quick normal taps",   15},
  {"off",   "HANDS OFF",  "",                     8},
  {"hover", "HOVER",      "finger ~1cm, no touch", 10},
  {"light", "FINGERTIP",  "lightest touch, hold", 10},
  {"off",   "HANDS OFF",  "",                     8},
};
static const int NUM_PHASES = sizeof(PHASES) / sizeof(PHASES[0]);
static const uint32_t READY_MS = 3000;  // "NEXT: ..." lead-in before each phase

enum Mode { IDLE, GUIDED, FREE, DONE };
static Mode mode = IDLE;
static int phaseIdx = 0;
static bool inReady = true;
static uint32_t phaseStart = 0;

// Rise time exactly as the firmware measures it.
static uint32_t measureRise(uint16_t settleUs) {
  TouchPad::Config cfg;
  cfg.settleUs = settleUs;
  return TouchPad(DriverPin, SensorPin, cfg).measureNs();
}

// Pad at Vcc, then time the fall through the resistor. Returns ns, 0 on timeout.
// IRAM_ATTR for the same reason as TouchPad::measureNs(): a flash cache miss
// mid-measurement would add a few microseconds.
static uint32_t IRAM_ATTR measureFall(uint16_t settleUs) {
  GPOS = DRV_MASK;
  pinMode(SensorPin, OUTPUT);
  GPOS = SEN_MASK;
  delayMicroseconds(settleUs);
  pinMode(SensorPin, INPUT);

  const uint32_t tpu = ESP.getCpuFreqMHz();
  const uint32_t limit = TIMEOUT_US * tpu;
  uint32_t dt;
  noInterrupts();
  const uint32_t t0 = ESP.getCycleCount();
  GPOC = DRV_MASK;
  do {
    dt = ESP.getCycleCount() - t0;
  } while ((GPI & SEN_MASK) && dt <= limit);
  interrupts();
  return dt > limit ? 0 : (dt / tpu) * 1000 + (dt % tpu) * 1000 / tpu;
}

static void show(const char *big, const char *small, int secsLeft) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.print(big);
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.print(small);
  if (secsLeft >= 0) {
    display.setCursor(110, 22);
    display.print(secsLeft);
  }
  display.display();
}

static uint32_t phaseLenMs() { return inReady ? READY_MS : PHASES[phaseIdx].seconds * 1000UL; }

static const char *currentLabel() {
  if (mode == FREE) return "free";
  if (mode != GUIDED) return nullptr;
  return inReady ? "ready" : PHASES[phaseIdx].label;
}

// Advance the guided test and redraw the OLED (called ~4x per second).
static void stepGuide() {
  static uint32_t lastDraw = 0;
  uint32_t now = millis();
  if (mode == GUIDED) {
    if (now - phaseStart >= phaseLenMs()) {
      phaseStart = now;
      if (inReady) {
        inReady = false;
        tone(BUZZER_PIN, 2020, 80);  // "go"
        Serial.printf("# phase %s\n", PHASES[phaseIdx].label);
      } else if (++phaseIdx >= NUM_PHASES) {
        mode = DONE;
        tone(BUZZER_PIN, 1500, 400);
        Serial.println("# done");
      } else {
        inReady = true;
      }
      lastDraw = 0;
    }
  }
  if (now - lastDraw < 250) return;
  lastDraw = now;

  switch (mode) {
    case IDLE:   show("TOUCH TUNE", "waiting for 's'", -1); break;
    case FREE:   show("FREE RUN", "streaming...", -1); break;
    case DONE:   show("DONE", "thanks!", -1); break;
    case GUIDED: {
      const Phase &p = PHASES[phaseIdx];
      int left = (phaseLenMs() - (now - phaseStart) + 999) / 1000;
      if (inReady) {
        char nxt[24];
        snprintf(nxt, sizeof(nxt), "next: %s", p.big);
        show("GET READY", nxt, left);
      } else {
        show(p.big, p.small, left);
      }
      break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(DriverPin, OUTPUT);
  pinMode(SensorPin, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  Wire.begin();
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  // Same radio activity as the real clock: join the stored network.
  preferences.begin("Settings", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();
  WiFi.mode(WIFI_STA);
  if (ssid.length()) WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.printf("\n# touch_tuner cpu=%uMHz timeout=%uus wifi_ssid=%s\n",
                ESP.getCpuFreqMHz(), TIMEOUT_US, ssid.length() ? "stored" : "none");
  Serial.println("# send 's' = guided test, 'f' = free run, 'x' = stop");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 's') { mode = GUIDED; phaseIdx = 0; inReady = true; phaseStart = millis(); Serial.println("# start"); }
    if (c == 'f') { mode = FREE; Serial.println("# free"); }
    if (c == 'x') { mode = IDLE; Serial.println("# stop"); }
  }

  static bool wifiLogged = false;
  if (!wifiLogged && WiFi.status() == WL_CONNECTED) {
    wifiLogged = true;
    Serial.printf("# wifi connected rssi=%d\n", WiFi.RSSI());
  }

  stepGuide();

  const char *label = currentLabel();
  if (!label) { delay(10); return; }

  // One round = every settle time, rise and fall each (~1-2 ms), then a 20 ms
  // pause (which also lets Wi-Fi run): ~45 rounds/s, ~6 KB/s of output,
  // comfortably inside 115200 baud.
  for (int i = 0; i < NUM_SETTLE; i++) {
    uint32_t r = measureRise(SETTLE_US[i]);
    uint32_t f = measureFall(SETTLE_US[i]);
    Serial.printf("D,%s,%lu,%lu,%lu,%lu\n", label, (unsigned long)millis(),
                  (unsigned long)SETTLE_US[i], (unsigned long)r, (unsigned long)f);
  }
  delay(20);
}
