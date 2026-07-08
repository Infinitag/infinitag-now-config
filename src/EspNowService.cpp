#include "EspNowService.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "Config.h"

EspNowService *EspNowService::s_instance = nullptr;

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool EspNowService::begin(uint8_t channel) {
  s_instance = this;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  // no STA join wanted (Doc 12 §3.8)
  WiFi.setSleep(WIFI_PS_NONE);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  WiFi.macAddress(_ownMac);

  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecvStatic);

  esp_now_peer_info_t bcast = {};
  memcpy(bcast.peer_addr, BCAST, 6);
  bcast.channel = channel;
  bcast.encrypt = false;
  return esp_now_add_peer(&bcast) == ESP_OK;
}

void EspNowService::onRecvStatic(const uint8_t *mac, const uint8_t *data,
                                 int len) {
  EspNowService *self = s_instance;
  if (!self) return;
  if (!inow::validate(data, (size_t)len)) return;  // wrong system / corrupt

  portENTER_CRITICAL(&self->_mux);
  const size_t next = (self->_head + 1) % RX_RING;
  if (next != self->_tail) {  // drop packet if ring is full
    RxPacket &slot = self->_ring[self->_head];
    memcpy(slot.mac, mac, 6);
    memcpy(&slot.pkt, data, sizeof(inow::Packet));
    self->_head = next;
  }
  portEXIT_CRITICAL(&self->_mux);
}

bool EspNowService::receive(RxPacket &out) {
  bool have = false;
  portENTER_CRITICAL(&_mux);
  if (_tail != _head) {
    out = _ring[_tail];
    _tail = (_tail + 1) % RX_RING;
    have = true;
  }
  portEXIT_CRITICAL(&_mux);
  return have;
}

bool EspNowService::ensurePeer(const uint8_t mac[6]) {
  if (esp_now_is_peer_exist(mac)) {
    for (auto &p : _peers)
      if (p.used && memcmp(p.mac, mac, 6) == 0) p.lastUse = millis();
    return true;
  }

  // Find a free slot, otherwise evict the least recently used peer.
  Peer *slot = nullptr;
  for (auto &p : _peers)
    if (!p.used) { slot = &p; break; }
  if (!slot) {
    Peer *oldest = &_peers[0];
    for (auto &p : _peers)
      if (p.lastUse < oldest->lastUse) oldest = &p;
    esp_now_del_peer(oldest->mac);
    oldest->used = false;
    slot = oldest;
  }

  esp_now_peer_info_t info = {};
  memcpy(info.peer_addr, mac, 6);
  info.channel = cfg::ESPNOW_CHANNEL;
  info.encrypt = false;
  if (esp_now_add_peer(&info) != ESP_OK) return false;

  memcpy(slot->mac, mac, 6);
  slot->lastUse = millis();
  slot->used = true;
  return true;
}

bool EspNowService::send(const uint8_t mac[6], inow::Packet &p) {
  if (!ensurePeer(mac)) return false;
  inow::seal(p);
  return esp_now_send(mac, reinterpret_cast<uint8_t *>(&p), sizeof(p)) ==
         ESP_OK;
}

bool EspNowService::sendBroadcast(inow::Packet &p) {
  inow::seal(p);
  return esp_now_send(BCAST, reinterpret_cast<uint8_t *>(&p), sizeof(p)) ==
         ESP_OK;
}
