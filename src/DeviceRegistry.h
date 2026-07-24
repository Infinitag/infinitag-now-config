// In-RAM list of devices found by the last discovery cycle.
// Stateless by design (Doc 18 §1): cleared on every new discovery, the
// devices' own NVS is the source of truth. Since protocol v0x02 the MAC is
// the only identity; lists are ordered by MAC for a stable display.

#pragma once
#include <Arduino.h>
#include "InfinitagNow.h"

struct Device {
  bool used = false;
  uint8_t mac[6] = {0};
  uint8_t deviceType = 0;  // inow::DeviceType
  uint8_t proto = 0;       // protocol version of the reply; foreign versions
                           //   arrive via the rescue anchor (PROTOCOL.md)
  inow::DiscoverReply info;
  uint32_t lastSeenMs = 0;

  bool foreignProto() const { return proto != inow::PROTOCOL_VERSION; }
};

class DeviceRegistry {
 public:
  static constexpr size_t MAX_DEVICES = 32;

  void clear(uint8_t deviceType);  // DEV_ANY clears everything

  // Insert or update from a validated DISCOVER_REPLY packet.
  void upsert(const uint8_t mac[6], const inow::Packet &pkt);

  size_t count(uint8_t deviceType) const;

  // n-th device of the given type, ordered by MAC ascending.
  Device *byIndex(uint8_t deviceType, size_t idx);

  // Highest firmware version key (major<<16|minor<<8|patch) seen for the
  // type, 0 if no device of that type is known.
  uint32_t maxVersionKey(uint8_t deviceType) const;

  static uint32_t versionKey(const inow::DiscoverReply &info) {
    return ((uint32_t)info.fw_major << 16) | ((uint32_t)info.fw_minor << 8) |
           info.fw_patch;
  }

 private:
  Device _dev[MAX_DEVICES];
};
