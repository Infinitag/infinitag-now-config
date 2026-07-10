#include "UiController.h"

#include <esp_system.h>  // esp_random()

#include "Config.h"

using namespace inow;

// ---------------------------------------------------------------------------
// menu definitions
// ---------------------------------------------------------------------------

static const char *MAIN_ITEMS[] = {"Stationen", "Targets", "Live-Monitor",
                                   "Web-UI", "Tools"};
static constexpr uint8_t MAIN_COUNT = 5;

static const char *STATION_ITEMS[] = {"Liste", "Neue Station (Stab)",
                                      "Sound testen"};
static constexpr uint8_t STATION_COUNT = 3;

UiController::UiController(EspNowService &net, DeviceRegistry &reg,
                           InputController &in)
    : _net(net), _reg(reg), _in(in),
      _oled(U8G2_R0, /* reset=*/U8X8_PIN_NONE) {}

void UiController::begin() {
  _oled.begin();
  _oled.setFont(u8g2_font_6x10_tf);
  gotoScreen(SCR_MAIN);
}

// ---------------------------------------------------------------------------
// screen transitions
// ---------------------------------------------------------------------------

void UiController::gotoScreen(Screen s) {
  _screen = s;
  _cursor = 0;
  _valueEditing = false;
  _dirty = true;

  switch (s) {
    case SCR_DEVICE_LIST:
      startDiscovery(_listType);
      break;
    case SCR_SETUP_WAIT:
      beginSetupMode();
      break;
    default:
      break;
  }
}

void UiController::startDiscovery(uint8_t deviceType) {
  _reg.clear(deviceType);
  _discoverToken = (uint8_t)(esp_random() & 0xFF);
  if (_discoverToken == 0) _discoverToken = 1;

  Packet p;
  init(p, MSG_DISCOVER_REQ, DEV_CONFIG_BOX);  // header type = sender type
  p.token = _discoverToken;
  p.payload[0] = deviceType;  // filter (Doc 12 §3.5)
  _net.sendBroadcast(p);
}

void UiController::sendIdentify(const Device &d) {
  Packet p;
  init(p, MSG_IDENTIFY, d.deviceType);
  if (d.deviceType == DEV_STATION) p.station_id = d.id;
  if (d.deviceType == DEV_TARGET) p.target_id = d.id;
  p.payload[0] = cfg::IDENTIFY_DURATION_100MS;
  Packet copy = p;
  _net.send(d.mac, copy);
}

void UiController::beginSetupMode() {
  _setupStartedMs = millis();
  _lastSetupTxMs = 0;  // forces immediate send in handleTimers()
  _setupResult[0] = '\0';
}

// ---------------------------------------------------------------------------
// edit screen setup
// ---------------------------------------------------------------------------

void UiController::enterEdit(Device &d) {
  _editDev = d;
  _fieldCount = 0;
  _statusLine[0] = '\0';
  _awaitingAck = false;

  auto add = [&](const char *label, int32_t val, int32_t mn, int32_t mx,
                 int32_t st, const char *unit = nullptr, bool bits = false) {
    if (_fieldCount >= MAX_FIELDS) return;
    _fields[_fieldCount++] = Field{label, val, mn, mx, st, unit, bits};
  };

  if (d.deviceType == DEV_STATION) {
    StationConfig c;
    decodeStationConfig(d.info.config_blob, d.info.config_blob_len, c);
    add("ID", c.station_id, 1, 99, 1);
    add("Volume", c.volume_pct, 0, 100, 1, "%");
    add("Setup-Snd", c.default_setup_sound, 0, cfg::SOUND_ID_MAX, 1);
  } else {
    TargetConfig c;
    decodeTargetConfig(d.info.config_blob, d.info.config_blob_len, c);
    add("Target-ID", c.target_id, 1, 99, 1);
    add("Station", c.station_id, 1, 99, 1);
    add("Sound", c.sound_id, 0, cfg::SOUND_ID_MAX, 1);
    add("Hit-Time", c.hit_time_ms, 100, 60000, 100, "ms");
    add("Cooldown", c.cooldown_ms, 0, 60000, 100, "ms");
    add("SW-Anim", c.sw_animation, 0, 1, 1);
    add("SW-Kanal", c.sw_channels, 0, 7, 1, nullptr, true);
  }
  gotoScreen(SCR_DEVICE_EDIT);
}

// ---------------------------------------------------------------------------
// radio actions
// ---------------------------------------------------------------------------

void UiController::sendCfgWrite() {
  Packet p;
  init(p, MSG_CFG_WRITE, _editDev.deviceType);
  p.flags = FLAG_ACK_REQUIRED;

  if (_editDev.deviceType == DEV_STATION) {
    StationConfig c;
    c.station_id = (uint8_t)_fields[0].value;
    c.volume_pct = (uint8_t)_fields[1].value;
    c.default_setup_sound = (uint8_t)_fields[2].value;
    p.station_id = c.station_id;
    encodeStationConfig(c, p.payload);
  } else {
    TargetConfig c;
    c.target_id = (uint8_t)_fields[0].value;
    c.station_id = (uint8_t)_fields[1].value;
    c.sound_id = (uint8_t)_fields[2].value;
    c.hit_time_ms = (uint16_t)_fields[3].value;
    c.cooldown_ms = (uint16_t)_fields[4].value;
    c.sw_animation = (uint8_t)_fields[5].value;
    c.sw_channels = (uint8_t)_fields[6].value;
    p.target_id = c.target_id;
    p.station_id = c.station_id;
    encodeTargetConfig(c, p.payload);
  }

  if (_net.send(_editDev.mac, p)) {
    _awaitingAck = true;
    _ackDeadline = millis() + cfg::ACK_TIMEOUT_MS;
    snprintf(_statusLine, sizeof(_statusLine), "Speichere...");
  } else {
    snprintf(_statusLine, sizeof(_statusLine), "Sendefehler!");
  }
  _dirty = true;
}

void UiController::sendTestSound() {
  Packet p;
  init(p, MSG_CFG_TEST_SOUND, DEV_STATION);
  p.station_id = _editDev.id;
  p.payload[0] = _testSound;
  _net.send(_editDev.mac, p);
}

// ---------------------------------------------------------------------------
// packet handling (called from loop for every received packet)
// ---------------------------------------------------------------------------

void UiController::onPacket(const RxPacket &rx) {
  const Packet &p = rx.pkt;

  switch (p.msg_type) {
    case MSG_DISCOVER_REPLY:
      // token echo protects against replies to a stale discovery; token 0
      // is tolerated for early stub firmwares.
      if (p.token == _discoverToken || p.token == 0) {
        _reg.upsert(rx.mac, p);
        if (_screen == SCR_DEVICE_LIST) _dirty = true;
      }
      break;

    case MSG_CFG_ACK:
      if (_awaitingAck && memcmp(rx.mac, _editDev.mac, 6) == 0) {
        _awaitingAck = false;
        if (p.payload[0] == ACK_OK) {
          snprintf(_statusLine, sizeof(_statusLine), "Gespeichert");
        } else {
          snprintf(_statusLine, sizeof(_statusLine), "NACK Code %u",
                   p.payload[0]);
        }
        _dirty = true;
      }
      break;

    case MSG_SETUP_TAKE:
      if (_screen == SCR_SETUP_WAIT && _setupResult[0] == '\0') {
        snprintf(_setupResult, sizeof(_setupResult), "Station %02u gesetzt",
                 p.payload[0]);
        _dirty = true;
      }
      break;

    case MSG_HIT_REPORT: {
      // ring buffer, newest first
      for (size_t i = MONITOR_RING - 1; i > 0; i--) _hits[i] = _hits[i - 1];
      _hits[0] = HitEntry{(uint32_t)millis(), p.target_id, p.station_id,
                          p.payload[0]};
      if (_hitCount < MONITOR_RING) _hitCount++;
      if (_screen == SCR_LIVE_MONITOR) _dirty = true;
      break;
    }

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// input handling
// ---------------------------------------------------------------------------

static int32_t clampVal(int32_t v, int32_t mn, int32_t mx) {
  return v < mn ? mn : (v > mx ? mx : v);
}

void UiController::handleInput() {
  const int delta = _in.takeEncoderDelta();
  const bool push = _in.takePress(BTN_ENC) || _in.takePress(BTN_K4);
  const bool back = _in.takePress(BTN_K1);
  const bool k2 = _in.takePress(BTN_K2);
  const bool k3 = _in.takePress(BTN_K3);
  const int32_t stepMul = _in.isHeld(BTN_K2) ? 10 : 1;

  if (delta == 0 && !push && !back && !k2 && !k3) return;
  _dirty = true;

  switch (_screen) {
    case SCR_MAIN:
      if (delta) _cursor = (uint8_t)clampVal(_cursor + delta, 0, MAIN_COUNT - 1);
      if (push) {
        switch (_cursor) {
          case 0: gotoScreen(SCR_STATION_MENU); break;
          case 1:
            _listType = DEV_TARGET;
            _listPurpose = LIST_EDIT;
            gotoScreen(SCR_DEVICE_LIST);
            break;
          case 2: gotoScreen(SCR_LIVE_MONITOR); break;
          case 3: gotoScreen(SCR_NOTICE); break;  // Web-UI ab V0.2
          case 4: gotoScreen(SCR_TOOLS_INFO); break;
        }
      }
      break;

    case SCR_STATION_MENU:
      if (delta)
        _cursor = (uint8_t)clampVal(_cursor + delta, 0, STATION_COUNT - 1);
      if (push) {
        switch (_cursor) {
          case 0:
            _listType = DEV_STATION;
            _listPurpose = LIST_EDIT;
            gotoScreen(SCR_DEVICE_LIST);
            break;
          case 1: gotoScreen(SCR_SETUP_WAIT); break;
          case 2:
            _listType = DEV_STATION;
            _listPurpose = LIST_SOUND_TEST;
            gotoScreen(SCR_DEVICE_LIST);
            break;
        }
      }
      if (back) gotoScreen(SCR_MAIN);
      break;

    case SCR_DEVICE_LIST: {
      const int count = (int)_reg.count(_listType);
      if (delta && count > 0) {
        _cursor = (uint8_t)clampVal(_cursor + delta, 0, count - 1);
        _lastIdentifyMs = 0;  // identify the new cursor entry immediately
      }
      if (k3) _identifyEnabled = !_identifyEnabled;
      if (k2) startDiscovery(_listType);  // manual refresh
      if (push && count > 0) {
        Device *d = _reg.byIndex(_listType, _cursor);
        if (d) {
          if (_listPurpose == LIST_EDIT) {
            enterEdit(*d);
          } else {
            _editDev = *d;  // sound test: remember chosen station
            gotoScreen(SCR_SOUND_TEST);
          }
        }
      }
      if (back)
        gotoScreen(_listType == DEV_STATION ? SCR_STATION_MENU : SCR_MAIN);
      break;
    }

    case SCR_DEVICE_EDIT: {
      if (_awaitingAck) break;  // ignore input while waiting for the ACK
      const int rows = _fieldCount + 1;  // + "Speichern"
      if (_valueEditing) {
        Field &f = _fields[_cursor];
        if (delta) f.value = clampVal(f.value + delta * f.step * stepMul,
                                      f.min, f.max);
        if (push) _valueEditing = false;             // apply
        if (back) { f.value = _valueBackup; _valueEditing = false; }  // cancel
      } else {
        if (delta) _cursor = (uint8_t)clampVal(_cursor + delta, 0, rows - 1);
        if (push) {
          if (_cursor < _fieldCount) {
            _valueBackup = _fields[_cursor].value;
            _valueEditing = true;
          } else {
            sendCfgWrite();
          }
        }
        if (back) gotoScreen(SCR_DEVICE_LIST);
      }
      break;
    }

    case SCR_SETUP_WAIT:
      if (back || (push && _setupResult[0] != '\0'))
        gotoScreen(SCR_STATION_MENU);
      break;

    case SCR_SOUND_TEST:
      if (delta)
        _testSound =
            (uint8_t)clampVal(_testSound + delta * stepMul, 0,
                              cfg::SOUND_ID_MAX);
      if (push) sendTestSound();
      if (back) gotoScreen(SCR_STATION_MENU);
      break;

    case SCR_LIVE_MONITOR:
    case SCR_TOOLS_INFO:
    case SCR_NOTICE:
      if (back || push) gotoScreen(SCR_MAIN);
      break;
  }
}

// ---------------------------------------------------------------------------
// periodic timers
// ---------------------------------------------------------------------------

void UiController::handleTimers() {
  const uint32_t now = millis();

  // identify blink refresh (Doc 18 §7)
  if (_screen == SCR_DEVICE_LIST && _identifyEnabled &&
      now - _lastIdentifyMs >= cfg::IDENTIFY_PERIOD_MS) {
    Device *d = _reg.byIndex(_listType, _cursor);
    if (d) sendIdentify(*d);
    _lastIdentifyMs = now;
  }

  // setup mode: rebroadcast + timeout (Doc 18 §8)
  if (_screen == SCR_SETUP_WAIT && _setupResult[0] == '\0') {
    if (now - _lastSetupTxMs >= cfg::SETUP_REBROADCAST_MS) {
      Packet p;
      init(p, MSG_SETUP_BEGIN, DEV_STATION);
      p.payload[0] = cfg::SETUP_TIMEOUT_S;
      _net.sendBroadcast(p);
      _lastSetupTxMs = now;
    }
    if (now - _setupStartedMs >= (uint32_t)cfg::SETUP_TIMEOUT_S * 1000UL) {
      snprintf(_setupResult, sizeof(_setupResult), "Timeout!");
      _dirty = true;
    }
  }

  // CFG_ACK timeout
  if (_awaitingAck && now >= _ackDeadline) {
    _awaitingAck = false;
    snprintf(_statusLine, sizeof(_statusLine), "Keine Antwort!");
    _dirty = true;
  }

  // live monitor ages change the displayed seconds
  if (_screen == SCR_LIVE_MONITOR && _hitCount > 0) _dirty = true;
}

void UiController::tick() {
  handleInput();
  handleTimers();

  const uint32_t now = millis();
  if (_dirty && now - _lastRenderMs >= cfg::RENDER_INTERVAL_MS) {
    render();
    _lastRenderMs = now;
    _dirty = false;
  }
}

// ---------------------------------------------------------------------------
// rendering
// ---------------------------------------------------------------------------

void UiController::macSuffix(const uint8_t mac[6], char *out) {
  snprintf(out, 7, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

// Two-color panel: rows 0..15 are yellow, rows 16..63 are blue. The title
// bar fills the yellow zone exactly; all body content starts below it.
void UiController::drawTitle(const char *title, int count) {
  _oled.drawBox(0, 0, 128, 16);
  _oled.setDrawColor(0);
  _oled.drawStr(2, 12, title);
  if (count >= 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "[%d]", count);
    _oled.drawStr(128 - _oled.getStrWidth(buf) - 2, 12, buf);
  }
  _oled.setDrawColor(1);
}

void UiController::drawFooter(const char *text) { _oled.drawStr(0, 63, text); }

// generic 4-line scrolling list body between y=24..54 (blue zone only)
static void drawRows(U8G2 &oled, uint8_t cursor, int rowCount,
                     void (*rowText)(void *, int, char *, size_t), void *ctx) {
  const int first = (cursor > 3) ? cursor - 3 : 0;
  for (int line = 0; line < 4; line++) {
    const int idx = first + line;
    if (idx >= rowCount) break;
    char buf[24];
    rowText(ctx, idx, buf, sizeof(buf));
    const int y = 24 + line * 10;
    if (idx == cursor) oled.drawStr(0, y, ">");
    oled.drawStr(7, y, buf);
  }
}

void UiController::render() {
  _oled.clearBuffer();

  switch (_screen) {
    case SCR_MAIN: {
      drawTitle("Infinitag Config");
      struct Ctx { const char **items; } ctx{MAIN_ITEMS};
      drawRows(_oled, _cursor, MAIN_COUNT,
               [](void *c, int i, char *b, size_t n) {
                 snprintf(b, n, "%s", ((Ctx *)c)->items[i]);
               },
               &ctx);
      drawFooter("Drehen+Push  K4=OK");
      break;
    }

    case SCR_STATION_MENU: {
      drawTitle("Stationen");
      struct Ctx { const char **items; } ctx{STATION_ITEMS};
      drawRows(_oled, _cursor, STATION_COUNT,
               [](void *c, int i, char *b, size_t n) {
                 snprintf(b, n, "%s", ((Ctx *)c)->items[i]);
               },
               &ctx);
      drawFooter("K1=Zurueck");
      break;
    }

    case SCR_DEVICE_LIST: {
      const int count = (int)_reg.count(_listType);
      drawTitle(_listType == DEV_STATION ? "Stationen: Liste"
                                         : "Targets: Liste",
                count);
      if (count == 0) {
        _oled.drawStr(7, 34, "suche Geraete...");
        _oled.drawStr(7, 44, "K2 = neu suchen");
      } else {
        struct Ctx {
          UiController *ui;
          uint8_t type;
        } ctx{this, _listType};
        drawRows(_oled, _cursor, count,
                 [](void *c, int i, char *b, size_t n) {
                   Ctx *x = (Ctx *)c;
                   Device *d = x->ui->_reg.byIndex(x->type, i);
                   if (!d) { b[0] = '\0'; return; }
                   char mac[7];
                   x->ui->macSuffix(d->mac, mac);
                   char idBuf[4];
                   if (d->id == 0) snprintf(idBuf, sizeof(idBuf), "??");
                   else snprintf(idBuf, sizeof(idBuf), "%02u", d->id);
                   snprintf(b, n, "%s%s %s %4ddBm",
                            x->ui->_reg.isDuplicateId(*d) ? "!" : "", idBuf,
                            mac, d->info.rssi_self);
                 },
                 &ctx);
      }
      drawFooter(_identifyEnabled ? "K3=Blink aus K2=Suche"
                                  : "K3=Blink an  K2=Suche");
      break;
    }

    case SCR_DEVICE_EDIT: {
      char title[24];
      char mac[7];
      macSuffix(_editDev.mac, mac);
      snprintf(title, sizeof(title), "%s %s",
               _editDev.deviceType == DEV_STATION ? "Station" : "Target", mac);
      drawTitle(title);

      const int rows = _fieldCount + 1;
      struct Ctx { UiController *ui; } ctx{this};
      drawRows(_oled, _cursor, rows,
               [](void *c, int i, char *b, size_t n) {
                 UiController *ui = ((Ctx *)c)->ui;
                 if (i >= ui->_fieldCount) {
                   snprintf(b, n, "[Speichern]");
                   return;
                 }
                 const Field &f = ui->_fields[i];
                 char val[12];
                 if (f.isBitmask) {
                   snprintf(val, sizeof(val), "%c%c%c",
                            (f.value & 1) ? '1' : '-',
                            (f.value & 2) ? '2' : '-',
                            (f.value & 4) ? '3' : '-');
                 } else {
                   snprintf(val, sizeof(val), "%ld%s", (long)f.value,
                            f.unit ? f.unit : "");
                 }
                 const bool editing =
                     ui->_valueEditing && i == ui->_cursor;
                 snprintf(b, n, "%-9s:%s%s%s", f.label,
                          editing ? "[" : " ", val, editing ? "]" : "");
               },
               &ctx);

      drawFooter(_statusLine[0] ? _statusLine
                                : (_valueEditing ? "Push=OK K1=Abbruch"
                                                 : "K2halt=x10 K1=Zurueck"));
      break;
    }

    case SCR_SETUP_WAIT:
      drawTitle("Neue Station");
      if (_setupResult[0] == '\0') {
        _oled.drawStr(0, 24, "Setup-Modus aktiv.");
        _oled.drawStr(0, 36, "Trigger an der Wunsch-");
        _oled.drawStr(0, 46, "Station druecken...");
        drawFooter("K1=Abbrechen");
      } else {
        _oled.drawStr(0, 34, _setupResult);
        drawFooter("K1/K4=Zurueck");
      }
      break;

    case SCR_SOUND_TEST: {
      char title[24];
      snprintf(title, sizeof(title), "Sound-Test St.%02u", _editDev.id);
      drawTitle(title);
      char big[8];
      snprintf(big, sizeof(big), "%02u", _testSound);
      _oled.setFont(u8g2_font_10x20_tf);
      _oled.drawStr(54, 40, big);
      _oled.setFont(u8g2_font_6x10_tf);
      drawFooter("Push=Spielen K1=Zurueck");
      break;
    }

    case SCR_LIVE_MONITOR: {
      drawTitle("Live-Monitor", (int)_hitCount);
      if (_hitCount == 0) {
        _oled.drawStr(7, 34, "warte auf Treffer...");
      } else {
        struct Ctx { UiController *ui; } ctx{this};
        drawRows(_oled, 0, (int)_hitCount,
                 [](void *c, int i, char *b, size_t n) {
                   UiController *ui = ((Ctx *)c)->ui;
                   const HitEntry &h = ui->_hits[i];
                   const uint32_t age = (millis() - h.ms) / 1000;
                   snprintf(b, n, "%3lus T%02u>S%02u snd=%02u",
                            (unsigned long)(age > 999 ? 999 : age),
                            h.targetId, h.stationId, h.soundId);
                 },
                 &ctx);
      }
      drawFooter("K1=Zurueck");
      break;
    }

    case SCR_TOOLS_INFO: {
      drawTitle("Firmware-Info");
      char buf[24];
      snprintf(buf, sizeof(buf), "Version %u.%u.%u", cfg::FW_MAJOR,
               cfg::FW_MINOR, cfg::FW_PATCH);
      _oled.drawStr(0, 24, buf);
      const uint8_t *m = _net.ownMac();
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1],
               m[2], m[3], m[4], m[5]);
      _oled.drawStr(0, 34, buf);
      snprintf(buf, sizeof(buf), "Heap %lu B",
               (unsigned long)ESP.getFreeHeap());
      _oled.drawStr(0, 44, buf);
      // average 8 samples: the 100k/22k divider is high-impedance for the
      // ADC sample cap; a 100 nF cap at GPIO3 fixes the systematic droop,
      // averaging smooths the rest
      uint32_t mvAcc = 0;
      for (int i = 0; i < 8; i++) mvAcc += analogReadMilliVolts(cfg::PIN_VBAT);
      const uint32_t mv = mvAcc / 8;
      const float vbat = mv * cfg::VBAT_DIVIDER / 1000.0f;
      if (vbat > 3.0f) {
        snprintf(buf, sizeof(buf), "Up %lumin Batt %.2fV",
                 (unsigned long)(millis() / 60000UL), (double)vbat);
      } else {
        snprintf(buf, sizeof(buf), "Up %lumin  USB",
                 (unsigned long)(millis() / 60000UL));
      }
      _oled.drawStr(0, 54, buf);
      break;
    }

    case SCR_NOTICE:
      drawTitle("Web-UI");
      _oled.drawStr(0, 30, "Kommt in V0.2");
      _oled.drawStr(0, 42, "(SoftAP + Browser)");
      drawFooter("K1=Zurueck");
      break;
  }

  _oled.sendBuffer();
}
