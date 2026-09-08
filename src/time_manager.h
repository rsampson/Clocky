#pragma once

// NTP sync + timezone/DST conversion. TimeLib (the `now()`/`hour()`/etc.
// functions used throughout the firmware) is the actual clock; this module
// just supplies it with UTC time from the network and the local offset to
// apply on top. See getNtpTime() in time_manager.cpp for how the two connect.

#include <Arduino.h>
#include <NTPClient.h>
#include <Timezone.h>
#include <TimeLib.h>

// --- NTP Client ---
extern NTPClient timeClient;

// --- Timezone declarations ---
// One global Timezone object per supported zone, each covering a whole
// region as a stand-in for every city in it (e.g. usET for all of US
// Eastern) — not a full IANA tz database, just enough named choices for the
// dashboard's dropdown. `tz` points at whichever one is currently selected.
extern Timezone *tz;
extern Timezone ausET;
extern Timezone tzMSK;
extern Timezone CE;
extern Timezone UK;
extern Timezone UTC;
extern Timezone usET;
extern Timezone usCT;
extern Timezone usMT;
extern Timezone usAZ;
extern Timezone usPT;
extern Timezone tzIST;
extern Timezone tzJST;
extern Timezone tzCN;
extern Timezone tzGST;
extern Timezone tzSAST;
extern Timezone tzBRT;

extern String Days[];

// --- Function Prototypes ---
void printTZ(Timezone *tzone);
Timezone *TZstringToPointer(String tzstring);
String tzName(Timezone *tzone);
String tzCode(Timezone *tzone);
time_t getNtpTime();
