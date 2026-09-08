#pragma once

// Owns the web dashboard + JSON REST API. See web_server.cpp for the actual
// routes and web_assets.cpp for the HTML/CSS/JS served at "/".

#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;  // shared so ElegantOTA.begin(&server) (in Clocky.cpp) can attach /update to it

// millis() deadline for a deferred reboot, or 0 if none pending. handleReboot()
// sets this (instead of calling ESP.restart() inline) so the HTTP response can
// flush first; loop() in Clocky.cpp does the actual restart once it's reached.
extern volatile unsigned long rebootAt;

void setUpWebServer();
