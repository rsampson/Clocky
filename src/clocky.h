#pragma once

#include "wifi_manager.h"
#include "time_manager.h"
#include "ui.h"
#include <Preferences.h>

#include <CircularBuffer.hpp>

inline constexpr size_t bufferSize = 400;

// --- Shared Utility Objects ---
extern Preferences preferences;
extern CircularBuffer<float, 24> dayBuffer;
extern char charBuf[bufferSize];

extern String stored_hour;
extern String stored_minute;
extern uint16_t runHour;
extern uint16_t runMinute;
extern bool disable;

void fetchDebugText();
void setUpUI();
void updateOledDisplay();
void webPrint(const char *format, ...);

// Callback Prototypes
struct Control;  // Forward declare ESPUI Control struct
void generalCallback(Control *sender, int type);
void textCallback(Control *sender, int type);
void hourCallback(Control *sender, int type);
void minuteCallback(Control *sender, int type);
void switchCallback(Control *sender, int type);
void SaveWifiDetailsCallback(Control *sender, int type);
void SaveScheduleCallback(Control *sender, int type);
void TZcallback(Control *sender, int type);
void ESPReset(Control *sender, int type);
