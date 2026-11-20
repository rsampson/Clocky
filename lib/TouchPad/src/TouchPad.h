// TouchPad — a touch button made from a metal pad, one resistor and two GPIOs.
//
// Wiring:   driverPin ---[ R ~300k ]---+--- pad
//           sensorPin -----------------+
//
// How it works: discharge the pad to 0 V, drive driverPin HIGH, and time how
// long the pad takes to charge through R until sensorPin reads HIGH. A finger
// adds capacitance, so the rise takes longer. That time is compared against a
// slowly self-adjusting "untouched" baseline, so humidity, temperature and
// power-supply changes don't need a re-tune.
//
// Reliability measures:
//   * Mains hum: on an ungrounded supply (a 2-prong wall adapter) a nearby
//     finger couples 50/60 Hz into the pad and the reading swings up and down
//     with it — which made a hovering finger "press" repeatedly. So readings
//     are taken one every sampleMs and the decision uses the median of the
//     last windowSamples of them, spanning a full mains cycle (>= 20 ms), so
//     the hum averages out. The median also outvotes single readings spoiled
//     by an interrupt (the ESP8266 Wi-Fi NMI can't be masked).
//   * A press needs confirmChecks filtered values in a row above the press
//     level (~45 ms), and is only released below a lower release level
//     (hysteresis). On wall power, Wi-Fi activity (e.g. right after it
//     connects) produced 15-30 ms bursts of 50-1000 us readings with nobody
//     touching; real taps keep the reading up for 150-400 ms.
//   * If the reading stays above the release level for relearnMs (an object
//     left next to the pad, a hand resting on the case), that level becomes
//     the new baseline, so the pad can't be latched "held" forever.
//   * Timeouts: with a finger on the pad on an ungrounded supply, mains hum
//     can hold the pad below the input threshold for part of each cycle, so
//     single readings time out mid-touch. An isolated timeout therefore
//     counts as a very long reading (a strong touch). Only a sustained run of
//     them (stuckChecks, ~36 ms: stuck pin, broken wire) means "not
//     responding", which counts as untouched, never as a touch; stuck()
//     reports it. stuckChecks < confirmChecks, so a pad that fails while
//     running is flagged stuck before its timeouts could confirm a press.
//
// Usage:
//   TouchPad pad(D3, D7);
//   setup(): pad.begin();               // keep fingers off for ~25 ms
//   loop():  if (pad.update()) { ... }  // true once, at the moment of touching
//
// Defaults were measured on Clocky (ESP8266, 300k, ~20 pF pad): untouched
// ~9 us; light fingertip +50..120 %; firm touch +500 %; finger hovering 1 cm
// away +3..9 % on USB power. On an ungrounded 2-prong wall adapter (worse
// plug orientation) everything is bigger: hovering 0.5-2 cm up to +26 %,
// grazes +51 %, taps +184..228 %; pressPercent = 35 sits in that gap. In
// the other orientation, touches mix in timeouts (see above). USB
// taps went as low as +59 %. See tools/touch_tuner and tools/touch_watch.py.
#pragma once

#include <Arduino.h>

class TouchPad {
 public:
  static const uint8_t MAX_WINDOW = 15;  // upper limit for Config::windowSamples

  struct Config {
    uint16_t settleUs = 100;      // time to fully discharge the pad before each reading
    uint16_t timeoutUs = 1000;    // a reading longer than this is a fault (largest real touch seen: ~165 us)
    uint8_t sampleMs = 3;         // one reading per this many ms ...
    uint8_t windowSamples = 7;    // ... filtered by the median of this many (7 x 3 ms = 21 ms >= one 50 Hz cycle)
    uint8_t pressPercent = 35;    // touched when filtered > baseline * (1 + pressPercent/100) ...
    uint8_t confirmChecks = 15;   // ... for this many readings in a row (~45 ms: rejects Wi-Fi bursts, taps last 150+ ms)
    uint8_t releasePercent = 8;   // released when filtered < baseline * (1 + releasePercent/100)
    uint8_t baselineShift = 10;   // baseline rises toward untouched readings by 1/2^shift per reading (~3 s); falls 16x faster
    uint8_t stuckChecks = 12;     // this many timeouts in a row => stuck() (real touches: <= 5); kept < confirmChecks
    uint32_t relearnMs = 30000;   // filtered above the release level this long => it becomes the baseline
  };

  TouchPad(uint8_t driverPin, uint8_t sensorPin) : TouchPad(driverPin, sensorPin, Config()) {}
  TouchPad(uint8_t driverPin, uint8_t sensorPin, const Config &cfg);

  // Configures the pins and learns the untouched baseline.
  void begin();

  // Call as often as you like (at most one reading per sampleMs, ~10-170 us).
  // Returns true exactly once per touch, at the moment the touch is detected.
  bool update();

  bool touched() const { return _touched; }
  bool stuck() const { return _timeoutRun >= _cfg.stuckChecks; }
  uint32_t readingNs() const { return _filteredNs; }   // filtered (median) reading; 0 while stuck()
  uint32_t baselineNs() const { return _baselineQ8 >> 8; }

  // Near misses: a rise past half the press level that fell back without
  // becoming a press. A steady trickle of these means the pad is set too
  // insensitive (or something is brushing it). For tuning/logging.
  uint32_t nearMisses() const { return _nearMisses; }
  uint32_t lastNearMissNs() const { return _lastNearMissNs; }  // peak of the latest one

  // Optional: called with every raw reading and the filtered value (for diagnostics).
  void onSample(void (*hook)(uint32_t rawNs, uint32_t filteredNs, bool touched)) { _hook = hook; }

  // One raw reading in nanoseconds (0 = timed out). Exposed for tuning tools.
  uint32_t measureNs();

 private:
  uint32_t windowMedian() const;
  bool decide(uint32_t raw, uint32_t now);  // ported offline in tools/touch_watch.py replay(); keep in sync
  uint32_t windowValue(uint32_t raw) const { return raw ? raw : (uint32_t)_cfg.timeoutUs * 1000; }
  void setBaseline(uint32_t ns) { _baselineQ8 = ns << 8; }

  uint8_t _driverPin, _sensorPin;
  Config _cfg;
  void (*_hook)(uint32_t, uint32_t, bool) = nullptr;

  uint32_t _window[MAX_WINDOW] = {};
  uint8_t _winPos = 0;
  uint8_t _winCount = 0;

  bool _touched = false;
  uint8_t _aboveRun = 0;
  uint8_t _timeoutRun = 0;
  uint32_t _filteredNs = 0;
  uint32_t _baselineQ8 = 0;   // baseline in 1/256 ns (fixed point, so slow tracking still moves); 0 = not learned
  uint32_t _lastSampleMs = 0;
  uint32_t _peakNs = 0;       // highest filtered value since it rose above the release level (untouched only)
  uint32_t _nearMisses = 0;
  uint32_t _lastNearMissNs = 0;
  uint32_t _highSinceMs = 0;  // when filtered last rose above the release level (0 = it's below)
};
