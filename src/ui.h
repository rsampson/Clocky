#pragma once

#include <Arduino.h>

// Handles for all ESPUI controls, grouped so callbacks/modules can refer to
// them as ui.xxx instead of a scatter of loose globals.
struct UI {
  uint16_t wifi_ssid_text, wifi_pass_text;
  uint16_t debugLabel, timeLabel, signalLabel, bootLabel, timeZoneLabel;
  uint16_t mainSwitcher, mainText, mainTime, hourNumber, minuteNumber;
  uint16_t timeZoneSelect;
};

extern UI ui;
