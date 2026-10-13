#pragma once
// Pure C++ (no ESPHome deps) spike filter for the live pH / Redox readings.
//
// A reading is compared with the last NORMAL value (the reference):
//   - |value - reference| <= threshold : normal, published, becomes the new
//     reference (slow drifts are therefore followed);
//   - otherwise it is an outlier and is NOT published. If the readings come
//     back to normal within `max_outliers` consecutive readings, the spike is
//     simply dropped;
//   - if more than `max_outliers` consecutive readings are off, the deviation
//     is real (probe failure, wiring, chemistry...): the alarm turns ON and the
//     readings are published again so the real value is visible. The reference
//     is NOT moved, so the alarm stays ON until the value comes back within
//     `threshold` of the last normal value, or until reset() (e.g. after a probe
//     calibration, when the new level is the expected one).
//
// Values are the raw on-wire integers (pH x100, Redox mV). threshold <= 0
// disables the filter (every reading is published, alarm never set).

#include <cstdint>
#include <cstdlib>

namespace emec_core {

struct OutlierFilter {
  enum Result : uint8_t { PUBLISH, HOLD };

  // ---- configuration --------------------------------------------------------
  int threshold = 0;          // raw units; 0 = disabled
  uint8_t max_outliers = 3;   // consecutive outliers tolerated before the alarm

  // ---- state ----------------------------------------------------------------
  bool has_ref = false;
  int reference = 0;          // last normal value
  uint8_t outliers = 0;       // current run of consecutive outliers
  bool alarm = false;
  uint32_t rejected = 0;      // readings held back since boot (instrumentation)

  Result feed(int value) {
    if (threshold <= 0) return PUBLISH;
    if (!has_ref) {           // first reading after boot / reset: trust it
      has_ref = true;
      reference = value;
      outliers = 0;
      return PUBLISH;
    }
    if (std::abs(value - reference) <= threshold) {
      reference = value;      // back to (or still) normal
      outliers = 0;
      alarm = false;
      return PUBLISH;
    }
    if (alarm) return PUBLISH;   // confirmed deviation: show the real value
    if (outliers < max_outliers) {
      outliers++;
      rejected++;
      return HOLD;
    }
    alarm = true;             // still off after max_outliers readings: real
    return PUBLISH;
  }

  // Forget the reference: the next reading is trusted as the new normal level.
  void reset() {
    has_ref = false;
    outliers = 0;
    alarm = false;
  }
};

}  // namespace emec_core
