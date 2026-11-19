#include "TouchPad.h"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// --- Platform helpers: a fine-grained clock and fast pin access -------------
// On ESP8266/ESP32 the CPU cycle counter gives 12.5 ns (80 MHz) resolution,
// versus 1 us from micros(); untouched readings are only ~9 us, so that matters.
#if defined(ESP8266) || defined(ESP32)
static inline uint32_t ticks() { return ESP.getCycleCount(); }
static inline uint32_t ticksPerUs() { return ESP.getCpuFreqMHz(); }
#else
static inline uint32_t ticks() { return micros(); }
static inline uint32_t ticksPerUs() { return 1; }
#endif

static inline bool readPin(uint8_t pin) {
#if defined(ESP8266)
  if (pin < 16) return GPI & (1UL << pin);  // direct register read: much faster than digitalRead()
#endif
  return digitalRead(pin) == HIGH;
}

static inline void setPinHigh(uint8_t pin) {
#if defined(ESP8266)
  if (pin < 16) { GPOS = 1UL << pin; return; }
#endif
  digitalWrite(pin, HIGH);
}

// --- TouchPad ----------------------------------------------------------------

TouchPad::TouchPad(uint8_t driverPin, uint8_t sensorPin, const Config &cfg)
    : _driverPin(driverPin), _sensorPin(sensorPin), _cfg(cfg) {
  _cfg.windowSamples = constrain(_cfg.windowSamples, 1, MAX_WINDOW);
  // A dead pad (all timeouts) must be flagged stuck before those timeouts,
  // which count as long readings, could confirm a press.
  if (_cfg.stuckChecks >= _cfg.confirmChecks) _cfg.stuckChecks = _cfg.confirmChecks - 1;
  if (_cfg.stuckChecks < 1) _cfg.stuckChecks = 1;
}

void TouchPad::begin() {
  pinMode(_driverPin, OUTPUT);
  digitalWrite(_driverPin, LOW);
  pinMode(_sensorPin, INPUT);

  // Fill the window (one full mains cycle) and take its median as the
  // starting baseline. 0 if the pad isn't responding; update() learns it later.
  for (uint8_t i = 0; i < _cfg.windowSamples; i++) {
    _window[i] = windowValue(measureNs());
    delay(_cfg.sampleMs);
  }
  _winCount = _cfg.windowSamples;
  _winPos = 0;
  const uint32_t m = windowMedian();
  setBaseline(m == windowValue(0) ? 0 : m);  // mostly timeouts: learn it in update() instead
  _lastSampleMs = millis();
}

// IRAM_ATTR: the timed loop must run from internal RAM. From flash it can hit
// an instruction-cache miss mid-measurement (likely whenever other code ran
// since the last reading), which adds a fixed ~4 us (+45 %) to the reading
// and was enough to cause phantom presses.
uint32_t IRAM_ATTR TouchPad::measureNs() {
  // Discharge the pad completely: driver low, and the sensor pin briefly
  // driving the pad low directly.
  digitalWrite(_driverPin, LOW);
  pinMode(_sensorPin, OUTPUT);
  digitalWrite(_sensorPin, LOW);
  delayMicroseconds(_cfg.settleUs);
  pinMode(_sensorPin, INPUT);

  // Charge through the resistor and time it. Interrupts are off so a timer or
  // Wi-Fi interrupt can't stretch the measurement; the timeout bounds how long
  // they stay off.
  const uint32_t limit = (uint32_t)_cfg.timeoutUs * ticksPerUs();
  uint32_t elapsed;
  noInterrupts();
  const uint32_t start = ticks();
  setPinHigh(_driverPin);
  do {
    elapsed = ticks() - start;
  } while (!readPin(_sensorPin) && elapsed <= limit);
  interrupts();

  if (elapsed > limit) return 0;
  return (uint64_t)elapsed * 1000 / ticksPerUs();
}

// Median of the readings in the window.
uint32_t TouchPad::windowMedian() const {
  uint32_t v[MAX_WINDOW];
  const uint8_t n = _winCount;
  for (uint8_t i = 0; i < n; i++) {  // insertion sort: n is tiny
    uint32_t x = _window[i];
    uint8_t j = i;
    while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; }
    v[j] = x;
  }
  return v[n / 2];
}

bool TouchPad::update() {
  const uint32_t now = millis();
  if (now - _lastSampleMs < _cfg.sampleMs) return false;
  _lastSampleMs = now;

  const uint32_t raw = measureNs();
  _window[_winPos] = windowValue(raw);
  _winPos = (_winPos + 1) % _cfg.windowSamples;
  if (_winCount < _cfg.windowSamples) _winCount++;

  const bool pressed = decide(raw, now);
  if (_hook) _hook(raw, _filteredNs, _touched);
  return pressed;
}

bool TouchPad::decide(uint32_t raw, uint32_t now) {
  // A run of timeouts means the pad isn't responding (stuck pin, broken
  // wire): untouched, never a touch.
  if (raw == 0) {
    if (_timeoutRun < 255) _timeoutRun++;
  } else {
    _timeoutRun = 0;
  }
  if (stuck()) {
    _touched = false;
    _aboveRun = 0;
    _filteredNs = 0;
    return false;
  }

  const uint32_t r = windowMedian();
  _filteredNs = r;

  // No baseline yet (pad wasn't responding at begin()): learn it now.
  if (_baselineNs == 0) {
    if (r != windowValue(0)) setBaseline(r);  // not while the window is mostly timeouts
    return false;
  }

  const uint32_t pressAt = _baselineNs + _baselineNs * _cfg.pressPercent / 100;
  const uint32_t releaseAt = _baselineNs + _baselineNs * _cfg.releasePercent / 100;

  // Raised for too long to be a finger: re-learn the baseline from here.
  if (r < releaseAt) {
    _highSinceMs = 0;
  } else if (_highSinceMs == 0) {
    _highSinceMs = now | 1;  // never 0 while armed
  } else if (now - _highSinceMs >= _cfg.relearnMs) {
    setBaseline(r);
    _touched = false;
    _aboveRun = 0;
    _highSinceMs = 0;
    _peakNs = 0;
    return false;
  }

  if (_touched) {
    if (r < releaseAt) _touched = false;
    return false;
  }

  if (r > pressAt) {
    if (++_aboveRun >= _cfg.confirmChecks) {
      _touched = true;
      _aboveRun = 0;
      _peakNs = 0;
      return true;  // the moment of touching
    }
  } else {
    _aboveRun = 0;
  }

  // Near-miss bookkeeping: remember the peak while the reading is up, and
  // when it falls back without a press, count it if the peak got past half
  // the press level.
  if (r >= releaseAt) {
    if (r > _peakNs) _peakNs = r;
    return false;
  }
  if (_peakNs > _baselineNs + _baselineNs * _cfg.pressPercent / 200) {
    _nearMisses++;
    _lastNearMissNs = _peakNs;
  }
  _peakNs = 0;

  // Clearly untouched: let the baseline follow slow drift. It falls quickly
  // (in case begin() ran while the pad was being touched) and rises slowly
  // (so a slowly approaching finger can't drag it up).
  // Kept in fixed point (x256) so slow tracking still moves by fractions of
  // a ns per reading; in plain ns, small differences would round to no
  // movement at all.
  const uint32_t rq = r << 8;
  if (rq < _baselineQ8) _baselineQ8 -= (_baselineQ8 - rq) >> 4;
  else _baselineQ8 += (rq - _baselineQ8) >> _cfg.baselineShift;
  _baselineNs = _baselineQ8 >> 8;
  return false;
}
