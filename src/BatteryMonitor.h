// Battery level state machine of the config box (Doc 18 §4.2).
//
// Pure, Arduino-free (natively unit-tested, see test/test_battery). Fed
// with one averaged VBAT sample per poll; returns the warning level:
//   0 = fine or USB powered (reading <= usbV: the divider floats)
//   1 = warn    (<= warnV):    blinking icon in the title bar, no updates
//   2 = replace (<= replaceV): lock screen "Batterie leer"
//
// Stepping DOWN (worse) needs `debounce` consecutive low samples - a
// single sagged reading (radio burst) must not trip the warning.
// Stepping UP (better) needs the reading clearly above threshold + hyst.
// The caller skips feeding samples while the radio transmits for
// seconds at a time (firmware push, SoftAP mode): under that load a
// fresh 5.8 V pack reads 5.2 V and would lock the box for no reason.

#pragma once
#include <stdint.h>

namespace battery {

struct Thresholds {
  float usbV;      // <= : no battery, powered from USB
  float warnV;     // <= : level 1
  float replaceV;  // <= : level 2
  float hystV;     // step up only above threshold + hystV
  uint8_t debounce;  // consecutive low samples before stepping down
};

class Monitor {
 public:
  explicit Monitor(const Thresholds &t) : _t(t) {}

  // Feed one sample; returns the (possibly unchanged) level.
  uint8_t update(float v) {
    if (v <= _t.usbV) {
      _lowRun = 0;
      _level = 0;
      return _level;
    }
    // the level this single reading would justify
    uint8_t want = 0;
    if (v <= _t.replaceV) {
      want = 2;
    } else if (v <= _t.warnV) {
      want = 1;
    }
    if (want > _level) {
      // worse: debounce
      if (++_lowRun >= _t.debounce) {
        _level = want;
        _lowRun = 0;
      }
      return _level;
    }
    _lowRun = 0;
    // better: hysteresis, one step at a time
    if (_level == 2 && v > _t.replaceV + _t.hystV) _level = 1;
    if (_level == 1 && v > _t.warnV + _t.hystV) _level = 0;
    return _level;
  }

  uint8_t level() const { return _level; }

 private:
  Thresholds _t;
  uint8_t _level = 0;
  uint8_t _lowRun = 0;
};

}  // namespace battery
