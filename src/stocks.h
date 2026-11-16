#pragma once

// Stock-index mode: fetches the Dow, S&P 500 and Nasdaq Composite from Yahoo
// Finance's chart JSON endpoint over HTTPS. See stocks.cpp for the details
// (and the caveats — it's an unofficial endpoint, and each fetch blocks
// loop() for a few seconds).

#include <Arduino.h>

struct StockIndex {
  const char *label;   // short name shown on the OLED / dashboard
  const char *symbol;  // URL-encoded Yahoo symbol, e.g. "%5EDJI" for ^DJI
  float price;         // last regular-market price (last close when the market is shut)
  float prevClose;     // previous session's close, for the up/down indicator
};

inline constexpr int NUM_STOCKS = 3;
extern StockIndex stocks[NUM_STOCKS];

// Call every loop(). `wanted` = the clock is currently in stock mode; nothing
// is fetched (so no blocking, no heap use) while it's false or WiFi is down.
void handleStocks(bool wanted);

// True when all three values were fetched recently enough to be worth
// showing. When false, the display falls back to the plain clock.
bool stocksFresh();
