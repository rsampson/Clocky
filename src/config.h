#pragma once

#include <Arduino.h>

// Tested on ESP32 Wemos Lolin32 and ESP12-F (esp8266), make sure these match
// your board, otherwise strange results will occur.

#define HOSTNAME "clocky"

// --- Display ---
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 32  // OLED display height, in pixels
#define SSD1306_NO_SPLASH

// --- Pins ---
const int BUZZER_PIN = D6;
const int BUZZER_PIN_N = D5;  // GPIO14: complement of BUZZER_PIN, for differential piezo drive
                              // (piezo between D6 and D5 instead of D6 and GND; ESP8266 only)
const int SensorPin = D7;  // this input connects directly to the touch pad
const int DriverPin = D3;  // this output connects to the touch pad through a 300k resistor
const int ledPin = D4;

// --- Beeper tones ---
// The piezo only responds over about one octave; every tone and alarm-tune
// note must stay inside it (checked at compile time in Clocky.cpp).
const int BEEPER_MIN_HZ = 1500;
const int BEEPER_MAX_HZ = 3000;
const int highTone = 2217;     // hourly chime, C#7 (also the boot beep)
const int lowTone = 1975;      // hourly chime, B6
const int touchBlipHz = 2020;  // short blip confirming a touch

// --- Touch sensor ---
// Sensitivity and filtering are TouchPad::Config defaults (lib/TouchPad), set
// from measurements of this pad. Re-tune with tools/touch_tuner.
