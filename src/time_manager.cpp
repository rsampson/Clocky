#include "time_manager.h"
#include <WiFiUdp.h>

// --- UDP and NTP Client instances ---
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// --- Timezone Rules and Definitions ---
//
// Each TimeChangeRule is { abbreviation, week-of-month, day-of-week, month,
// hour, offset-from-UTC-in-minutes } describing when a zone's clock changes
// (e.g. "2nd Sunday in March at 2am, then UTC-4"). A Timezone with two rules
// observes DST (one rule for standard time, one for daylight time, applied
// automatically based on the current date); a Timezone with one rule doesn't
// observe DST at all — see the Timezone library's docs for the full API.

// Australia Eastern Time Zone (Sydney, Melbourne)
TimeChangeRule aEDT = {"AEDT", First, Sun, Oct, 2, 660}; // UTC + 11 hours
TimeChangeRule aEST = {"AEST", First, Sun, Apr, 3, 600}; // UTC + 10 hours
Timezone ausET(aEDT, aEST);

// Moscow Standard Time (MSK, does not observe DST)
TimeChangeRule msk = {"MSK", Last, Sun, Mar, 1, 180};
Timezone tzMSK(msk);

// Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120}; // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};  // Central European Standard Time
Timezone CE(CEST, CET);

// United Kingdom (London, Belfast)
TimeChangeRule BST = {"BST", Last, Sun, Mar, 1, 60}; // British Summer Time
TimeChangeRule GMT = {"GMT", Last, Sun, Oct, 2, 0};  // Standard Time
Timezone UK(BST, GMT);

// UTC
TimeChangeRule utcRule = {"UTC", Last, Sun, Mar, 1, 0}; // UTC
Timezone UTC(utcRule);

// US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240}; // Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};  // Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);

// US Central Time Zone (Chicago, Houston)
TimeChangeRule usCDT = {"CDT", Second, Sun, Mar, 2, -300};
TimeChangeRule usCST = {"CST", First, Sun, Nov, 2, -360};
Timezone usCT(usCDT, usCST);

// US Mountain Time Zone (Denver, Salt Lake City)
TimeChangeRule usMDT = {"MDT", Second, Sun, Mar, 2, -360};
TimeChangeRule usMST = {"MST", First, Sun, Nov, 2, -420};
Timezone usMT(usMDT, usMST);

// Arizona is US Mountain Time Zone but does not use DST
Timezone usAZ(usMST);

// US Pacific Time Zone (Las Vegas, Los Angeles)
TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};
TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};
Timezone usPT(usPDT, usPST);

// India Standard Time (Kolkata, Mumbai, Delhi) - no DST, UTC + 5:30
TimeChangeRule istRule = {"IST", Last, Sun, Mar, 1, 330};
Timezone tzIST(istRule);

// Japan Standard Time (Tokyo) - no DST, UTC + 9
TimeChangeRule jstRule = {"JST", Last, Sun, Mar, 1, 540};
Timezone tzJST(jstRule);

// China Standard Time (Beijing, Shanghai) - no DST, UTC + 8
TimeChangeRule cnstRule = {"CNST", Last, Sun, Mar, 1, 480};
Timezone tzCN(cnstRule);

// Gulf Standard Time (Dubai, Abu Dhabi) - no DST, UTC + 4
TimeChangeRule gstRule = {"GST", Last, Sun, Mar, 1, 240};
Timezone tzGST(gstRule);

// South Africa Standard Time (Johannesburg) - no DST, UTC + 2
TimeChangeRule sastRule = {"SAST", Last, Sun, Mar, 1, 120};
Timezone tzSAST(sastRule);

// Brazil (Sao Paulo) - no DST since 2019, UTC - 3
TimeChangeRule brtRule = {"BRT", Last, Sun, Mar, 1, -180};
Timezone tzBRT(brtRule);

Timezone *tz = &UTC;  // the user's currently selected zone; changed via TZstringToPointer()

// --- Global Days array ---
String Days[] = {"Undefined", "Sunday", "Monday", "Tuesday",
                 "Wednesday", "Thursday", "Friday", "Saturday"};

// --- Helper functions ---

// This is TimeLib's sync-provider callback (wired up via setSyncProvider() in
// Clocky.cpp's setup()): TimeLib calls this whenever it decides it needs a
// fresh time, and adopts whatever time_t comes back (unless it's 0, meaning
// "no time available yet"). timeClient.getEpochTime() returns UTC seconds
// since 1970 from the last successful NTP fetch (0 if there's never been
// one); toLocal() applies the selected Timezone's offset/DST rule on top.
time_t getNtpTime(void) {
  time_t serv_time = tz->toLocal(timeClient.getEpochTime());
  return (serv_time);
}

// tzName()/tzCode()/TZstringToPointer() are three views of the same mapping
// between a Timezone object and how it's shown/stored in the web UI: tzName()
// gives the human-readable label (e.g. "Eastern Standard Time") for the
// status card, tzCode() gives the short code (e.g. "EST") used as the
// dropdown's <option value>, and TZstringToPointer() (below) does the
// reverse, turning a saved/submitted code back into the Timezone* to use.
// Comparing by pointer (== &usET) works because each Timezone is a single
// global instance — there's exactly one usET in the whole program.
String tzName(Timezone *tzone) {
  String TZS;

  if (tzone == &ausET)
    TZS = "Australia Eastern";
  else if (tzone == &tzMSK)
    TZS = "Moscow";
  else if (tzone == &CE)
    TZS = "Central European";
  else if (tzone == &UK)
    TZS = "British Standard";
  else if (tzone == &UTC)
    TZS = "Universal";
  else if (tzone == &usET)
    TZS = "Eastern Standard";
  else if (tzone == &usCT)
    TZS = "Central Standard";
  else if (tzone == &usMT)
    TZS = "Mountain Standard";
  else if (tzone == &usAZ)
    TZS = "Arizona";
  else if (tzone == &usPT)
    TZS = "Pacific Standard";
  else if (tzone == &tzIST)
    TZS = "India Standard";
  else if (tzone == &tzJST)
    TZS = "Japan Standard";
  else if (tzone == &tzCN)
    TZS = "China Standard";
  else if (tzone == &tzGST)
    TZS = "Gulf Standard";
  else if (tzone == &tzSAST)
    TZS = "South Africa Standard";
  else if (tzone == &tzBRT)
    TZS = "Brasilia";
  else
    TZS = "Unknown TZ";

  return TZS + " Time";
}

String tzCode(Timezone *tzone) {
  if (tzone == &ausET)
    return "AEST";
  else if (tzone == &tzMSK)
    return "MSK";
  else if (tzone == &CE)
    return "CE";
  else if (tzone == &UK)
    return "GMT";
  else if (tzone == &usET)
    return "EST";
  else if (tzone == &usCT)
    return "CST";
  else if (tzone == &usMT)
    return "MST";
  else if (tzone == &usAZ)
    return "AZT";
  else if (tzone == &usPT)
    return "PST";
  else if (tzone == &tzIST)
    return "IST";
  else if (tzone == &tzJST)
    return "JST";
  else if (tzone == &tzCN)
    return "CNST";
  else if (tzone == &tzGST)
    return "GST";
  else if (tzone == &tzSAST)
    return "SAST";
  else if (tzone == &tzBRT)
    return "BRT";
  else
    return "UTC";
}

void printTZ(Timezone *tzone) {
  Serial.println(tzName(tzone));
}

// Called when a saved/user-submitted time zone code (matching the values in
// web_assets.cpp's <select id="timezone"> options) needs to become the actual
// Timezone* used for conversions. Falls back to UTC for anything unrecognized.
Timezone *TZstringToPointer(String tzstring) {
  if (tzstring == "AEST")
    return (&ausET);
  else if (tzstring == "MSK")
    return (&tzMSK);
  else if (tzstring == "CE")
    return (&CE);
  else if (tzstring == "GMT")
    return (&UK);
  else if (tzstring == "UTC")
    return (&UTC);
  else if (tzstring == "EST")
    return (&usET);
  else if (tzstring == "CST")
    return (&usCT);
  else if (tzstring == "MST")
    return (&usMT);
  else if (tzstring == "AZT")
    return (&usAZ);
  else if (tzstring == "PST")
    return (&usPT);
  else if (tzstring == "IST")
    return (&tzIST);
  else if (tzstring == "JST")
    return (&tzJST);
  else if (tzstring == "CNST")
    return (&tzCN);
  else if (tzstring == "GST")
    return (&tzGST);
  else if (tzstring == "SAST")
    return (&tzSAST);
  else if (tzstring == "BRT")
    return (&tzBRT);
  else {
    Serial.println("Bad TZ selection");
    return (&UTC);
  }
}
