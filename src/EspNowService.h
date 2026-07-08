// Thin ESP-NOW wrapper: init, broadcast/unicast send with automatic peer
// registration (LRU cache, Doc 12 §3.8), and a small RX ring buffer so the
// WiFi-task receive callback never touches UI state directly.

#pragma once
#include <Arduino.h>
#include "InfinitagNow.h"

struct RxPacket {
  uint8_t mac[6];
  inow::Packet pkt;
};

class EspNowService {
 public:
  bool begin(uint8_t channel);

  // Seals (CRC) and sends. Registers the peer if needed.
  bool send(const uint8_t mac[6], inow::Packet &p);
  bool sendBroadcast(inow::Packet &p);

  // Pops one received (and already CRC-validated) packet. False if empty.
  bool receive(RxPacket &out);

  const uint8_t *ownMac() const { return _ownMac; }

 private:
  static void onRecvStatic(const uint8_t *mac, const uint8_t *data, int len);
  bool ensurePeer(const uint8_t mac[6]);

  static EspNowService *s_instance;

  // RX ring buffer, written from the WiFi task, read from loop().
  static constexpr size_t RX_RING = 8;
  RxPacket _ring[RX_RING];
  volatile size_t _head = 0, _tail = 0;
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

  // LRU peer cache (broadcast peer is permanent and not tracked here).
  static constexpr size_t MAX_PEERS = 18;
  struct Peer {
    uint8_t mac[6];
    uint32_t lastUse = 0;
    bool used = false;
  };
  Peer _peers[MAX_PEERS];

  uint8_t _ownMac[6] = {0};
};
