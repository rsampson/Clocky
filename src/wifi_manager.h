#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

// Wi-Fi State Machine states. See handleWiFi() in wifi_manager.cpp for the
// transitions between them: DISCONNECTED -> CONNECTING -> CONNECTED is the
// happy path; repeated CONNECTING timeouts fall back to AP_MODE instead of
// retrying forever.
enum WifiState {
  WIFI_STATE_DISCONNECTED,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_AP_MODE
};

// --- WiFi Global Interfaces ---
extern String stored_ssid;
extern String stored_pass;
extern bool ap_mode;
extern WifiState currentWifiState;

// --- WiFi Actions ---
void handleWiFi();

#endif // WIFI_MANAGER_H
