#include "DeviceRegistry.h"

void DeviceRegistry::clear(uint8_t deviceType) {
  for (auto &d : _dev)
    if (d.used && (deviceType == inow::DEV_ANY || d.deviceType == deviceType))
      d.used = false;
}

void DeviceRegistry::upsert(const uint8_t mac[6], const inow::Packet &pkt) {
  Device *slot = nullptr;
  for (auto &d : _dev) {
    if (d.used && memcmp(d.mac, mac, 6) == 0) { slot = &d; break; }
  }
  if (!slot) {
    for (auto &d : _dev)
      if (!d.used) { slot = &d; break; }
  }
  if (!slot) return;  // registry full – ignore (32 devices is plenty)

  slot->used = true;
  memcpy(slot->mac, mac, 6);
  slot->deviceType = pkt.device_type;
  slot->id = (pkt.device_type == inow::DEV_STATION) ? pkt.station_id
                                                    : pkt.target_id;
  inow::decodeDiscoverReply(pkt.payload, slot->info);
  slot->lastSeenMs = millis();
}

size_t DeviceRegistry::count(uint8_t deviceType) const {
  size_t n = 0;
  for (const auto &d : _dev)
    if (d.used && d.deviceType == deviceType) n++;
  return n;
}

Device *DeviceRegistry::byIndex(uint8_t deviceType, size_t idx) {
  // Selection by ascending id (0 = unset sorts last), stable for equal ids.
  Device *best = nullptr;
  size_t skip = idx;
  // simple O(n^2) selection – MAX_DEVICES is small
  bool taken[MAX_DEVICES] = {false};
  for (size_t round = 0;; round++) {
    best = nullptr;
    size_t bestIdx = 0;
    for (size_t i = 0; i < MAX_DEVICES; i++) {
      Device &d = _dev[i];
      if (!d.used || d.deviceType != deviceType || taken[i]) continue;
      const uint16_t key = (d.id == 0) ? 0x100 : d.id;
      const uint16_t bestKey =
          (best == nullptr) ? 0xFFFF : ((best->id == 0) ? 0x100 : best->id);
      if (key < bestKey) { best = &d; bestIdx = i; }
    }
    if (!best) return nullptr;  // idx out of range
    if (skip == 0) return best;
    taken[bestIdx] = true;
    skip--;
  }
}

bool DeviceRegistry::isDuplicateId(const Device &d) const {
  if (d.id == 0) return false;
  for (const auto &o : _dev) {
    if (!o.used || &o == &d) continue;
    if (o.deviceType == d.deviceType && o.id == d.id) return true;
  }
  return false;
}
