/**
 * When this program boots, it will load an SSID and password from nvmem.
 * If these credentials do not work for some reason, the ESP will create an Access
 * Point wifi with the SSID HOSTNAME (defined in config.h). You can then connect and use
 * the web dashboard's Setup tab to store credentials into the nvmem.
 *
 */
// Tested on ESP32 Wemos Lolin32  and ESP12-F (esp8266), make sure these match your board,
// otherwise strange results will occur.

#include "config.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Arduino.h>

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);   // SDA connects to D2, SCL  to D1 on the Wemos mini esp8266

// The capacitive touch pad (the clock's only button). All the measuring,
// filtering and debouncing lives in lib/TouchPad; see TouchPad.h.
#include <TouchPad.h>
TouchPad touchPad(DriverPin, SensorPin);

// Counts touches since boot. Only its value modulo NUM_MODES matters: each
// touch steps to the next mode, clock -> stocks -> sleep -> clock ...
// See currentMode() and updateOledDisplay().
unsigned int buttonPressCounter;
enum DisplayMode { MODE_CLOCK = 0, MODE_STOCKS = 1, MODE_SLEEP = 2, NUM_MODES = 3 };
static inline int currentMode() { return buttonPressCounter % NUM_MODES; }

#if defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#else

// esp8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <umm_malloc/umm_heap_select.h>
#ifndef CORE_MOCK
#ifndef MMU_IRAM_HEAP
#warning Try MMU option '2nd heap shared' in 'tools' IDE menu (cf. https://arduino-esp8266.readthedocs.io/en/latest/mmu.html#option-summary)
#warning then check http://<ip>/heap
#endif  // MMU_IRAM_HEAP
#ifndef DEBUG_ESP_OOM
#error on ESP8266 with AsyncWebServer, you must define OOM debug option when developping
#endif
#endif
#endif

#include "wifi_manager.h"

#include <NTPClient.h>         // version 3.2.1 for ntp time client
#include <Timezone.h>          // https://github.com/JChristensen/Timezone
#include "time_manager.h"

#include <Preferences.h>
Preferences preferences;

#include <TimeLib.h>
#include <ElegantOTA.h>  // version 3.1.6 https://github.com/ayushsharma82/ElegantOTA

#include <CircularBuffer.hpp>         // version 1.4.0 https://github.com/rlogiacco/CircularBuffer
// Leftover from a temperature-averaging feature this project doesn't have
// (Clocky has no temp sensor) — declared and cleared but never filled/read.
// Kept only because CLAUDE.md/other tooling may reference it; safe to delete.
CircularBuffer<float, 24> dayBuffer;

#include "web_server.h"
#include "clocky.h"
#include "stocks.h"

#include <Ticker.h>

#ifdef TOUCH_DIAG
// Touch diagnostics (build with -D TOUCH_DIAG): every raw reading goes into a
// ring buffer that GET /api/touchraw?since=<seq> returns as JSON, so
// tools/touch_watch.py can record the pad over Wi-Fi — the only way to see it
// on wall power, since a USB cable changes the grounding being investigated.
struct TouchDiagRec { uint32_t seq, ms, raw, filt; bool touched; };
static TouchDiagRec touchDiag[128];
static volatile uint32_t touchDiagSeq = 0;  // seq of the newest record

static void touchDiagHook(uint32_t raw, uint32_t filt, bool touched) {
  uint32_t seq = touchDiagSeq + 1;
  touchDiag[seq % 128] = {seq, millis(), raw, filt, touched};
  touchDiagSeq = seq;
}

static void handleTouchRaw(AsyncWebServerRequest *request) {
  uint32_t since = request->hasParam("since") ? request->getParam("since")->value().toInt() : 0;
  uint32_t newest = touchDiagSeq;
  if (newest - since > 127) since = newest - 127;  // older ones are overwritten
  AsyncResponseStream *out = request->beginResponseStream("application/json");
  out->printf("{\"seq\":%lu,\"base\":%lu,\"presses\":%u,\"nearMisses\":%lu,\"s\":[",
              (unsigned long)newest, (unsigned long)touchPad.baselineNs(), buttonPressCounter,
              (unsigned long)touchPad.nearMisses());
  for (uint32_t q = since + 1; q <= newest; q++) {
    const TouchDiagRec r = touchDiag[q % 128];
    if (r.seq != q) continue;  // overwritten while we were reading
    out->printf("%s[%lu,%lu,%lu,%lu,%d]", q == since + 1 ? "" : ",", (unsigned long)r.seq,
                (unsigned long)r.ms, (unsigned long)r.raw, (unsigned long)r.filt, r.touched);
  }
  out->print("]}");
  request->send(out);
}
#endif

// The pad is read from a 1 ms Ticker rather than from loop(), because loop()
// blocks for ~2 s at a time (the stock fetch's TLS, the hourly chime). Ticker
// callbacks still run while those wait on the network or in delay(), so a
// touch -- or the release between two taps -- during a fetch isn't lost.
// The callback only counts presses; loop() acts on them (beep, mode change),
// possibly a moment late.
static Ticker touchTicker;
static volatile uint8_t pendingTouches = 0;
static uint32_t lastTouchMs = 0;  // when loop() last acted on a touch; see handleStocks() call

#ifdef TOUCH_DIAG
static uint32_t pressSnap[12];  // raw readings (ns) leading up to the latest press
#endif

static void sampleTouchPad() {
  if (touchPad.update() && pendingTouches < 255) {
    pendingTouches++;
#ifdef TOUCH_DIAG
    uint32_t seq = touchDiagSeq;
    for (int i = 0; i < 12; i++) pressSnap[i] = touchDiag[(seq - 11 + i) % 128].raw;
#endif
  }
}

#if !defined(ESP32)
#include <core_esp8266_waveform.h>  // startWaveformClockCycles(), stopWaveform()
#endif

// --- Piezo beeper: differential drive ---
// The piezo sits between BUZZER_PIN (D6) and BUZZER_PIN_N (D5), and D5 always
// carries the complement of D6, so the piezo sees +/-3.3 V (6.6 V peak to
// peak) instead of 0..3.3 V: about 6 dB louder, with no extra parts. A piezo
// still wired from D6 to GND works exactly as before (D5 just toggles unused).
// The core's waveform generator does this: D5 is started locked to D6 half a
// period later. That needs its phase-locked mode (enablePhaseLockedWaveform()
// in setup()); the default PWM-locked generator ignores phase alignment and
// would leave the two pins at an arbitrary, possibly cancelling, phase.
// ESP32 builds keep plain single-ended tone() on BUZZER_PIN.
static void buzzerOff() {
  noTone(BUZZER_PIN);
#if !defined(ESP32)
  noTone(BUZZER_PIN_N);
#endif
}

static void buzzerTone(unsigned int freq, unsigned long ms) {
#if defined(ESP32)
  tone(BUZZER_PIN, freq, ms);
#else
  if (freq == 0) {
    buzzerOff();
    return;
  }
  const uint32_t period = microsecondsToClockCycles(1000000UL) / freq;
  const uint32_t high = period / 2;
  const uint32_t low = period - high;
  uint32_t run = microsecondsToClockCycles(ms * 1000UL);  // rounded up to whole periods, as tone() does
  run += period - 1;
  run -= run % period;
  stopWaveform(BUZZER_PIN_N);  // only a freshly started waveform is phase-aligned
  startWaveformClockCycles(BUZZER_PIN, high, low, run);                        // changes on the fly if already running
  startWaveformClockCycles(BUZZER_PIN_N, high, low, run, BUZZER_PIN, high);   // rises as D6 falls
#endif
}

//Function Prototypes
void runAlarm(void);

// String forms of runHour/runMinute, kept around so they can be handed straight
// to Preferences.putString()/getString() without re-formatting each time.
String stored_hour, stored_minute;

// Input values
uint16_t runHour = 2;     // hour to start running
uint16_t runMinute = 10;  // minute to start running

bool disable;                // true = settable alarm is muted (hourly chime is unaffected)
uint8_t activeDays = 0x7F;   // which weekdays the alarm fires on; see clocky.h for the bit layout
char charBuf[bufferSize];    // holds the drained debug log text; see debug.cpp
String bootTime;

void setup() {

  Serial.begin(115200);

#if !defined(ESP32)
  enablePhaseLockedWaveform();  // needed for the complementary buzzer drive; see buzzerTone()
  pinMode(BUZZER_PIN_N, OUTPUT);
  digitalWrite(BUZZER_PIN_N, LOW);
#endif
  pinMode(BUZZER_PIN, OUTPUT);
  touchPad.begin();  // also learns the untouched baseline, so keep hands off at boot
  touchTicker.attach_ms(1, sampleTouchPad);  // update() itself paces readings to sampleMs (3 ms)
  pinMode(ledPin, OUTPUT);

  buzzerTone(highTone, 1000);  // audible boot beep

  // Preferences is the NVS-backed key/value store (not EEPROM.h) used for every
  // setting the web UI can change: Wi-Fi credentials, time zone, alarm schedule.
  if (!preferences.begin("Settings")) {
    Serial.println(F("Failed to open preferences."));
    ESP.restart();
  }

  tz = TZstringToPointer(preferences.getString("tz", "UTC"));
  printTZ(tz);

  // Kick off the non-blocking WiFi state machine; handleWiFi() in loop()
  // drives the connection (including the very first attempt) from here.
  currentWifiState = WIFI_STATE_DISCONNECTED;

  // NTPClient just opens a local UDP socket here; it hasn't talked to a time
  // server yet (WiFi isn't connected at this point in setup()). setSyncProvider
  // tells TimeLib to call getNtpTime() whenever it needs a fresh time, and
  // getNtpTime() reads whatever NTPClient last fetched (or 0 if nothing yet).
  // The very first call below will get 0, so TimeLib just marks itself
  // "needs sync" rather than adopting a bogus time. See the initialTimeSynced
  // block in loop() for how we force a real resync once WiFi actually connects.
  timeClient.begin();
  setSyncProvider(getNtpTime);
  setSyncInterval(300);  // re-run getNtpTime() at most every 5 minutes

  dayBuffer.clear();

  // Load saved alarm settings; defaults here match a fresh/never-configured device.
  runHour = (stored_hour = preferences.getString("hour", "8")).toInt();
  runMinute = (stored_minute = preferences.getString("minute", "0")).toInt();
  disable = preferences.getBool("disable", false);  // missing key => alarm enabled
  activeDays = preferences.getUChar("activeDays", 0x7F);  // 0x7F = every day

  Serial.println(F("configuring web server"));
  setUpWebServer();  // starts the AsyncWebServer; see web_server.cpp for routes

#ifdef TOUCH_DIAG
  touchPad.onSample(touchDiagHook);
  server.on("/api/touchraw", HTTP_GET, handleTouchRaw);
#endif
  ElegantOTA.begin(&server);  // adds the /update page to the same web server
  // (No "up at ..." boot message here — the clock hasn't synced yet, so it would
  // just log 00:00:00. The "WiFi up: <ip>" line from wifi_manager.cpp marks the
  // boot in the web log instead.)
  // getBootReasonMessage(bootReasonMessage, BOOT_REASON_MESSAGE_SIZE);
  // webPrint("Reset reason: %s\n", bootReasonMessage);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }

  display.clearDisplay();

  display.setTextSize(1);               // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);  // Draw white text
  display.setFont(&FreeSans12pt7b);
  display.setCursor(0,20);               // Start at top-left corner
  display.println("booting");
  display.display();
  // Note: setup() does NOT wait for a touch or for WiFi/NTP here — loop() starts
  // immediately so the clock can sync as soon as possible after a power cycle.

  Serial.println(F("We Are Go!"));
}

// Sets up font/cursor and fills buf with what the desk clock shows right now.
// Cycles based on the current second, so day-of-week and date each get one
// 1-second slot per 5 seconds, while the time itself (the thing you actually
// glance at) gets 3 of the 5 slots. Shared by clock mode and by stock mode's
// fallback when no fresh stock data is available.
static void prepClockFace(char *buf) {
  int hr = hour() % 12;
  if (hr == 0) hr = 12;  // convert 24h hour() to 12h display, with 0 shown as 12

  switch (second() % 5) {
    case 0:  // day of week, e.g. "Mon"
      display.setFont(&FreeSans18pt7b);
      display.setCursor(0, 28);
      sprintf(buf, "   %s", dayShortStr(weekday()));
      break;
    case 1:  // display time 3 times longer than day of week or date
    case 2:
    case 3:
      display.setFont(&FreeSans24pt7b);
      display.setCursor(3, 31);
      sprintf(buf, "%2d:%02d", hr, minute());
      break;
    case 4:  // month + day, e.g. "Aug 22"
      display.setFont(&FreeSans12pt7b);
      display.setCursor(12, 25);
      sprintf(buf, " %s %2d", monthShortStr(month()), day());
      break;
  }
}

// Stock mode's face: each index gets 3 seconds, showing its value and a
// one-letter name together (e.g. "34567 D", "5123 S", "23456 N") so there's no
// separate name screen to wait through. The three indexes rotate every 9
// seconds. Values are rounded to whole points, with a small up/down triangle
// at the right showing direction versus the previous close.
static void prepStockFace(char *buf) {
  unsigned int slot = now() % (NUM_STOCKS * 3);
  const StockIndex &s = stocks[slot / 3];

  // The value is drawn by the caller from buf (12pt); the one-letter name is
  // drawn here in a smaller font (9pt) right after it.
  sprintf(buf, "%ld", lroundf(s.price));
  char tag[2] = { s.label[0], 0 };

  // Centre value + gap + letter in the space left of the triangle.
  const int arrowW = 12, gap = 5;
  int avail = SCREEN_WIDTH - arrowW - 2;
  int16_t x1, y1, tx1, ty1;
  uint16_t w, h, tw, th;
  display.setFont(&FreeSans12pt7b);
  display.getTextBounds(buf, 0, 25, &x1, &y1, &w, &h);
  display.setFont(&FreeSans9pt7b);
  display.getTextBounds(tag, 0, 25, &tx1, &ty1, &tw, &th);
  int x = (avail - ((int)w + gap + (int)tw)) / 2 - x1;
  if (x < 0) x = 0;
  display.setCursor(x + (int)w + gap, 25);
  display.print(tag);
  display.setFont(&FreeSans12pt7b);
  display.setCursor(x, 25);

  if (s.price != s.prevClose) {
    int ax = SCREEN_WIDTH - arrowW;  // left edge of the triangle
    if (s.price > s.prevClose) {
      display.fillTriangle(ax, 22, ax + arrowW - 1, 22, ax + arrowW / 2, 10, SSD1306_WHITE);  // up
    } else {
      display.fillTriangle(ax, 10, ax + arrowW - 1, 10, ax + arrowW / 2, 22, SSD1306_WHITE);  // down
    }
  }
}

// Called twice a second from loop(). Which of the three display modes is
// shown depends only on buttonPressCounter (bumped once per touch) modulo
// NUM_MODES — there's no separate "mode" variable. See currentMode().
void updateOledDisplay(void) {

  char buf[20];

  display.clearDisplay();

  // AP-mode provisioning screen: when WiFi credentials haven't worked and
  // Clocky has fallen back to being its own access point, the clock is
  // meaningless (no NTP sync) — instead show the SoftAP IP the user needs to
  // type into a browser to reach the Setup tab. The AP address is the fixed
  // 192.168.4.1 configured in wifi_manager.cpp's setUpAP().
  if (currentWifiState == WIFI_STATE_AP_MODE) {
    display.setFont(&FreeSans12pt7b);
    display.setCursor(0, 40);
    display.print(WiFi.softAPIP().toString());
    display.display();
    return;
  }

  sprintf(buf, " ");
  //display.setTextColor(SSD1306_WHITE);
  //display.invertDisplay(true);

  switch (currentMode()) {
    case MODE_CLOCK:               // desk-clock mode (the default at boot)
      digitalWrite(ledPin, LOW);  // turn on the night lite
      display.stopscroll();
      prepClockFace(buf);
      break;
    case MODE_STOCKS:              // stock-index mode: Dow / S&P 500 / Nasdaq
      digitalWrite(ledPin, LOW);  // turn on the night lite
      display.stopscroll();
      // No fresh data (still fetching, WiFi down, fetch failed, data gone
      // stale): behave exactly like the desk clock until handleStocks() in
      // loop() gets good values.
      if (stocksFresh()) {
        prepStockFace(buf);
      } else {
        prepClockFace(buf);
      }
      break;
    case MODE_SLEEP:  // sleep mode: screen mostly dark
      digitalWrite(ledPin, HIGH) ;  // turn off the night lite
      // NOTE: deliberately no noTone() here. Sleep mode only suppresses the
      // display light; the settable alarm must keep sounding to wake the user
      // (it's silenced by a touch, which steps on to desk-clock mode). The
      // hourly chime is already prevented from ringing in sleep mode by the
      // currentMode() != MODE_SLEEP guard in runAlarm().
      display.clearDisplay();
      //display.setTextColor(SSD1306_WHITE);
      //display.invertDisplay(false);
      display.setFont(&FreeSans12pt7b);
      display.setCursor(3, 16);
      sprintf(buf, "      .");
      display.startscrollright(0x00, 0x0F);  // small scrolling dot as an "I'm still alive" indicator
      break;
  }
  display.print(buf);
  display.display();
}

// --- Alarm tune ---
// The settable alarm plays this little tune on a loop until a touch silences
// it. Notes are stepped by a Ticker (a hardware-timer callback) rather than
// from loop(), because loop() stalls for seconds at a time (hourly chime, stock fetch) — far too
// uneven to keep a rhythm. freq 0 = a rest.
struct Note {
  uint16_t freq;  // Hz
  uint8_t beats;  // how many ALARM_BEAT_MS until the next note starts
};

// Which tune the alarm plays — a compile-time choice:
//   0 = a short original ditty
//   1 = "The Sailor's Hornpipe" (traditional; a.k.a. the College Hornpipe) (default)
// Change the default here, or override it without editing this file by adding
// `-D ALARM_TUNE=0` to build_flags in platformio.ini.
#ifndef ALARM_TUNE
#define ALARM_TUNE 1
#endif

// Note frequencies (equal temperament; S = sharp). Both tunes are pitched to
// sit at or below ~3 kHz, where the piezo buzzer resonates.
enum : uint16_t {
  N_G5 = 784, N_A5 = 880, N_B5 = 988, N_C6 = 1047, N_CS6 = 1109, N_D6 = 1175, N_E6 = 1319, N_FS6 = 1480,
  N_G6 = 1568, N_A6 = 1760, N_B6 = 1976, N_C7 = 2093, N_D7 = 2349, N_E7 = 2637, N_FS7 = 2960, N_G7 = 3136
};

#if ALARM_TUNE == 1
// The Sailor's Hornpipe, in G, two octaves above written pitch. One table row
// per bar; a beat is an eighth note. Both 8-bar parts are played once each
// (no repeats), then a short rest before the whole thing loops.
static const uint16_t ALARM_BEAT_MS = 140;
static const Note alarmTune[] = {
  // --- first part ---
  { N_G6, 1 }, { N_FS6, 1 },
  { N_G6, 2 }, { N_G5, 2 }, { N_G5, 2 }, { N_D6, 1 }, { N_C6, 1 },
  { N_B5, 1 }, { N_D6, 1 }, { N_G6, 1 }, { N_FS6, 1 }, { N_G6, 1 }, { N_B6, 1 }, { N_A6, 1 }, { N_G6, 1 },
  { N_A6, 2 }, { N_A5, 2 }, { N_A5, 2 }, { N_A6, 1 }, { N_G6, 1 },
  { N_FS6, 1 }, { N_A6, 1 }, { N_D7, 2 }, { N_D7, 2 }, { N_E7, 1 }, { N_FS7, 1 },
  { N_G7, 1 }, { N_FS7, 1 }, { N_E7, 1 }, { N_D7, 1 }, { N_E7, 1 }, { N_D7, 1 }, { N_C7, 1 }, { N_B6, 1 },
  { N_C7, 1 }, { N_B6, 1 }, { N_A6, 1 }, { N_G6, 1 }, { N_G6, 1 }, { N_FS6, 1 }, { N_E6, 1 }, { N_D6, 1 },
  { N_E6, 1 }, { N_G6, 1 }, { N_FS6, 1 }, { N_A6, 1 }, { N_G6, 1 }, { N_B6, 1 }, { N_A6, 1 }, { N_C7, 1 },
  { N_B6, 2 }, { N_G6, 2 }, { N_G6, 2 },
  // --- second part ---
  { N_D6, 1 }, { N_C6, 1 },
  { N_B5, 1 }, { N_D6, 1 }, { N_G6, 1 }, { N_D6, 1 }, { N_B5, 1 }, { N_D6, 1 }, { N_G6, 1 }, { N_D6, 1 },
  { N_E6, 2 }, { N_C6, 2 }, { N_C6, 2 }, { N_E6, 1 }, { N_D6, 1 },
  { N_CS6, 1 }, { N_E6, 1 }, { N_A6, 1 }, { N_E6, 1 }, { N_CS6, 1 }, { N_E6, 1 }, { N_A6, 1 }, { N_E6, 1 },
  { N_FS6, 2 }, { N_D6, 2 }, { N_D6, 2 }, { N_A6, 1 }, { N_B6, 1 },
  { N_C7, 1 }, { N_B6, 1 }, { N_C7, 1 }, { N_D7, 1 }, { N_E7, 1 }, { N_D7, 1 }, { N_C7, 1 }, { N_B6, 1 },
  { N_C7, 1 }, { N_B6, 1 }, { N_A6, 1 }, { N_G6, 1 }, { N_G6, 1 }, { N_FS6, 1 }, { N_E6, 1 }, { N_D6, 1 },
  { N_E6, 1 }, { N_G6, 1 }, { N_FS6, 1 }, { N_A6, 1 }, { N_G6, 1 }, { N_B6, 1 }, { N_A6, 1 }, { N_C7, 1 },
  { N_B6, 2 }, { N_G6, 2 }, { N_G6, 2 },
  { 0, 6 },
};
#else
// The original ditty, in G: four short phrases, then a pause.
static const uint16_t ALARM_BEAT_MS = 170;
static const Note alarmTune[] = {
  { N_D6, 1 }, { N_G6, 1 }, { N_B6, 1 }, { N_D7, 2 }, { N_B6, 1 }, { N_D7, 3 }, { 0, 1 },
  { N_E7, 1 }, { N_D7, 1 }, { N_B6, 1 }, { N_G6, 2 }, { N_A6, 1 }, { N_B6, 3 }, { 0, 1 },
  { N_D6, 1 }, { N_G6, 1 }, { N_B6, 1 }, { N_D7, 2 }, { N_B6, 1 }, { N_E7, 3 }, { 0, 1 },
  { N_D7, 1 }, { N_B6, 1 }, { N_A6, 1 }, { N_G6, 2 }, { N_FS6, 1 }, { N_G6, 3 }, { 0, 5 },
};
#endif
static const uint8_t ALARM_TUNE_LEN = sizeof(alarmTune) / sizeof(alarmTune[0]);

static Ticker alarmTicker;
static volatile bool alarmRinging = false;  // true from the alarm firing until a touch
static uint8_t alarmNoteIdx = 0;

// Ticker callback: sound the current note and schedule the next one.
static void playNextAlarmNote() {
  if (!alarmRinging) return;
  const Note &n = alarmTune[alarmNoteIdx];
  alarmNoteIdx = (alarmNoteIdx + 1) % ALARM_TUNE_LEN;
  uint32_t ms = (uint32_t)n.beats * ALARM_BEAT_MS;
  if (n.freq) {
    buzzerTone(n.freq, ms - 25);  // stop a touch early so repeated notes stay distinct
  } else {
    buzzerOff();
  }
  alarmTicker.once_ms(ms, playNextAlarmNote);
}

static void startAlarmTune() {
  alarmNoteIdx = 0;
  alarmRinging = true;
  playNextAlarmNote();
}

static void stopAlarmTune() {
  alarmRinging = false;
  alarmTicker.detach();
  buzzerOff();
}

// Called every loop() iteration. Handles two independent things: the hourly
// chime (always active unless in sleep mode) and the settable alarm (gated by
// disable/activeDays). Both compare against the same `now()` snapshot so they
// can't disagree about what time it currently is mid-function.
void runAlarm() {

  time_t t = now();  // Store the current time atomically

  int hr = hour(t) % 12;
  if (hr == 0) hr = 12;

  // Hourly chime: rings once per hour of the 12-hour clock (e.g. 7 chimes at
  // 7:00), muted only by sleep mode — NOT by the `disable`/alarm settings below —
  // and skipped while the alarm tune is playing, so the two don't talk over each other.
  // This whole block is blocking (delay() calls totalling ~1s per chime), so at
  // 12 o'clock it can tie up loop() for several seconds.
  if (minute(t) == 0 && second(t) == 0 && currentMode() != MODE_SLEEP && !alarmRinging) {
    for (int i = 0; i < hr; i++) {
      buzzerTone(highTone, 150);  // c#7
      delay(300);
      buzzerTone(lowTone, 250);  // b6
      delay(700);
      buzzerOff();
    }
    delay(400);  // prevent reentrance: keeps this from re-triggering within the same second
  }

  if (disable) {  // bail out if alarm is disabled
    return;
  }
  // weekday(t) is 1=Sunday..7=Saturday; activeDays bit 0=Sunday..bit 6=Saturday.
  if (!(activeDays & (1 << (weekday(t) - 1)))) {  // bail out if today isn't an alarm day
    return;
  }
  // process alarm: fires once when the clock reaches the configured hour/minute.
  // runAlarm() runs every loop() iteration, so second() == 0 stays true for many
  // iterations — latch on the triggering minute so we only log/beep once, not
  // once per loop through that whole second.
  static int lastFiredMinuteOfDay = -1;
  int minuteOfDay = hour(t) * 60 + minute(t);
  if (minuteOfDay == runHour * 60 + runMinute && minuteOfDay != lastFiredMinuteOfDay) {
    lastFiredMinuteOfDay = minuteOfDay;
    webPrint("Alarm run at: %d hour and %d minutes\n", runHour, runMinute);
    startAlarmTune();  // loops until a touch calls stopAlarmTune()
  }
}



long unsigned previousTime = 0;
bool initialTimeSynced = false;

void loop() {

  handleWiFi();         // non-blocking wifi connect/reconnect state machine
  // Only poll NTP when actually connected: NTPClient::update() blocks ~1s on a
  // UDP receive timeout when there's no link, which would stall loop() (touch
  // sampling, the alarm check) for the whole duration of a WiFi outage.
  // TimeLib's now() keeps free-running off millis() in the meantime.
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();  // run ntp time client
  }
  runAlarm();           // activate alarm if correct time
  ElegantOTA.loop();    // services any in-progress /update firmware upload

  // Deferred reboot: /api/reboot sets rebootAt instead of calling ESP.restart()
  // inline, so the HTTP response has time to flush to the browser first.
  if (rebootAt != 0 && millis() >= rebootAt) {
    ESP.restart();
  }

  // setup() calls setSyncProvider() before WiFi exists, so that first sync
  // fails and TimeLib won't retry on its own for a full syncInterval. Force
  // one resync as soon as we have a connection so the clock is right at boot.
  if (!initialTimeSynced && currentWifiState == WIFI_STATE_CONNECTED) {
    initialTimeSynced = true;
    setTime(getNtpTime());
  }

  // Each touch steps to the next display mode and silences a ringing alarm.
  // (Counted by sampleTouchPad() on the touchTicker.)
  while (pendingTouches > 0) {
    noInterrupts();
    pendingTouches--;
    interrupts();
    lastTouchMs = millis();
    buttonPressCounter++;        // steps to the next mode (clock/stocks/sleep); see updateOledDisplay()
    stopAlarmTune();             // a touch silences an in-progress alarm
    buzzerTone(2020, 30);  // short beep to confirm the touch registered
    webPrint("Touch #%u: %luns (baseline %luns)\n", buttonPressCounter,
             (unsigned long)touchPad.readingNs(), (unsigned long)touchPad.baselineNs());
    previousTime = 0;  // redraw right away, before any (blocking) stock fetch below
#ifdef TOUCH_DIAG
    webPrint("  raw before press (us): %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
             pressSnap[0] / 1000, pressSnap[1] / 1000, pressSnap[2] / 1000, pressSnap[3] / 1000,
             pressSnap[4] / 1000, pressSnap[5] / 1000, pressSnap[6] / 1000, pressSnap[7] / 1000,
             pressSnap[8] / 1000, pressSnap[9] / 1000, pressSnap[10] / 1000, pressSnap[11] / 1000);
    webPrint("  at %lus uptime, WiFi status %d\n", millis() / 1000, (int)WiFi.status());
#endif
  }
  // Log rises that didn't make it to a press, to show if the pad is set too
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

  if (millis() > previousTime + 500) {  // update display twice per second
    previousTime = millis();
    updateOledDisplay();  // update the physical OLED display
  }

  // Stock mode only: fetch/refresh the index values. Blocks for a few seconds
  // per fetch (TLS handshake), so it runs after the display update above and
  // only while the mode is actually selected. It also waits until the pad has
  // been untouched for 3 s: the handshake has ~0.5 s stretches where even the
  // touch Ticker can't run, long enough to swallow a whole tap, so don't start
  // one while someone is tapping through the modes.
  handleStocks(currentMode() == MODE_STOCKS && millis() - lastTouchMs >= 3000);
#if !defined(ESP32)
  //We don't need to call this explicitly on ESP32 but we do on 8266
  MDNS.update();
#endif
}
