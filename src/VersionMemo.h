// Persistent "highest firmware version ever seen" per device type.
//
// Stage-1 upgrade of the outdated-marker ('^' in the device lists): the
// plain heuristic compares only against the best version currently in
// the list and is therefore blind when a single device of a type is
// around. This memo remembers the best version the box has EVER seen
// (NVS), so a lone station keeps its marker once any newer firmware
// was in the air. Stage 2 (exact target version from a firmware image
// carried by the box) comes with the ESP-NOW OTA, see Doc 18 §12.
//
// Writes hit the NVS only when a version increases – rare enough that
// flash wear is a non-issue. Deliberate downgrades leave a stale
// marker; Tools -> "Versions-Memo Reset" clears the memo.

#pragma once
#include <Arduino.h>

class VersionMemo {
 public:
  void load();

  // Record a seen version key (major<<16|minor<<8|patch); persists if
  // it is a new maximum for the type.
  void note(uint8_t deviceType, uint32_t versionKey);

  // Highest version key ever seen for the type, 0 = none.
  uint32_t maxKey(uint8_t deviceType) const;

  void clear();

 private:
  static int slot(uint8_t deviceType);  // -1 = type not tracked
  uint32_t _max[2] = {0, 0};            // [0] = station, [1] = target
};
