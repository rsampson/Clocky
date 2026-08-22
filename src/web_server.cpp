#include "web_server.h"
#include "web_assets.h"
#include "clocky.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <TimeLib.h>

#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

AsyncWebServer server(80);

// Streams a JsonDocument out as the HTTP response body, instead of
// serializing to a String first — avoids an extra full-size copy of the JSON.
static void sendJson(AsyncWebServerRequest *request, JsonDocument &doc) {
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  serializeJson(doc, *response);
  request->send(response);
}

// GET /api/status — polled by the dashboard once per second (see app.js's
// refresh()). Returns a snapshot of everything the UI needs to redraw itself:
// current time/timezone, signal strength, alarm state/schedule, Wi-Fi/AP
// status, and the debug log tail. Doesn't take any input and never mutates
// state.
static void handleStatus(AsyncWebServerRequest *request) {
  DynamicJsonDocument doc(1536);

  char timeBuf[20];
  time_t t = now();
  sprintf(timeBuf, "%02d:%02d:%02d %02d/%02d", hour(t), minute(t), second(t), month(t), day(t));
  doc["time"] = timeBuf;
  doc["timezone"] = tzName(tz);
  doc["timezoneCode"] = tzCode(tz);
  doc["rssi"] = WiFi.RSSI();
  doc["disabled"] = disable;
  doc["runHour"] = runHour;
  doc["runMinute"] = runMinute;
  doc["activeDays"] = activeDays;
  doc["weekday"] = weekday(t);  // 1=Sunday..7=Saturday; used by app.js's describeNextAlarm()
  doc["ssid"] = stored_ssid;
  doc["apMode"] = ap_mode;

  fetchDebugText();
  doc["log"] = charBuf;

  sendJson(request, doc);
}

// POST /api/alarm { disable: bool } — the Alarm tab's on/off switch. Only
// mutes the settable alarm; the hourly chime is controlled separately by
// sleep mode (the touch pad), not by this flag.
static void handleAlarm(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  bool disableAlarm = body["disable"] | false;
  disable = disableAlarm;
  preferences.putBool("disable", disable);
  request->send(200, "text/plain", "ok");
}

// POST /api/schedule { hour, minute, activeDays } — Alarm tab's "Save Alarm
// Time" button. activeDays is the bit 0=Sunday..bit 6=Saturday mask described
// in clocky.h; the `| runHour`/etc. fallbacks mean a field can be omitted from
// the request body to leave that setting unchanged.
static void handleSchedule(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();

  runHour = body["hour"] | runHour;
  runMinute = body["minute"] | runMinute;
  activeDays = body["activeDays"] | activeDays;
  stored_hour = String(runHour);
  stored_minute = String(runMinute);
  preferences.putString("hour", stored_hour);
  preferences.putString("minute", stored_minute);
  preferences.putUChar("activeDays", activeDays);

  webPrint("Saved %s alarm hour: %d and minute: %d\n", disable ? "disabled" : "active", runHour, runMinute);
  request->send(200, "text/plain", "ok");
}

// POST /api/wifi { ssid, pass } — Setup tab. Doesn't connect right away;
// just saves the credentials and flips the Wi-Fi state machine back to
// DISCONNECTED so handleWiFi() (in wifi_manager.cpp, driven from loop())
// picks them up and attempts a fresh connection on its own.
static void handleWifi(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  stored_ssid = body["ssid"] | stored_ssid;
  stored_pass = body["pass"] | stored_pass;
  preferences.putString("ssid", stored_ssid);
  preferences.putString("pass", stored_pass);

  // apply immediately and let handleWiFi() reconnect with the new credentials
  currentWifiState = WIFI_STATE_DISCONNECTED;
  request->send(200, "text/plain", "ok");
}

// POST /api/timezone { tz } — Setup tab's time zone dropdown. `tz` is one of
// the short codes from TZstringToPointer() (e.g. "EST", "UTC"). Re-applying
// setTime(getNtpTime()) immediately re-renders the already-known UTC time
// through the new zone, so the display doesn't wait for the next NTP sync.
static void handleTimezone(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  String tzstring = body["tz"] | "UTC";
  tz = TZstringToPointer(tzstring);
  preferences.putString("tz", tzstring);
  printTZ(tz);
  setTime(getNtpTime());
  request->send(200, "text/plain", "ok");
}

// POST /api/settime { epoch } — only meaningful in AP mode (no internet, so no
// NTP). The dashboard's JS calls this once per page load with the browser's
// own clock, giving the device a rough time to display while it's
// unconfigured. Ignored once the device is on real Wi-Fi (ap_mode false), so
// a stale/wrong browser clock can't override a good NTP sync.
static void handleSetTime(AsyncWebServerRequest *request, JsonVariant &json) {
  JsonObject body = json.as<JsonObject>();
  unsigned long epoch = body["epoch"] | 0UL;
  if (epoch > 0 && ap_mode) {
    setTime(epoch);
  }
  request->send(200, "text/plain", "ok");
}

// POST /api/reboot — Setup tab's Reboot button. Restarts immediately, with no
// delay; the HTTP response may or may not make it back to the browser before
// the device actually resets.
static void handleReboot(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "rebooting");
  ESP.restart();
}

// Small helper so each POST route below doesn't have to repeat this cast.
// AsyncCallbackJsonWebHandler parses the request body as JSON and hands the
// parsed JsonVariant to fn — that's what lets handlers above just do
// json.as<JsonObject>() instead of parsing the raw body themselves.
static AsyncCallbackJsonWebHandler *jsonHandler(const char *uri, ArJsonRequestHandlerFunction fn) {
  return new AsyncCallbackJsonWebHandler(uri, fn);
}

// Registers every route and starts listening. Called once from setup().
// ElegantOTA.begin(&server) is called separately, right after this, to add
// the /update page onto this same server instance.
void setUpWebServer() {
  // Static dashboard assets — served straight from PROGMEM (web_assets.cpp),
  // no filesystem/LittleFS involved. send_P() reads directly from flash.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/css", STYLE_CSS);
  });
  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "application/javascript", APP_JS);
  });

  // Plain GET/POST routes with no JSON request body to parse.
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/reboot", HTTP_POST, handleReboot);

  // POST routes whose body is JSON; jsonHandler() wraps each handler so it
  // receives an already-parsed JsonVariant instead of a raw body string.
  server.addHandler(jsonHandler("/api/alarm", handleAlarm));
  server.addHandler(jsonHandler("/api/schedule", handleSchedule));
  server.addHandler(jsonHandler("/api/wifi", handleWifi));
  server.addHandler(jsonHandler("/api/timezone", handleTimezone));
  server.addHandler(jsonHandler("/api/settime", handleSetTime));

  server.begin();
}
