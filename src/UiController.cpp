#include "UiController.h"

#include <esp_system.h>  // esp_random()

#include <LittleFS.h>
#include <esp_now.h>

#include "Config.h"
#include "NetUpdater.h"
#include "WebPage.h"
#include "SoundCatalog.h"

using namespace inow;

// ---------------------------------------------------------------------------
// menu definitions
// ---------------------------------------------------------------------------

static const char *MAIN_ITEMS[] = {"Stationen", "Targets", "Live-Monitor",
                                   "Tools"};
static constexpr uint8_t MAIN_COUNT = 4;

// Device menu (after picking a device from the list)
static const char *DEVMENU_STATION[] = {"< Zurueck", "Konfigurieren",
                                        "Sound testen", "Selbsttest",
                                        "Update (Funk)", "Update (OTA)"};
static constexpr uint8_t DEVMENU_STATION_COUNT = 6;
static const char *DEVMENU_TARGET[] = {"< Zurueck", "Konfigurieren",
                                       "Update (Funk)", "Update (OTA)"};
static constexpr uint8_t DEVMENU_TARGET_COUNT = 4;

static const char *TOOLS_ITEMS[] = {"< Zurueck", "Nach Updates suchen",
                                    "Firmware-Info", "Update-Modus",
                                    "Geraete-Images", "Versions-Memo Reset"};
static constexpr uint8_t TOOLS_COUNT = 6;

// Self-test rows: 0 = back, 1 = run all, 2..6 = tests 1..5 (DebugTest ids).
static const char *SELFTEST_ITEMS[] = {"< Zurueck",  "Alle testen",
                                       "Sound",      "Stab-LEDs",
                                       "Laser (2s)", "IR-Burst",
                                       "Trigger (10s)"};
static constexpr uint8_t SELFTEST_ROWS = 7;

// Per-test DEBUG_CMD parameter and config-box-side wait deadline.
static const uint8_t SELFTEST_PARAM[5] = {0, 0, 2, 1, 10};
static const uint32_t SELFTEST_DEADLINE_MS[5] = {30000, 5000, 5000, 5000,
                                                 15000};

UiController::UiController(EspNowService &net, DeviceRegistry &reg,
                           InputController &in)
    : _net(net), _reg(reg), _in(in),
      _oled(U8G2_R0, /* reset=*/U8X8_PIN_NONE) {}

void UiController::begin() {
  _memo.load();
  if (_images.begin()) {
    // stored image versions count as "seen" (stage 2 of the '^' marker)
    for (uint8_t t : {(uint8_t)DEV_STATION, (uint8_t)DEV_TARGET}) {
      const uint32_t k = _images.versionKey(t);
      if (k) _memo.note(t, k);
    }
  }
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
    case SCR_SELF_TEST:
      for (uint8_t i = 0; i < SELF_TESTS; i++) _selfResult[i] = ' ';
      _selfRunning = 0;
      _selfAllMode = false;
      break;
    case SCR_DEV_UPDATE:
      beginDeviceUpdate();
      break;
    case SCR_PUSH:
      if (!beginPush(_editDev)) {
        snprintf(_pushResult, sizeof(_pushResult), "Kein Image geladen!");
        _pushPhase = 2;
      }
      break;
    case SCR_BULK:
      _bulk = true;
      _bulkPos = 0;
      _bulkOk = _bulkFail = _bulkSkip = 0;
      if (!_images.info(_listType).present) {
        snprintf(_pushResult, sizeof(_pushResult), "Kein Image geladen!");
        _pushPhase = 2;
      } else {
        bulkNext();
      }
      break;
    case SCR_SELF_UPDATE:
      beginSelfUpdate();
      break;
    default:
      break;
  }
}

void UiController::startDiscovery(uint8_t deviceType) {
  _reg.clear(deviceType);
  _discoverToken = (uint8_t)(esp_random() & 0xFF);
  if (_discoverToken == 0) _discoverToken = 1;
  sendDiscoverReq(deviceType);
}

void UiController::sendDiscoverReq(uint8_t deviceType) {
  Packet p;
  init(p, MSG_DISCOVER_REQ, DEV_CONFIG_BOX);  // header type = sender type
  p.token = _discoverToken;
  p.payload[0] = deviceType;
  _net.sendBroadcast(p);
}

void UiController::sendIdentify(const Device &d) {
  Packet p;
  init(p, MSG_IDENTIFY, d.deviceType);
  p.payload[0] = cfg::IDENTIFY_DURATION_100MS;
  _net.send(d.mac, p);
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
                 int32_t st, const char *unit = nullptr,
                 FieldFmt fmt = FMT_NUM) {
    if (_fieldCount >= MAX_FIELDS) return;
    _fields[_fieldCount++] = Field{label, val, mn, mx, st, unit, fmt};
  };

  if (d.deviceType == DEV_STATION) {
    StationConfig c;
    decodeStationConfig(d.info.config_blob, d.info.config_blob_len, c);
    add("Volume", c.volume_pct, 0, 100, 1, "%");
    // Wand status colors: channel mask R/G/B/W, all 15 combinations.
    add("LED bereit", c.led_ready, 1, LED_MASK_MAX, 1, nullptr, FMT_LED);
    add("LED aktiv", c.led_busy, 1, LED_MASK_MAX, 1, nullptr, FMT_LED);
  } else {
    TargetConfig c;
    decodeTargetConfig(d.info.config_blob, d.info.config_blob_len, c);

    // Build the station pick list: all discovered stations, plus the MAC
    // currently stored in the target if it is set but not (re)discovered.
    _staPickCount = 0;
    int32_t current = 0;
    for (size_t i = 0; _staPickCount < MAX_STA_PICK; i++) {
      Device *s = _reg.byIndex(DEV_STATION, i);
      if (!s) break;
      memcpy(_staPick[_staPickCount], s->mac, 6);
      if (memcmp(s->mac, c.station_mac, 6) == 0) current = _staPickCount;
      _staPickCount++;
    }
    static const uint8_t ZERO_MAC[6] = {0};
    const bool stored = memcmp(c.station_mac, ZERO_MAC, 6) != 0;
    if (stored && _staPickCount < MAX_STA_PICK) {
      bool known = false;
      for (uint8_t i = 0; i < _staPickCount; i++)
        if (memcmp(_staPick[i], c.station_mac, 6) == 0) {
          known = true;
          current = i;
        }
      if (!known) {
        memcpy(_staPick[_staPickCount], c.station_mac, 6);
        current = _staPickCount;
        _staPickCount++;
      }
    }

    add("Station", current, 0,
        _staPickCount > 0 ? _staPickCount - 1 : 0, 1, nullptr, FMT_STATION);
    add("Sound", c.sound_id, 0, SOUND_COUNT - 1, 1, nullptr, FMT_SOUND);
    add("Hit-Time", c.hit_time_ms, 100, 60000, 100, "ms");
    add("Cooldown", c.cooldown_ms, 0, 60000, 100, "ms");
    add("SW-Anim", c.sw_animation, 0, 1, 1);
    add("SW-Kanal", c.sw_channels, 0, 7, 1, nullptr, FMT_BITS);
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
    c.volume_pct = (uint8_t)_fields[0].value;
    c.led_ready = (uint8_t)_fields[1].value;
    c.led_busy = (uint8_t)_fields[2].value;
    encodeStationConfig(c, p.payload);
  } else {
    TargetConfig c;
    if (_staPickCount > 0)
      memcpy(c.station_mac, _staPick[_fields[0].value], 6);
    c.sound_id = (uint8_t)_fields[1].value;
    c.hit_time_ms = (uint16_t)_fields[2].value;
    c.cooldown_ms = (uint16_t)_fields[3].value;
    c.sw_animation = (uint8_t)_fields[4].value;
    c.sw_channels = (uint8_t)_fields[5].value;
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
  p.payload[0] = _testSound;
  _net.send(_editDev.mac, p);
}

void UiController::runSelfTest(uint8_t test) {
  if (test < 1 || test > SELF_TESTS) return;
  Packet p;
  init(p, MSG_DEBUG_CMD, _editDev.deviceType);
  p.payload[0] = test;
  p.payload[1] = SELFTEST_PARAM[test - 1];
  if (_net.send(_editDev.mac, p)) {
    _selfRunning = test;
    _selfResult[test - 1] = '.';
    _selfDeadline = millis() + SELFTEST_DEADLINE_MS[test - 1];
  } else {
    _selfResult[test - 1] = 'X';
    _selfRunning = 0;
    _selfAllMode = false;
  }
  _dirty = true;
}

void UiController::beginDeviceUpdate() {
  _updOldVer = DeviceRegistry::versionKey(_editDev.info);
  _updPollMs = millis();
  _updResult[0] = '\0';
  Packet p;
  init(p, MSG_UPDATE_BEGIN, _editDev.deviceType);
  p.payload[0] = cfg::UPDATE_TIMEOUT_MIN;
  if (_net.send(_editDev.mac, p)) {
    _updState = UPD_WAIT_ACK;
    _updAckDeadline = millis() + cfg::UPDATE_ACK_TIMEOUT_MS;
  } else {
    _updState = UPD_NO_ACK;
  }
}

void UiController::rebootWithScreen(const char *line) {
  _oled.clearBuffer();
  drawTitle("Update-Modus");
  _oled.drawStr(0, 32, line);
  _oled.drawStr(0, 44, "Neustart...");
  _oled.sendBuffer();
  delay(800);  // long enough to read before the radio/screen freeze
  ESP.restart();
}

float UiController::readVbat() const {
  // average 8 samples: the 100k/22k divider is high-impedance for the
  // ADC sample cap; a 100 nF cap at GPIO3 fixes the systematic droop,
  // averaging smooths the rest
  uint32_t mvAcc = 0;
  for (int i = 0; i < 8; i++) mvAcc += analogReadMilliVolts(cfg::PIN_VBAT);
  return (mvAcc / 8) * cfg::VBAT_DIVIDER / 1000.0f;
}

// StoreHooks bridge (WebUpdateService callbacks are plain C functions).
static ImageStore *s_imgStore = nullptr;
static bool storeBegin(const char *) { return s_imgStore->uploadBegin(); }
static bool storeWrite(const uint8_t *d, size_t n) {
  return s_imgStore->uploadWrite(d, n);
}
static bool storeEnd(bool ok) { return s_imgStore->uploadEnd(ok); }
static const char *storeResult() { return s_imgStore->resultText(); }
static const StoreHooks kStoreHooks = {storeBegin, storeWrite, storeEnd,
                                       storeResult};

// (Re)build the SoftAP page from the design template; called at
// update-mode start AND after every WLAN save so the page never shows a
// stale SSID.
void UiController::buildWifiForm() {
  char mac[7];
  macSuffix(_net.ownMac(), mac);
  char ver[16];
  snprintf(ver, sizeof(ver), "v%u.%u.%u", cfg::FW_MAJOR, cfg::FW_MINOR,
           cfg::FW_PATCH);
  char curSsid[33] = "";
  NetUpdater::getWifiSsid(curSsid, sizeof(curSsid));

  _wifiFormHtml = WEB_PAGE_TEMPLATE;
  _wifiFormHtml.replace("%DEVICE_ID%", mac);
  _wifiFormHtml.replace("%VERSION%", ver);
  _wifiFormHtml.replace("%WIFI_STATUS%",
                        curSsid[0] ? curSsid : "nicht konfiguriert");
  _webUpd.setCustomPage(_wifiFormHtml.c_str());
}

void UiController::beginSelfUpdate() {
  // Battery gate: never start flashing on a nearly empty pack. Readings
  // below 3.0 V mean "no battery / USB powered" and are fine.
  const float vbat = readVbat();
  if (vbat > 3.0f && vbat < cfg::VBAT_MIN_FOR_UPDATE) {
    _selfUpd = SELFUPD_REFUSED;
    return;
  }

  const uint8_t *m = _net.ownMac();
  snprintf(_selfUpdAp, sizeof(_selfUpdAp), "infinitag-cfg-%02X%02X%02X", m[3],
           m[4], m[5]);
  char ver[16];
  snprintf(ver, sizeof(ver), "%u.%u.%u", cfg::FW_MAJOR, cfg::FW_MINOR,
           cfg::FW_PATCH);
  char label[24];
  snprintf(label, sizeof(label), "Config-Box %02X%02X%02X", m[3], m[4], m[5]);

  // Tears down ESP-NOW; the only way back to normal operation is a reboot.
  s_imgStore = &_images;
  _webUpd.setStoreHooks(&kStoreHooks);
  buildWifiForm();
  if (_webUpd.begin(_selfUpdAp, ver, label, "infinitag-config")) {
    _webUpd.server().on("/wifi", HTTP_POST, [this]() {
      WebServer &srv = _webUpd.server();
      NetUpdater::setWifiCredentials(srv.arg("ssid").c_str(),
                                     srv.arg("pass").c_str());
      buildWifiForm();  // root page must show the new SSID right away
      String body = "Verbunden wird mit: <b>";
      body += srv.arg("ssid");
      body += "</b> &ndash; genutzt beim n&auml;chsten "
              "\"Nach Updates suchen\".";
      srv.send(200, "text/html",
               WebUpdateService::resultPage("WLAN gespeichert", body));
    });
    _selfUpd = SELFUPD_ACTIVE;
    _selfUpdDeadline = millis() + cfg::SELF_UPDATE_TIMEOUT_MS;
  } else {
    rebootWithScreen("AP-Fehler");  // radio state is undefined now
  }
}

// ---------------------------------------------------------------------------
// ESP-NOW firmware push (Doc 21 E3) + bulk mode (E4)
// ---------------------------------------------------------------------------

// random-access reader for the push sender (LittleFS file)
static size_t pushRead(void *ctx, uint32_t offset, uint8_t *buf, size_t len) {
  File *f = (File *)ctx;
  if (!f->seek(offset)) return 0;
  return f->read(buf, len);
}

bool UiController::beginPush(const Device &d) {
  const ImageInfo &img = _images.info(d.deviceType);
  if (!img.present) return false;

  _pushFile = LittleFS.open(ImageStore::path(d.deviceType), "r");
  if (!_pushFile) return false;

  // CRC once over the file (bitwise, ~0.5 s per MB on the C3)
  uint32_t crc = 0;
  size_t crcBytes = 0;
  uint8_t buf[1024];
  while (_pushFile.available()) {
    const int n = _pushFile.read(buf, sizeof(buf));
    if (n <= 0) break;
    crc = crc32(crc, buf, (size_t)n);
    crcBytes += (size_t)n;
  }

  _pushImgKey = _images.versionKey(d.deviceType);
  _pushPhase = 0;
  _pushResult[0] = '\0';
  // announce exactly what the CRC covered, not the store metadata
  if (!_pushTx.start(&_net, d.mac, pushRead, &_pushFile, crcBytes, crc,
                     img.major, img.minor, img.patch)) {
    _pushFile.close();
    return false;
  }
  return true;
}

// advance the bulk queue to the next outdated device of _listType
void UiController::bulkNext() {
  const uint32_t imgKey = _images.versionKey(_listType);
  for (;; _bulkPos++) {
    Device *d = _reg.byIndex(_listType, _bulkPos);
    if (!d) {  // done
      snprintf(_pushResult, sizeof(_pushResult), "OK:%u Fehl:%u Akt:%u",
               _bulkOk, _bulkFail, _bulkSkip);
      _pushPhase = 2;
      _dirty = true;
      return;
    }
    if (DeviceRegistry::versionKey(d->info) >= imgKey) {
      _bulkSkip++;
      continue;  // already up to date
    }
    _editDev = *d;
    _bulkPos++;
    if (beginPush(_editDev)) {
      _dirty = true;
      return;
    }
    _bulkFail++;  // image/file problem – counts as failure, try next
  }
}

// ---------------------------------------------------------------------------
// guided net update (Doc 21 E2) – blocking, always ends in ESP.restart()
// ---------------------------------------------------------------------------

void UiController::netScreen(const char *l1, const char *l2, const char *l3,
                             const char *footer) {
  _oled.clearBuffer();
  drawTitle("Internet-Update");
  if (l1) _oled.drawStr(0, 28, l1);
  if (l2) _oled.drawStr(0, 40, l2);
  if (l3) _oled.drawStr(0, 52, l3);
  if (footer) drawFooter(footer);
  _oled.sendBuffer();
}

// progress callback bridge (plain function pointer)
static UiController *s_netUi = nullptr;
static char s_netLine[24];
static void netProgress(size_t done, size_t total) {
  static uint32_t lastMs = 0;
  if (millis() - lastMs < 250) return;
  lastMs = millis();
  char line[24];
  if (total > 0) {
    snprintf(line, sizeof(line), "%u%% (%u KB)",
             (unsigned)(done * 100 / total), (unsigned)(done / 1024));
  } else {
    snprintf(line, sizeof(line), "%u KB", (unsigned)(done / 1024));
  }
  s_netUi->netScreen(s_netLine, line);
}

// wait for encoder push (true) or K1 (false)
bool uiWaitConfirm(InputController &in) {
  for (;;) {
    in.poll();
    if (in.takePress(BTN_ENC) || in.takePress(BTN_K4)) return true;
    if (in.takePress(BTN_K1)) return false;
    delay(10);
  }
}

void UiController::runNetUpdate() {
  // battery gate as with the SoftAP update mode
  const float vbat = readVbat();
  if (vbat > 3.0f && vbat < cfg::VBAT_MIN_FOR_UPDATE) {
    netScreen("Akku zu leer!", "(min. 3.6 V oder USB)", nullptr,
              "Taste = Zurueck");
    uiWaitConfirm(_in);
    ESP.restart();
  }
  if (!NetUpdater::hasWifiCredentials()) {
    netScreen("Kein WLAN konfiguriert", "Tools > Update-Modus:", 
              "WLAN dort speichern", "Taste = Neustart");
    uiWaitConfirm(_in);
    ESP.restart();
  }

  s_netUi = this;
  esp_now_deinit();  // leave the ESP-NOW world; way back = reboot

  NetUpdater net;
  netScreen("Verbinde WLAN...");
  if (!net.connectWifi()) {
    netScreen("WLAN fehlgeschlagen", net.lastError(), nullptr,
              "Taste = Neustart");
    uiWaitConfirm(_in);
    ESP.restart();
  }

  netScreen("Frage GitHub ab...");
  ReleaseInfo relBox, relSta, relTgt;
  net.fetchLatest("infinitag-now-config", "infinitag-config", relBox);
  net.fetchLatest("infinitag-now-station", "infinitag-station", relSta);
  net.fetchLatest("infinitag-now-target", "infinitag-target", relTgt);

  const uint32_t ownKey = ((uint32_t)cfg::FW_MAJOR << 16) |
                          ((uint32_t)cfg::FW_MINOR << 8) | cfg::FW_PATCH;
  const bool boxNew = relBox.ok && NetUpdater::versionKey(relBox) > ownKey;
  const bool staNew =
      relSta.ok &&
      NetUpdater::versionKey(relSta) > _images.versionKey(DEV_STATION);
  const bool tgtNew =
      relTgt.ok &&
      NetUpdater::versionKey(relTgt) > _images.versionKey(DEV_TARGET);

  char l1[24], l2[24], l3[24];
  if (relBox.ok)
    snprintf(l1, sizeof(l1), "Box:     v%u.%u.%u%s", relBox.major,
             relBox.minor, relBox.patch, boxNew ? " NEU" : " ok");
  else
    snprintf(l1, sizeof(l1), "Box:     Fehler");
  if (relSta.ok)
    snprintf(l2, sizeof(l2), "Station: v%u.%u.%u%s", relSta.major,
             relSta.minor, relSta.patch, staNew ? " NEU" : " ok");
  else
    snprintf(l2, sizeof(l2), "Station: --");
  if (relTgt.ok)
    snprintf(l3, sizeof(l3), "Target:  v%u.%u.%u%s", relTgt.major,
             relTgt.minor, relTgt.patch, tgtNew ? " NEU" : " ok");
  else
    snprintf(l3, sizeof(l3), "Target:  --");

  if (!boxNew && !staNew && !tgtNew) {
    netScreen(l1, l2, l3, "Alles aktuell! Taste=Neustart");
    uiWaitConfirm(_in);
    ESP.restart();
  }
  netScreen(l1, l2, l3, "Push=Laden  K1=Abbruch");
  if (!uiWaitConfirm(_in)) ESP.restart();

  // 1) device images into the store (survive the self-update reboot)
  if (staNew) {
    snprintf(s_netLine, sizeof(s_netLine), "Lade Station-Image");
    netScreen(s_netLine);
    if (!net.downloadToStore(relSta, _images, netProgress)) {
      netScreen("Station-Image Fehler", net.lastError(), nullptr,
                "Taste = Neustart");
      uiWaitConfirm(_in);
      ESP.restart();
    }
  }
  if (tgtNew) {
    snprintf(s_netLine, sizeof(s_netLine), "Lade Target-Image");
    netScreen(s_netLine);
    if (!net.downloadToStore(relTgt, _images, netProgress)) {
      netScreen("Target-Image Fehler", net.lastError(), nullptr,
                "Taste = Neustart");
      uiWaitConfirm(_in);
      ESP.restart();
    }
  }

  // 2) own firmware last – reboot follows immediately
  if (boxNew) {
    snprintf(s_netLine, sizeof(s_netLine), "Box-Update laeuft");
    netScreen(s_netLine, "NICHT ausschalten!");
    if (net.selfUpdate(relBox, netProgress)) {
      netScreen("Box-Update OK", "Neustart...");
    } else {
      netScreen("Box-Update Fehler", net.lastError(), "Alte FW bleibt.",
                "Taste = Neustart");
      uiWaitConfirm(_in);
    }
  } else {
    netScreen("Images geladen.", "Verteilen: Geraet >", "Update (OTA)",
              "Taste = Neustart");
    uiWaitConfirm(_in);
  }
  delay(800);
  ESP.restart();
}

// ---------------------------------------------------------------------------
// version check ('^' marker in the device lists)
// ---------------------------------------------------------------------------

bool UiController::isOutdated(const Device &d) const {
  // Reference = best of (newest version currently in the list, newest
  // version ever seen, persisted in NVS). Stage 2 – the exact version
  // from a firmware image carried by the box – comes with the ESP-NOW
  // OTA (Doc 18 §12) and will simply become a third max() term here.
  uint32_t ref = _reg.maxVersionKey(d.deviceType);
  const uint32_t seen = _memo.maxKey(d.deviceType);
  if (seen > ref) ref = seen;
  return DeviceRegistry::versionKey(d.info) < ref;
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
        // remember the newest firmware ever seen per type ('^' marker)
        DiscoverReply info;
        decodeDiscoverReply(p.payload, info);
        _memo.note(p.device_type, DeviceRegistry::versionKey(info));
        if (_screen == SCR_DEVICE_LIST) _dirty = true;
      }
      // Push mode: device came back after the flash reboot?
      if ((_screen == SCR_PUSH || _screen == SCR_BULK) && _pushPhase == 1 &&
          memcmp(rx.mac, _editDev.mac, 6) == 0) {
        DiscoverReply r;
        decodeDiscoverReply(p.payload, r);
        if (DeviceRegistry::versionKey(r) >= _pushImgKey) {
          if (_bulk) {
            _bulkOk++;
            bulkNext();
          } else {
            snprintf(_pushResult, sizeof(_pushResult), "Update OK: v%u.%u.%u",
                     r.fw_major, r.fw_minor, r.fw_patch);
            _pushPhase = 2;
          }
          _dirty = true;
        }
      }
      // Update screen: the device rebooted back onto ESP-NOW – compare
      // versions and show the outcome before returning to the list.
      if (_screen == SCR_DEV_UPDATE && _updState == UPD_ACTIVE &&
          memcmp(rx.mac, _editDev.mac, 6) == 0) {
        DiscoverReply r;
        decodeDiscoverReply(p.payload, r);
        if (DeviceRegistry::versionKey(r) > _updOldVer) {
          snprintf(_updResult, sizeof(_updResult), "Update OK: v%u.%u.%u",
                   r.fw_major, r.fw_minor, r.fw_patch);
        } else {
          snprintf(_updResult, sizeof(_updResult), "Zurueck, unveraendert");
        }
        _updState = UPD_DEVICE_BACK;
        _updBackMs = millis();
        _dirty = true;
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

    case MSG_DEBUG_RESULT:
      if (_screen == SCR_SELF_TEST && memcmp(rx.mac, _editDev.mac, 6) == 0) {
        const uint8_t test = p.payload[0];
        if (test >= 1 && test <= SELF_TESTS) {
          char c;
          switch (p.payload[1]) {
            case DBG_RES_OK:      c = 'O'; break;  // rendered as "OK"
            case DBG_RES_FAIL:    c = 'X'; break;
            case DBG_RES_TIMEOUT: c = 'T'; break;
            default:              c = 'U'; break;  // unsupported
          }
          _selfResult[test - 1] = c;
          if (_selfRunning == test) _selfRunning = 0;
          // "Alle testen": advance to the next test in the chain.
          if (_selfAllMode) {
            if (test < SELF_TESTS) {
              runSelfTest(test + 1);
            } else {
              _selfAllMode = false;
            }
          }
          _dirty = true;
        }
      }
      break;

    case MSG_PUSH_ACK:
      _pushTx.onControl(rx);
      break;

    case MSG_UPDATE_ACK:
      if (_screen == SCR_DEV_UPDATE && _updState == UPD_WAIT_ACK &&
          memcmp(rx.mac, _editDev.mac, 6) == 0) {
        _updState = UPD_ACTIVE;
        _dirty = true;
      }
      break;

    case MSG_HIT_REPORT: {
      // ring buffer, newest first
      for (size_t i = MONITOR_RING - 1; i > 0; i--) _hits[i] = _hits[i - 1];
      HitEntry &h = _hits[0];
      h.ms = millis();
      memcpy(h.targetMac, rx.mac, 6);
      decodeHitReport(p.payload, h.stationMac, h.soundId);
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
  // x10 step: K2 held (shortcut) OR fast encoder turning (encoder-only way).
  const int32_t stepMul =
      (_in.isHeld(BTN_K2) || delta >= 4 || delta <= -4) ? 10 : 1;

  if (delta == 0 && !push && !back && !k2 && !k3) return;
  _dirty = true;

  switch (_screen) {
    case SCR_MAIN:
      if (delta) _cursor = (uint8_t)clampVal(_cursor + delta, 0, MAIN_COUNT - 1);
      if (push) {
        switch (_cursor) {
          case 0:
            _listType = DEV_STATION;
            gotoScreen(SCR_DEVICE_LIST);
            break;
          case 1:
            _listType = DEV_TARGET;
            gotoScreen(SCR_DEVICE_LIST);
            break;
          case 2: gotoScreen(SCR_LIVE_MONITOR); break;
          case 3: gotoScreen(SCR_TOOLS_MENU); break;
        }
      }
      break;

    case SCR_DEVICE_MENU: {
      const uint8_t rows = _editDev.deviceType == DEV_STATION
                               ? DEVMENU_STATION_COUNT
                               : DEVMENU_TARGET_COUNT;
      if (delta) _cursor = (uint8_t)clampVal(_cursor + delta, 0, rows - 1);
      if (push) {
        if (_editDev.deviceType == DEV_STATION) {
          switch (_cursor) {
            case 0: gotoScreen(SCR_DEVICE_LIST); break;  // < Zurueck
            case 1: enterEdit(_editDev); break;
            case 2: gotoScreen(SCR_SOUND_TEST); break;
            case 3: gotoScreen(SCR_SELF_TEST); break;
            case 4:
              _bulk = false;
              gotoScreen(SCR_PUSH);
              break;
            case 5: gotoScreen(SCR_DEV_UPDATE); break;
          }
        } else {
          switch (_cursor) {
            case 0: gotoScreen(SCR_DEVICE_LIST); break;
            case 1: enterEdit(_editDev); break;
            case 2:
              _bulk = false;
              gotoScreen(SCR_PUSH);
              break;
            case 3: gotoScreen(SCR_DEV_UPDATE); break;
          }
        }
      }
      if (back) gotoScreen(SCR_DEVICE_LIST);
      break;
    }

    case SCR_DEVICE_LIST: {
      // rows: 0 = "< Zurueck", 1 = "Neu suchen", 2.. = devices
      const int count = (int)_reg.count(_listType);
      const int rows = count + LIST_STATIC_ROWS;
      if (delta) {
        _cursor = (uint8_t)clampVal(_cursor + delta, 0, rows - 1);
        _lastIdentifyMs = 0;  // identify the new cursor entry immediately
      }
      if (k3) _identifyEnabled = !_identifyEnabled;
      if (k2) startDiscovery(_listType);  // shortcut: manual refresh
      if (push) {
        if (_cursor == 0) {
          gotoScreen(SCR_MAIN);
        } else if (_cursor == 1) {
          startDiscovery(_listType);
        } else if (_cursor == 2) {
          gotoScreen(SCR_BULK);  // Alle aktualisieren (Doc 21 E4)
        } else {
          Device *d = _reg.byIndex(_listType, _cursor - LIST_STATIC_ROWS);
          if (d) {
            _editDev = *d;
            gotoScreen(SCR_DEVICE_MENU);
          }
        }
      }
      if (back) gotoScreen(SCR_MAIN);
      break;
    }

    case SCR_DEVICE_EDIT: {
      if (_awaitingAck) break;  // ignore input while waiting for the ACK
      const int rows = _fieldCount + 2;  // + "[Speichern]" + "[< Zurueck]"
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
          } else if (_cursor == _fieldCount) {
            sendCfgWrite();
          } else {
            gotoScreen(SCR_DEVICE_MENU);  // [< Zurueck]
          }
        }
        if (back) gotoScreen(SCR_DEVICE_MENU);
      }
      break;
    }

    case SCR_SOUND_TEST: {
      // rows: 0 = "< Zurueck", 1 = "Sound: [xx]", 2 = "[Abspielen]"
      if (_valueEditing) {
        if (delta)
          _testSound = (uint8_t)clampVal(_testSound + delta * stepMul, 0,
                                         SOUND_COUNT - 1);
        if (push) _valueEditing = false;  // apply
        if (back) _valueEditing = false;
      } else {
        if (delta) _cursor = (uint8_t)clampVal(_cursor + delta, 0, 2);
        if (push) {
          if (_cursor == 0) gotoScreen(SCR_DEVICE_MENU);
          else if (_cursor == 1) _valueEditing = true;
          else sendTestSound();
        }
        if (back) gotoScreen(SCR_DEVICE_MENU);
      }
      break;
    }

    case SCR_SELF_TEST: {
      if (delta) _cursor = (uint8_t)clampVal(_cursor + delta, 0,
                                             SELFTEST_ROWS - 1);
      if (push && _selfRunning == 0) {
        if (_cursor == 0) {
          gotoScreen(SCR_DEVICE_MENU);
        } else if (_cursor == 1) {
          for (uint8_t i = 0; i < SELF_TESTS; i++) _selfResult[i] = ' ';
          _selfAllMode = true;
          runSelfTest(1);
        } else {
          _selfAllMode = false;
          runSelfTest((uint8_t)(_cursor - 1));  // rows 2..6 -> tests 1..5
        }
      }
      if (back) {
        // K1 aborts waiting for a result (the station may still answer,
        // but we leave the screen deliberately).
        _selfRunning = 0;
        _selfAllMode = false;
        gotoScreen(SCR_DEVICE_MENU);
      }
      break;
    }

    case SCR_DEV_UPDATE:
      // The device is in (or on its way into) its SoftAP mode and off the
      // air – back to the list; a later rescan shows it after its reboot.
      if (back || push) gotoScreen(SCR_DEVICE_LIST);
      break;

    case SCR_PUSH:
    case SCR_BULK:
      if (_pushPhase == 2) {
        if (back || push) gotoScreen(SCR_DEVICE_LIST);
      } else if (back) {
        // abort: the device side times out on radio silence and keeps
        // its old firmware (incomplete images cannot boot)
        _pushFile.close();
        gotoScreen(SCR_DEVICE_LIST);
      }
      break;

    case SCR_TOOLS_MENU:
      if (delta) _cursor = (uint8_t)clampVal(_cursor + delta, 0,
                                             TOOLS_COUNT - 1);
      if (push) {
        switch (_cursor) {
          case 0: gotoScreen(SCR_MAIN); break;
          case 1: runNetUpdate(); break;  // blocking, ends in restart
          case 2: gotoScreen(SCR_TOOLS_INFO); break;
          case 3: gotoScreen(SCR_SELF_UPDATE); break;
          case 4: gotoScreen(SCR_IMAGES); break;
          case 5:
            // forget the best-ever-seen versions (e.g. after a
            // deliberate downgrade left a stale '^' marker)
            _memo.clear();
            snprintf(_toolsMsg, sizeof(_toolsMsg), "Memo geloescht");
            _toolsMsgUntil = millis() + 3000;
            break;
        }
      }
      if (back) gotoScreen(SCR_MAIN);
      break;

    case SCR_IMAGES: {
      // rows: 0 = back, 1 = station image, 2 = target image
      if (delta) _cursor = (uint8_t)clampVal(_cursor + delta, 0, 2);
      if (push) {
        if (_cursor == 0) {
          gotoScreen(SCR_TOOLS_MENU);
        } else {
          const uint8_t t = _cursor == 1 ? DEV_STATION : DEV_TARGET;
          if (_images.remove(t)) _dirty = true;  // push on entry = delete
        }
      }
      if (back) gotoScreen(SCR_TOOLS_MENU);
      break;
    }

    case SCR_SELF_UPDATE:
      if (_selfUpd == SELFUPD_REFUSED) {
        if (back || push) {
          _selfUpd = SELFUPD_OFF;
          gotoScreen(SCR_TOOLS_MENU);
        }
      } else if (back) {
        // ESP-NOW is torn down – a clean reboot is the only way back.
        rebootWithScreen("Abbruch");
      }
      break;

    case SCR_LIVE_MONITOR:
    case SCR_TOOLS_INFO:
      if (back || push) gotoScreen(SCR_MAIN);
      break;
  }
}

// ---------------------------------------------------------------------------
// periodic timers
// ---------------------------------------------------------------------------

void UiController::handleTimers() {
  const uint32_t now = millis();

  // identify blink refresh (Doc 18 §7) – on device rows in the list and
  // while the device menu is open (so you always see WHICH device it is)
  if (_identifyEnabled && now - _lastIdentifyMs >= cfg::IDENTIFY_PERIOD_MS) {
    if (_screen == SCR_DEVICE_LIST && _cursor >= LIST_STATIC_ROWS) {
      Device *d = _reg.byIndex(_listType, _cursor - LIST_STATIC_ROWS);
      if (d) sendIdentify(*d);
      _lastIdentifyMs = now;
    } else if (_screen == SCR_DEVICE_MENU) {
      sendIdentify(_editDev);
      _lastIdentifyMs = now;
    }
  }

  // CFG_ACK timeout
  if (_awaitingAck && now >= _ackDeadline) {
    _awaitingAck = false;
    snprintf(_statusLine, sizeof(_statusLine), "Keine Antwort!");
    _dirty = true;
  }

  // UPDATE_BEGIN -> UPDATE_ACK timeout
  if (_screen == SCR_DEV_UPDATE && _updState == UPD_WAIT_ACK &&
      now >= _updAckDeadline) {
    _updState = UPD_NO_ACK;
    _dirty = true;
  }

  // device update: poll until the device reboots back onto ESP-NOW,
  // then show the result for a moment and return to the list
  if (_screen == SCR_DEV_UPDATE) {
    if (_updState == UPD_ACTIVE && now - _updPollMs >= 3000) {
      _updPollMs = now;
      sendDiscoverReq(_editDev.deviceType);
    }
    if (_updState == UPD_DEVICE_BACK && now - _updBackMs >= 4000) {
      gotoScreen(SCR_DEVICE_LIST);
    }
  }

  // self-test: no DEBUG_RESULT within the deadline
  if (_selfRunning != 0 && now >= _selfDeadline) {
    _selfResult[_selfRunning - 1] = '?';  // keine Antwort
    _selfRunning = 0;
    _selfAllMode = false;
    _dirty = true;
  }

  // own update mode: service HTTP, reboot on success/timeout
  if (_selfUpd == SELFUPD_ACTIVE) {
    _webUpd.loop();
    if (_webUpd.updateDone()) {
      render();  // one last "OK" frame
      delay(1500);  // let the browser receive the response page
      rebootWithScreen("Update OK");
    }
    if (now >= _selfUpdDeadline && !_webUpd.uploadActive())
      rebootWithScreen("Timeout");
    _dirty = true;  // keep the client counter fresh
  }

  // ESP-NOW push: drive the sender / wait for the device's reboot
  if (_screen == SCR_PUSH || _screen == SCR_BULK) {
    if (_pushPhase == 0) {
      _pushTx.loop();
      _dirty = true;  // live progress
      if (_pushTx.state() == EspNowPushSender::DONE) {
        _pushFile.close();
        _pushPhase = 1;
        _pushWaitMs = 0;
        _pushDeadline = now + 25000;
      } else if (_pushTx.state() == EspNowPushSender::FAILED) {
        _pushFile.close();
        if (_bulk) {
          _bulkFail++;
          bulkNext();
        } else {
          snprintf(_pushResult, sizeof(_pushResult), "Fehler (Code %u)",
                   _pushTx.finalStatus());
          _pushPhase = 2;
        }
      }
    } else if (_pushPhase == 1) {
      if (now - _pushWaitMs >= 3000) {
        _pushWaitMs = now;
        sendDiscoverReq(_editDev.deviceType);
      }
      if (now >= _pushDeadline) {
        if (_bulk) {
          _bulkFail++;
          bulkNext();
        } else {
          snprintf(_pushResult, sizeof(_pushResult), "Keine Rueckmeldung");
          _pushPhase = 2;
        }
        _dirty = true;
      }
    }
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

void UiController::apName(char *out, size_t n, const Device &d) const {
  char mac[7];
  macSuffix(d.mac, mac);
  snprintf(out, n, "infinitag-%s-%s",
           d.deviceType == DEV_STATION ? "sta" : "tgt", mac);
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

void UiController::drawFooter(const char *text) {
  // Inverted mini bar in a tiny font: reads as a footer, not as another
  // menu row (rows end at baseline 54, descenders at 56 – the bar starts
  // at 57 and stays clear of them).
  _oled.drawBox(0, 57, 128, 7);
  _oled.setDrawColor(0);
  _oled.setFont(u8g2_font_4x6_tf);
  _oled.drawStr(2, 63, text);
  _oled.setDrawColor(1);
  _oled.setFont(u8g2_font_6x10_tf);
}

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

  // Scrollbar on the right edge once the list does not fit: track over
  // the body area, thumb position/size mirror cursor and list length.
  if (rowCount > 4) {
    const int trackY = 17, trackH = 38;
    oled.drawVLine(127, trackY, trackH);
    int thumbH = trackH * 4 / rowCount;
    if (thumbH < 5) thumbH = 5;
    const int thumbY =
        trackY + (trackH - thumbH) * cursor / (rowCount - 1);
    oled.drawBox(125, thumbY, 3, thumbH);
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
      drawFooter("Drehen + Druecken");
      break;
    }

    case SCR_DEVICE_MENU: {
      // Title: type, MAC suffix and firmware version of the device.
      char title[24];
      char mac[7];
      macSuffix(_editDev.mac, mac);
      snprintf(title, sizeof(title), "%s %s v%u.%u.%u",
               _editDev.deviceType == DEV_STATION ? "Station" : "Target", mac,
               _editDev.info.fw_major, _editDev.info.fw_minor,
               _editDev.info.fw_patch);
      drawTitle(title);
      const bool st = _editDev.deviceType == DEV_STATION;
      struct Ctx { const char **items; } ctx{st ? DEVMENU_STATION
                                               : DEVMENU_TARGET};
      drawRows(_oled, _cursor,
               st ? DEVMENU_STATION_COUNT : DEVMENU_TARGET_COUNT,
               [](void *c, int i, char *b, size_t n) {
                 snprintf(b, n, "%s", ((Ctx *)c)->items[i]);
               },
               &ctx);
      break;
    }

    case SCR_DEVICE_LIST: {
      const int count = (int)_reg.count(_listType);
      drawTitle(_listType == DEV_STATION ? "Stationen" : "Targets", count);

      struct Ctx {
        UiController *ui;
        uint8_t type;
      } ctx{this, _listType};
      drawRows(_oled, _cursor, count + LIST_STATIC_ROWS,
               [](void *c, int i, char *b, size_t n) {
                 Ctx *x = (Ctx *)c;
                 if (i == 0) { snprintf(b, n, "< Zurueck"); return; }
                 if (i == 1) { snprintf(b, n, "Neu suchen"); return; }
                 if (i == 2) { snprintf(b, n, "Alle aktualisieren"); return; }
                 Device *d = x->ui->_reg.byIndex(x->type,
                                                 i - LIST_STATIC_ROWS);
                 if (!d) { b[0] = '\0'; return; }
                 char mac[7];
                 macSuffix(d->mac, mac);
                 // '^' = a newer firmware of this type exists in the list
                 snprintf(b, n, "%s%s %4ddBm",
                          x->ui->isOutdated(*d) ? "^" : " ", mac,
                          d->info.rssi_self);
               },
               &ctx);
      if (count == 0) drawFooter("suche Geraete...");
      break;
    }

    case SCR_DEVICE_EDIT: {
      char title[24];
      char mac[7];
      macSuffix(_editDev.mac, mac);
      snprintf(title, sizeof(title), "%s %s",
               _editDev.deviceType == DEV_STATION ? "Station" : "Target", mac);
      drawTitle(title);

      const int rows = _fieldCount + 2;
      struct Ctx { UiController *ui; } ctx{this};
      drawRows(_oled, _cursor, rows,
               [](void *c, int i, char *b, size_t n) {
                 UiController *ui = ((Ctx *)c)->ui;
                 if (i == ui->_fieldCount) {
                   snprintf(b, n, "[Speichern]");
                   return;
                 }
                 if (i == ui->_fieldCount + 1) {
                   snprintf(b, n, "< Zurueck");
                   return;
                 }
                 const Field &f = ui->_fields[i];
                 char val[12];
                 switch (f.fmt) {
                   case FMT_LED: {
                     // channel letters of the set dies, e.g. 0b1001 -> "RW"
                     int k = 0;
                     if (f.value & LED_R) val[k++] = 'R';
                     if (f.value & LED_G) val[k++] = 'G';
                     if (f.value & LED_B) val[k++] = 'B';
                     if (f.value & LED_W) val[k++] = 'W';
                     val[k] = '\0';
                     break;
                   }
                   case FMT_BITS:
                     snprintf(val, sizeof(val), "%c%c%c",
                              (f.value & 1) ? '1' : '-',
                              (f.value & 2) ? '2' : '-',
                              (f.value & 4) ? '3' : '-');
                     break;
                   case FMT_STATION:
                     if (ui->_staPickCount == 0) {
                       snprintf(val, sizeof(val), "--");
                     } else {
                       macSuffix(ui->_staPick[f.value], val);
                     }
                     break;
                   case FMT_SOUND:
                     snprintf(val, sizeof(val), "%02ld %s", (long)f.value,
                              soundName((uint8_t)f.value));
                     break;
                   default:
                     snprintf(val, sizeof(val), "%ld%s", (long)f.value,
                              f.unit ? f.unit : "");
                     break;
                 }
                 const bool editing =
                     ui->_valueEditing && i == ui->_cursor;
                 snprintf(b, n, "%-10s:%s%s%s", f.label,
                          editing ? "[" : " ", val, editing ? "]" : "");
               },
               &ctx);

      if (_statusLine[0]) drawFooter(_statusLine);
      else if (_valueEditing) drawFooter("Push = uebernehmen");
      break;
    }

    case SCR_SOUND_TEST: {
      char title[24];
      char mac[7];
      macSuffix(_editDev.mac, mac);
      snprintf(title, sizeof(title), "Sound-Test %s", mac);
      drawTitle(title);
      struct Ctx { UiController *ui; } ctx{this};
      drawRows(_oled, _cursor, 3,
               [](void *c, int i, char *b, size_t n) {
                 UiController *ui = ((Ctx *)c)->ui;
                 if (i == 0) { snprintf(b, n, "< Zurueck"); return; }
                 if (i == 1) {
                   const bool e = ui->_valueEditing;
                   snprintf(b, n, "Sound:%s%02u %s%s", e ? "[" : " ",
                            ui->_testSound, soundName(ui->_testSound),
                            e ? "]" : "");
                   return;
                 }
                 snprintf(b, n, "[Abspielen]");
               },
               &ctx);
      break;
    }

    case SCR_SELF_TEST: {
      char title[24];
      char mac[7];
      macSuffix(_editDev.mac, mac);
      snprintf(title, sizeof(title), "Selbsttest %s", mac);
      drawTitle(title);
      struct Ctx { UiController *ui; } ctx{this};
      drawRows(_oled, _cursor, SELFTEST_ROWS,
               [](void *c, int i, char *b, size_t n) {
                 UiController *ui = ((Ctx *)c)->ui;
                 if (i < 2) { snprintf(b, n, "%s", SELFTEST_ITEMS[i]); return; }
                 const char r = ui->_selfResult[i - 2];
                 const char *res;
                 switch (r) {
                   case 'O': res = "OK"; break;
                   case 'X': res = "FEHLER"; break;
                   case 'T': res = "Timeout"; break;
                   case 'U': res = "n/a"; break;
                   case '?': res = "k.Antw."; break;
                   case '.': res = "..."; break;
                   default:  res = ""; break;
                 }
                 snprintf(b, n, "%-11s %s", SELFTEST_ITEMS[i], res);
               },
               &ctx);
      drawFooter(_selfRunning ? "Test laeuft..." : "Push = starten");
      break;
    }

    case SCR_DEV_UPDATE: {
      drawTitle("Firmware-Update");
      char ap[32];
      apName(ap, sizeof(ap), _editDev);
      switch (_updState) {
        case UPD_WAIT_ACK:
          _oled.drawStr(0, 30, "Sende Kommando...");
          break;
        case UPD_NO_ACK:
          _oled.drawStr(0, 30, "Keine Antwort!");
          drawFooter("Push = Zurueck");
          break;
        case UPD_ACTIVE:
          _oled.drawStr(0, 24, "Geraet im Update-Modus");
          _oled.drawStr(0, 34, "WLAN:");
          _oled.drawStr(0, 44, ap);
          _oled.drawStr(0, 54, "http://192.168.4.1");
          drawFooter("warte auf Neustart...");
          break;
        case UPD_DEVICE_BACK:
          _oled.drawStr(0, 30, "Geraet wieder da:");
          _oled.drawStr(0, 42, _updResult);
          drawFooter("zurueck zur Liste...");
          break;
      }
      break;
    }

    case SCR_PUSH:
    case SCR_BULK: {
      char title[24];
      char mac[7];
      macSuffix(_editDev.mac, mac);
      if (_screen == SCR_BULK)
        snprintf(title, sizeof(title), "Alle aktualisieren");
      else
        snprintf(title, sizeof(title), "Funk-Update %s", mac);
      drawTitle(title);

      if (_pushPhase == 0) {
        char line[24];
        const size_t total = _pushTx.bytesTotal();
        snprintf(line, sizeof(line), "Sende an %s...", mac);
        _oled.drawStr(0, 28, line);
        snprintf(line, sizeof(line), "%u%% (%u/%u KB)",
                 total ? (unsigned)(_pushTx.bytesDone() * 100 / total) : 0,
                 (unsigned)(_pushTx.bytesDone() / 1024),
                 (unsigned)(total / 1024));
        _oled.drawStr(0, 42, line);
        drawFooter("K1 = Abbruch");
      } else if (_pushPhase == 1) {
        _oled.drawStr(0, 32, "Geflasht - warte auf");
        _oled.drawStr(0, 44, "Neustart des Geraets");
      } else {
        _oled.drawStr(0, 36, _pushResult);
        if (_screen == SCR_BULK) {
          char line[24];
          snprintf(line, sizeof(line), "Bericht siehe oben");
          (void)line;
        }
        drawFooter("Push = Zurueck");
      }
      break;
    }

    case SCR_TOOLS_MENU: {
      drawTitle("Tools");
      struct Ctx { const char **items; } ctx{TOOLS_ITEMS};
      drawRows(_oled, _cursor, TOOLS_COUNT,
               [](void *c, int i, char *b, size_t n) {
                 snprintf(b, n, "%s", ((Ctx *)c)->items[i]);
               },
               &ctx);
      if (_toolsMsg[0] && millis() < _toolsMsgUntil) {
        drawFooter(_toolsMsg);
        _dirty = true;  // repaint once the message expires
      }
      break;
    }

    case SCR_SELF_UPDATE: {
      drawTitle("Update-Modus");
      if (_selfUpd == SELFUPD_REFUSED) {
        _oled.drawStr(0, 30, "Akku zu leer!");
        _oled.drawStr(0, 42, "(min. 3.6 V oder USB)");
        drawFooter("Push = Zurueck");
      } else if (_webUpd.updateDone()) {
        _oled.drawStr(0, 36, "Update OK - Neustart");
      } else {
        _oled.drawStr(0, 24, _selfUpdAp);
        _oled.drawStr(0, 36, "http://192.168.4.1");
        char buf[24];
        if (_webUpd.uploadActive()) {
          snprintf(buf, sizeof(buf), "Upload laeuft...");
        } else {
          snprintf(buf, sizeof(buf), "Clients: %d", _webUpd.clientCount());
        }
        _oled.drawStr(0, 48, buf);
        drawFooter("K1 = Abbruch (Reboot)");
      }
      break;
    }

    case SCR_IMAGES: {
      drawTitle("Geraete-Images");
      struct Ctx { UiController *ui; } ctx{this};
      drawRows(_oled, _cursor, 3,
               [](void *c, int i, char *b, size_t n) {
                 UiController *ui = ((Ctx *)c)->ui;
                 if (i == 0) { snprintf(b, n, "< Zurueck"); return; }
                 const uint8_t t = i == 1 ? DEV_STATION : DEV_TARGET;
                 const ImageInfo &inf = ui->_images.info(t);
                 if (inf.present) {
                   snprintf(b, n, "%-8s v%u.%u.%u %ukB",
                            t == DEV_STATION ? "Station:" : "Target:",
                            inf.major, inf.minor, inf.patch,
                            (unsigned)(inf.size / 1024));
                 } else {
                   snprintf(b, n, "%-8s --",
                            t == DEV_STATION ? "Station:" : "Target:");
                 }
               },
               &ctx);
      drawFooter("Push = loeschen");
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
                   char tgt[7], sta[7];
                   macSuffix(h.targetMac, tgt);
                   macSuffix(h.stationMac, sta);
                   snprintf(b, n, "%3lus %s>%s %02u",
                            (unsigned long)(age > 999 ? 999 : age), tgt, sta,
                            h.soundId);
                 },
                 &ctx);
      }
      drawFooter("Push = Zurueck");
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
      const float vbat = readVbat();
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

  }

  _oled.sendBuffer();
}
