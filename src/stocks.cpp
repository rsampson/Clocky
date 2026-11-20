#include "stocks.h"
#include "clocky.h"  // webPrint()

#if defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#else
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <umm_malloc/umm_heap_select.h>
#endif

// Source: Yahoo Finance's chart endpoint. It's unofficial/undocumented, but
// it needs no API key, and with range=1d&interval=1d the whole reply is only
// ~1.3 KB of JSON. It rejects requests without a browser-like User-Agent
// (HTTP 429), and is HTTPS-only.
static const char STOCK_HOST[] = "query1.finance.yahoo.com";
static const char STOCK_USER_AGENT[] = "Mozilla/5.0";

static const uint32_t REFRESH_MS = 5UL * 60UL * 1000UL;   // refetch interval while in stock mode
static const uint32_t RETRY_MS = 60UL * 1000UL;           // wait after a failed fetch

#if !defined(ESP32)
// TLS receive buffer. Yahoo doesn't negotiate smaller TLS fragments, so this
// has to be the full 16 KB record size; the transmit side only carries our
// tiny GET requests.
static const size_t TLS_RX_BUF = 16384;
static const size_t TLS_TX_BUF = 512;
static const size_t TLS_RX_ALLOC = TLS_RX_BUF + 325 + 16;  // what the core really allocates: + TLS overhead + heap rounding
// Main-heap room BearSSL needs besides the receive buffer: its private 6.2 KB
// stack plus the client/X.509 contexts. Checked up front because running out
// mid-handshake aborts (reboots) the ESP8266 rather than failing politely.
static const size_t TLS_OTHER_BLOCK = 7000;   // largest single piece
static const size_t TLS_OTHER_TOTAL = 14000;  // all of it, with some slack
#endif

static const uint32_t STALE_MS = 15UL * 60UL * 1000UL;    // older than this => fall back to the clock

StockIndex stocks[NUM_STOCKS] = {
  { "Dow", "%5EDJI", 0, 0 },
  { "S&P", "%5EGSPC", 0, 0 },
  { "Nasdaq", "%5EIXIC", 0, 0 },
};

static bool haveData = false;      // at least one complete successful fetch
static uint32_t lastGoodMs = 0;    // millis() of that fetch
static uint32_t nextFetchMs = 0;   // millis() before which we won't try again
static bool fetchScheduled = false;  // false = never tried, fetch as soon as wanted
static int8_t lastResult = -1;     // -1 none yet, 0 failed, 1 ok — so we only log changes

// Records a fetch outcome, logging the message only when the outcome changes
// (so a failure that repeats every minute logs once). Returns ok.
static bool reportResult(bool ok, const char *fmt, ...) {
  if (lastResult != (int8_t)ok) {
    char msg[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    webPrint("%s", msg);
  }
  lastResult = ok;
  return ok;
}

// Pulls the number following `"key":` out of the JSON text. The reply is
// small and flat enough that a full JSON parse (and its extra heap) isn't
// worth it. The leading quote in the search key matters: it stops
// "regularMarketPrice" matching inside some longer field name.
static bool extractNumber(const String &body, const char *key, float &out) {
  int i = body.indexOf(key);
  if (i < 0) return false;
  out = body.substring(i + strlen(key), i + strlen(key) + 16).toFloat();
  return out > 0;
}

// One blocking pass over all three symbols, sharing a single TLS connection
// (keep-alive), since the handshake is by far the slowest and hungriest part.
// Values are only published if every symbol succeeded, so the display never
// shows a mix of fresh and stale numbers.
static bool fetchAll() {
  WiFiClientSecure client;
  // No certificate validation: this is display-only public data, and pinning
  // a cert/CA would break the feature whenever Yahoo rotates it.
  client.setInsecure();
#if !defined(ESP32)
  size_t txBuf = TLS_TX_BUF;
  size_t rxInMainHeap = TLS_RX_ALLOC;  // main-heap bytes the receive buffer will take

#if defined(MMU_IRAM_HEAP)
  // This project builds with the "2nd heap in IRAM" MMU option, and BearSSL
  // tries that small IRAM heap first for its I/O buffers, falling back to
  // the main heap. That's ideal for the 16 KB receive buffer (the IRAM heap
  // is otherwise idle, and it keeps the main heap unfragmented) — but the
  // ESP8266 core has a trap: right after each buffer it makes a 16-byte
  // bookkeeping allocation (a shared_ptr control block) in the same heap
  // with a *throwing* new. If a buffer leaves the IRAM heap with fewer than
  // ~24 bytes free, that allocation fails and the device aborts and reboots.
  // With this firmware's IRAM usage, RX + TX buffers fit with only ~16 bytes
  // to spare, i.e. exactly that case. So steer the allocations:
  struct IramFiller {
    void *p = nullptr;
    ~IramFiller() {
      HeapSelectIram iram;
      free(p);
    }
  } filler;
  {
    HeapSelectIram iram;
    const size_t slack = 64;  // room for the two bookkeeping allocations
    size_t blk = ESP.getMaxFreeBlockSize();
    if (blk >= TLS_RX_ALLOC + slack) {
      // RX buffer goes in IRAM. If the TX buffer wouldn't also fit with
      // room to spare, enlarge it so it clearly doesn't fit and falls back
      // to the main heap cleanly.
      rxInMainHeap = 0;
      size_t rem = blk - TLS_RX_ALLOC;
      if (rem < TLS_TX_BUF + 85 + slack) {
        txBuf = max(TLS_TX_BUF, rem + slack);
      }
    } else if (blk > 2048) {
      // RX buffer can't safely go in IRAM: temporarily fill most of the IRAM
      // heap so it's refused there outright and lands in the main heap.
      filler.p = malloc(blk - 2048);
    }
  }
#endif

  if (ESP.getMaxFreeBlockSize() < max(rxInMainHeap, TLS_OTHER_BLOCK) ||
      ESP.getFreeHeap() < rxInMainHeap + TLS_OTHER_TOTAL) {
    return reportResult(false, "Stocks: not enough memory (%u free, %u block)\n",
                        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxFreeBlockSize());
  }
  client.setBufferSizes(TLS_RX_BUF, txBuf);
#endif
  client.setTimeout(5000);

  HTTPClient http;
  http.setReuse(true);
  http.setTimeout(5000);
  http.setUserAgent(STOCK_USER_AGENT);

  float price[NUM_STOCKS], prev[NUM_STOCKS];
  bool ok = true;
  int code = 0;

  for (int i = 0; i < NUM_STOCKS && ok; i++) {
    String url = String(F("https://")) + STOCK_HOST + F("/v8/finance/chart/") + stocks[i].symbol + F("?interval=1d&range=1d");
    if (!http.begin(client, url)) {
      ok = false;
      break;
    }
    code = http.GET();
    if (code == HTTP_CODE_OK) {
      String body = http.getString();
      ok = extractNumber(body, "\"regularMarketPrice\":", price[i]);
      if (ok && !extractNumber(body, "\"chartPreviousClose\":", prev[i])) {
        prev[i] = price[i];  // no previous close => just show no movement
      }
      if (!ok) code = -100;  // got a reply but couldn't find the price in it
    } else {
      ok = false;
    }
    http.end();  // with setReuse(true) this keeps the connection open for the next symbol
    yield();
  }
  client.stop();

  if (!ok) return reportResult(false, "Stocks: fetch failed (%d), heap %u\n", code, (unsigned)ESP.getFreeHeap());

  for (int i = 0; i < NUM_STOCKS; i++) {
    stocks[i].price = price[i];
    stocks[i].prevClose = prev[i];
  }
  haveData = true;
  lastGoodMs = millis();
  return reportResult(true, "Stocks: Dow %.0f S&P %.0f Nasdaq %.0f\n", stocks[0].price, stocks[1].price, stocks[2].price);
}

bool stocksFresh() {
  return haveData && (millis() - lastGoodMs) < STALE_MS;
}

void handleStocks(bool wanted) {
  if (!wanted || WiFi.status() != WL_CONNECTED) return;
  // Signed difference so this stays correct across the 49-day millis() wrap.
  if (fetchScheduled && (int32_t)(millis() - nextFetchMs) < 0) return;

  bool ok = fetchAll();
  nextFetchMs = millis() + (ok ? REFRESH_MS : RETRY_MS);
  fetchScheduled = true;
}
