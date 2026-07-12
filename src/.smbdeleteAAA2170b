// In-RAM list of devices found by the last discovery cycle.
// Stateless by design (Doc 18 §1): cleared on every new discovery, the
// devices' own NVS is the source of truth.

#pragma once
#include <Arduino.h>
#include "InfinitagNow.h"

struct Device {
  bool used = false;
  uint8_t mac[6] = {0};
  uint8_t deviceType = 0;  // inow::DeviceType
  uint8_t id = 0;          // station_id or target_id, 0 = unset ("??")
  inow::DiscoverReply info;
  uint32_t lastSeenMs = 0;
};

class DeviceRegistry {
 public:
  static constexpr size_t MAX_DEVICES = 32;

  void clear(uint8_t deviceType);  // DEV_ANY clears everything

  // Insert or update from a validated DISCOVER_REPLY packet.
  void upsert(const uint8_t mac[6], const inow::Packet &pkt);

  size_t count(uint8_t deviceType) const;

  // n-th device of the given type, ordered by id ascending (unset ids last).
  Device *byIndex(uint8_t deviceType, size_t idx);

  // True if another device of the same type shares this id (id != 0).
  bool isDuplicateId(const Device &d) const;

 private:
  Device _dev[MAX_DEVICES];
};
