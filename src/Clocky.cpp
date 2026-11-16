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
#include <Arduino.h>

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);   // SDA connects to D2, SCL  to D1 on the Wemos mini esp8266

bool buttonState;  // debounced touch-pad state: HIGH while the pad is being touched
//bool lastButtonState = LOW;  // the previous reading from the input pin
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

//Function Prototypes
void runAlarm(void);
bool readTouch(void);

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

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(DriverPin, OUTPUT);
  pinMode(SensorPin, INPUT);
  pinMode(ledPin, OUTPUT);

  tone(BUZZER_PIN, highTone, 1000);  // audible boot beep

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

unsigned int deltaTime;  // last measured touch-pad RC rise time, in microseconds (see readTouch())

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
      //sprintf(buf, "%3d", deltaTime);  // hack to debug touch switch
      break;
    case 4:  // month + day, e.g. "Aug 22"
      display.setFont(&FreeSans12pt7b);
      display.setCursor(12, 25);
      sprintf(buf, " %s %2d", monthShortStr(month()), day());
      break;
  }
}

// Stock mode's face: each index gets 3 seconds — its name for 1 second, then
// its value for 2 — so the three indexes rotate every 9 seconds, in the same
// spirit as the clock's day/time/date rotation. Values are rounded to whole
// points (5 digits is all that fits at this font size) with a small
// up/down triangle at the right showing direction versus the previous close.
static void prepStockFace(char *buf) {
  unsigned int slot = now() % (NUM_STOCKS * 3);
  const StockIndex &s = stocks[slot / 3];
  bool showValue = (slot % 3) != 0;

  if (showValue) {
    sprintf(buf, "%ld", lroundf(s.price));
  } else {
    sprintf(buf, "%s", s.label);
  }

  // Centre the text in the space left of the triangle (value) or on the
  // whole screen (name).
  const int arrowW = 12;
  int avail = showValue ? SCREEN_WIDTH - arrowW - 2 : SCREEN_WIDTH;
  int16_t x1, y1;
  uint16_t w, h;
  display.setFont(&FreeSans18pt7b);
  display.getTextBounds(buf, 0, 28, &x1, &y1, &w, &h);
  int x = (avail - (int)w) / 2 - x1;
  display.setCursor(x < 0 ? 0 : x, 28);

  if (showValue && s.price != s.prevClose) {
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
// from loop(), because loop() only comes round every ~200ms (touch sampling)
// and stalls for seconds at a time (hourly chime, stock fetch) — far too
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
    tone(BUZZER_PIN, n.freq, ms - 25);  // stop a touch early so repeated notes stay distinct
  } else {
    noTone(BUZZER_PIN);
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
  noTone(BUZZER_PIN);
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
      tone(BUZZER_PIN, highTone, 150);  // c#7
      delay(300);
      tone(BUZZER_PIN, lowTone, 250);  // b6
      delay(700);
      noTone(BUZZER_PIN);
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



// There's no capacitive-touch library or dedicated hardware here — this is a
// homemade RC-delay trick. DriverPin feeds the touch pad through a 300k
// resistor, and SensorPin reads the pad directly. A finger touching the pad
// adds body capacitance, which slows how fast the pad's voltage rises when
// DriverPin switches from low to high. So: pull everything low, then drive
// high and time (in a tight busy-wait loop, using micros()) how long SensorPin
// takes to read HIGH. A slow rise (>= treshold, from config.h) means touched.
bool readTouch(void) {
  // Discharge the pad fully before timing, so every measurement starts from
  // the same known (low) voltage.
  digitalWrite(DriverPin, LOW);
  pinMode(SensorPin, OUTPUT);
  digitalWrite(SensorPin, LOW);
  delay(10);

  // Now drive the pad high through the resistor and time how long SensorPin
  // takes to follow.
  digitalWrite(DriverPin, HIGH);
  pinMode(SensorPin, INPUT);
  unsigned int bailOutCounter = 0;
  unsigned int timerVal = micros();
  while (digitalRead(SensorPin) != HIGH && bailOutCounter < 1000) { bailOutCounter++; }  // time how long it takes for the input to follow the output
  if (bailOutCounter == 1000) return (true);  // pin never went high — treat as touched (fail-safe, not fail-silent)
  deltaTime = micros() - timerVal;
  //Serial.println(deltaTime);       // debug
  return (deltaTime >= treshold);   // If it takes longer than the treshold time to go high, declair that the pad is touched
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

  // Touch-pad debouncing: readTouch() is a noisy analog-ish measurement, so
  // instead of trusting one reading, sample it 10 times (100ms total) and go
  // with whichever result was seen more than half the time (majority vote).
  uint16_t trueSum = 0;
  for (int i = 0; i < 10; i++) {
    if (readTouch() == true) trueSum++;
    delay(10);
  }
  bool reading = (trueSum > 6 ? HIGH : LOW);

  // Edge-triggered: only act when the debounced state actually changes, and
  // only count/beep on the LOW->HIGH transition (the moment of touching),
  // not on release.
  if (reading != buttonState) {
    buttonState = reading;
    if (buttonState == HIGH) {
      buttonPressCounter++;  // steps to the next mode (clock/stocks/sleep); see updateOledDisplay()
      stopAlarmTune();             // a touch silences an in-progress alarm
      tone(BUZZER_PIN, 2020, 30);  // short beep to confirm the touch registered
      delay(30);
      webPrint("Button press count: %02d\n", buttonPressCounter);
      previousTime = 0;  // redraw right away, before any (blocking) stock fetch below
    }
  }
  // end button processing

  if (millis() > previousTime + 500) {  // update display twice per second
    previousTime = millis();
    updateOledDisplay();  // update the physical OLED display
  }

  // Stock mode only: fetch/refresh the index values. Blocks for a few seconds
  // per fetch (TLS handshake), so it runs after the display update above and
  // only while the mode is actually selected.
  handleStocks(currentMode() == MODE_STOCKS);
#if !defined(ESP32)
  //We don't need to call this explicitly on ESP32 but we do on 8266
  MDNS.update();
#endif
}
