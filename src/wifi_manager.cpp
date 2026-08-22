#include "wifi_manager.h"
#include "config.h"

#if defined(ESP32)
#include <ESPmDNS.h>
#include <WiFi.h>
#else
// esp8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#endif

WifiState currentWifiState = WIFI_STATE_DISCONNECTED;
unsigned long wifiStateTimer = 0;
const unsigned long WIFI_CONNECT_TIMEOUT =
    15000; // 15 seconds connection timeout
const unsigned long WIFI_CHECK_INTERVAL =
    10000; // Check connection integrity every 10 seconds

char IP[] = "xxx.xxx.xxx.xxx"; // IP address string

String stored_ssid;
String stored_pass;
bool ap_mode = true;

#include <Preferences.h>
extern Preferences preferences;
extern void webPrint(const char *format, ...);

const int WIFI_MAX_CONNECT_ATTEMPTS =
    3; // give up on STA and start the access point after this many timeouts
int wifiConnectAttempts = 0;

void startAccessPoint() {
  ap_mode = true;
  Serial.println("\nCreating access point...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  WiFi.softAP(HOSTNAME);
}

void handleWiFi() {
  unsigned long currentMillis = millis();

  switch (currentWifiState) {
  case WIFI_STATE_DISCONNECTED: {
    Serial.println("WiFi: Disconnected. Initiating connection...");
    ap_mode = false;

    stored_ssid = preferences.getString("ssid", "SSID");
    stored_pass = preferences.getString("pass", "PASSWORD");

    // Completely reset Wi-Fi hardware before initiating a new connection to
    // clear any stuck states
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);

#if defined(ESP32)
    WiFi.setHostname(HOSTNAME);
#else
    WiFi.hostname(HOSTNAME);
#endif

    Serial.printf("WiFi: Connecting to SSID '%s'...\n", stored_ssid.c_str());
#if defined(ESP32)
    WiFi.begin(stored_ssid.c_str(), stored_pass.c_str());
#else
    WiFi.begin(stored_ssid, stored_pass);
#endif
    wifiStateTimer = currentMillis;
    currentWifiState = WIFI_STATE_CONNECTING;
    break;
  }

  case WIFI_STATE_CONNECTING: {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi: Connection established successfully!");
      Serial.print("WiFi: IP Address: ");
      Serial.println(WiFi.localIP());
      Serial.print("WiFi: RSSI (Signal Strength): ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");

      IPAddress ip = WiFi.localIP();
      ip.toString().toCharArray(IP, 16);
      webPrint("Wifi up, IP address = %s \n", IP);

      WiFi.setAutoReconnect(false); // we manage reconnects ourselves
      WiFi.persistent(true);
#if defined(ESP32)
      WiFi.setSleep(false); // turn off sleep to improve UI responsiveness
#endif

      if (!MDNS.begin(HOSTNAME)) {
        Serial.println("Error setting up MDNS responder!");
      }
      MDNS.addService("http", "tcp", 80);

      wifiConnectAttempts = 0;
      wifiStateTimer = currentMillis; // reset timer for periodic checks
      currentWifiState = WIFI_STATE_CONNECTED;
    } else if (currentMillis - wifiStateTimer >= WIFI_CONNECT_TIMEOUT) {
      wifiConnectAttempts++;
      if (wifiConnectAttempts >= WIFI_MAX_CONNECT_ATTEMPTS) {
        Serial.println("WiFi: Repeated connection failures. Falling back to access point...");
        wifiConnectAttempts = 0;
        startAccessPoint();
        currentWifiState = WIFI_STATE_AP_MODE;
      } else {
        Serial.println("WiFi: Connection attempt timed out. Retrying...");
        currentWifiState = WIFI_STATE_DISCONNECTED;
      }
    }
    break;
  }

  case WIFI_STATE_CONNECTED: {
    // Periodically check the connection integrity
    if (currentMillis - wifiStateTimer >= WIFI_CHECK_INTERVAL) {
      wifiStateTimer = currentMillis; // reset check timer

      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi: Connection lost! Triggering reconnection...");
        webPrint("WiFi: Connection lost! Triggering reconnection...\n");

        currentWifiState = WIFI_STATE_DISCONNECTED;
      }
    }
    break;
  }

  case WIFI_STATE_AP_MODE: {
    // Idle here until the user saves new credentials, which resets
    // currentWifiState to WIFI_STATE_DISCONNECTED (see SaveWifiDetailsCallback).
    break;
  }
  }
}
