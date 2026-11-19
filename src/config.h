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
const int SensorPin = D7;  // this input connects directly to the touch pad
const int DriverPin = D3;  // this output connects to the touch pad through a 300k resistor
const int ledPin = D4;

// --- Alarm tones ---
const int highTone = 2217;  // tones for hour chime
const int lowTone = 1975;

// --- Touch sensor ---
// Sensitivity and filtering are TouchPad::Config defaults (lib/TouchPad), set
// from measurements of this pad. Re-tune with tools/touch_tuner.
