#pragma once

// Owns the web dashboard + JSON REST API. See web_server.cpp for the actual
// routes and web_assets.cpp for the HTML/CSS/JS served at "/".

#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;  // shared so ElegantOTA.begin(&server) (in Clocky.cpp) can attach /update to it

void setUpWebServer();
