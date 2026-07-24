// OLED menu state machine (Doc 18 §5) for the 0.96" SSD1315 128x64.
// Two-color panel: rows 0..15 yellow (title bar), rows 16..63 blue (body).
//
// Interaction model (since 2026-07-11): EVERYTHING is reachable with the
// encoder alone (turn + push). Every level has an explicit "< Zurueck" row.
// The OLED buttons are optional shortcuts only:
//   K1 = back / cancel value edit   K2 (hold) = x10 step / (press) rescan
//   K3 = identify blink on/off      K4 = OK (same as encoder push)
// Fast turning accelerates value edits (x10 without any button).
//
// Since protocol v0x02 devices are identified by MAC only (suffix shown,
// e.g. "220AAC"); the SETUP flow is gone. New: firmware update screens
// (remote device via UPDATE_BEGIN, own firmware via SoftAP web updater)
// and an outdated-firmware marker '^' in the device lists.

#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

#include "DeviceRegistry.h"
#include "EspNowPush.h"
#include "EspNowService.h"
#include "ImageStore.h"
#include "InputController.h"
#include "VersionMemo.h"
#include "WebUpdateService.h"

class UiController {
 public:
  UiController(EspNowService &net, DeviceRegistry &reg, InputController &in);

  void begin();
  void tick();                      // input handling + periodic timers
  void onPacket(const RxPacket &rx);  // feed every received packet here

  // simple 3-line status screen, used by the blocking net update flow
  // (public: the C-style progress callback needs to reach it)
  void netScreen(const char *l1, const char *l2 = nullptr,
                 const char *l3 = nullptr, const char *footer = nullptr);

 private:
  // --- screens ---------------------------------------------------------------
  // Device-first navigation: pick the device from the list, THEN choose an
  // action in its device menu (Prusa-style).
  enum Screen : uint8_t {
    SCR_MAIN,
    SCR_DEVICE_LIST,   // stations or targets, see _listType
    SCR_DEVICE_MENU,   // actions for the selected device (_editDev)
    SCR_DEVICE_EDIT,
    SCR_SOUND_TEST,    // pick sound + fire CFG_TEST_SOUND at chosen station
    SCR_SELF_TEST,     // remote self-test (DEBUG_CMD/DEBUG_RESULT)
    SCR_CALIBRATE,     // calibration mode on the station (laser + IR on)
    SCR_DEV_UPDATE,    // sent UPDATE_BEGIN, show the device's AP info
    SCR_PUSH,          // ESP-NOW firmware push to _editDev (Doc 21 E3)
    SCR_BULK,          // push to every outdated device of the list type
    SCR_LIVE_MONITOR,
    SCR_TOOLS_MENU,    // Firmware-Info / own update mode
    SCR_TOOLS_INFO,
    SCR_SELF_UPDATE,   // own SoftAP web updater (ends in reboot)
    SCR_IMAGES,        // stored device firmware images (Doc 21 E1)
  };

  // Static rows before the devices in a device list:
  // 0 = "< Zurueck", 1 = "Neu suchen", 2 = "Alle aktualisieren"
  static constexpr uint8_t LIST_STATIC_ROWS = 3;

  // --- editing ---------------------------------------------------------------
  static constexpr size_t MAX_FIELDS = 8;
  enum FieldFmt : uint8_t {
    FMT_NUM,      // plain number (+ optional unit)
    FMT_BITS,     // sw_channels: "12-" style
    FMT_LED,      // LED channel mask: letters "R".."RGBW"
    FMT_SOUND,    // sound id shown as "06 Daemon" (SoundCatalog)
    FMT_LASER,    // 0 = Aus, 1 = An, 2.. = Nachleuchten (v-1) x 0,5 s
  };
  struct Field {
    const char *label;
    int32_t value, min, max, step;
    const char *unit;   // may be nullptr
    FieldFmt fmt;
  };

  // --- live monitor ------------------------------------------------------------
  static constexpr size_t MONITOR_RING = 8;
  struct HitEntry {
    uint32_t ms;
    uint8_t targetMac[6];
    uint8_t shooterId;  // ir_id from the IR telegram (v0x03)
    uint8_t soundId;
    uint8_t damage;
  };

  // --- device update handshake ---------------------------------------------------
  // WAIT_ACK -> ACTIVE (device opened its AP). While ACTIVE the box polls
  // with DISCOVER_REQ until the device reboots back onto ESP-NOW ->
  // DEVICE_BACK shows the result (version compare) and auto-returns.
  enum UpdState : uint8_t { UPD_WAIT_ACK, UPD_ACTIVE, UPD_NO_ACK,
                            UPD_DEVICE_BACK };

  // --- own update mode -------------------------------------------------------------
  enum SelfUpdState : uint8_t { SELFUPD_OFF, SELFUPD_REFUSED, SELFUPD_ACTIVE };

  // --- helpers -----------------------------------------------------------------
  void gotoScreen(Screen s);
  void handleInput();
  void handleTimers();
  void render();

  void startDiscovery(uint8_t deviceType);
  void sendDiscoverReq(uint8_t deviceType);  // request only, keeps registry
  void sendIdentify(const Device &d);
  void enterEdit(Device &d);
  void sendCfgWrite();
  void sendTestSound();
  void runSelfTest(uint8_t test);  // sends DEBUG_CMD, arms deadline
  void sendCalibrate(uint8_t minutes);  // DEBUG_CMD Test 6 (0 = aus)
  void beginDeviceUpdate();        // sends UPDATE_BEGIN to _editDev
  String webPage(const char *activePath, const String &section);
  void buildWebPages();            // root page + /wlan, /images, /log routes
  bool beginPush(const Device &d); // start the ESP-NOW push (image needed)
  void bulkNext();                 // advance the "Alle aktualisieren" queue
  void beginSelfUpdate();          // battery check + own SoftAP updater
  // Blocking guided flow "Nach Updates suchen" (Doc 21 E2): WLAN ->
  // GitHub -> device images into the store -> self update. Always ends
  // in ESP.restart().
  void runNetUpdate(bool resumed = false);
  // Draws a final "reboot" frame (the OLED keeps showing the last frame
  // across ESP.restart(), so without this the user cannot tell that the
  // reboot happened), then restarts. Never returns.
  void rebootWithScreen(const char *line);
  float readVbat() const;

  // '^' marker: a newer firmware of the same device type is known –
  // either currently in the list or ever seen before (VersionMemo).
  bool isOutdated(const Device &d) const;

  void drawTitle(const char *title, int count = -1);
  void drawFooter(const char *text);
  static void macSuffix(const uint8_t mac[6], char *out);  // "AABBCC"
  void apName(char *out, size_t n, const Device &d) const;

  // --- members -----------------------------------------------------------------
  EspNowService &_net;
  DeviceRegistry &_reg;
  InputController &_in;
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C _oled;

  Screen _screen = SCR_MAIN;
  uint8_t _cursor = 0;

  // list state
  uint8_t _listType = 0;        // DEV_STATION / DEV_TARGET
  bool _identifyEnabled = true;
  uint32_t _lastIdentifyMs = 0;
  uint8_t _discoverToken = 0;

  // edit state
  Device _editDev;              // copy of the device being edited
  Field _fields[MAX_FIELDS];
  uint8_t _fieldCount = 0;
  bool _valueEditing = false;
  int32_t _valueBackup = 0;
  bool _awaitingAck = false;
  uint32_t _ackDeadline = 0;
  char _statusLine[24] = "";    // "Gespeichert" / "Fehler: ..."

  // sound test state
  uint8_t _testSound = 0;

  // self-test state (tests 1..5, results indexed test-1)
  static constexpr uint8_t SELF_TESTS = 5;
  uint8_t _calState = 0;        // 0 = warte auf ACK, 1 = laeuft, 2 = keine Antwort
  uint32_t _calDeadline = 0;    // ACK-Timeout
  char _selfResult[SELF_TESTS] = {' ', ' ', ' ', ' ', ' '};
  uint8_t _selfRunning = 0;     // DebugTest id currently awaited, 0 = none
  bool _selfAllMode = false;    // "Alle testen": auto-advance on result
  uint32_t _selfDeadline = 0;

  // ESP-NOW push state (single device + bulk mode)
  EspNowPushSender _pushTx;
  File _pushFile;
  uint32_t _pushImgKey = 0;     // version key of the pushed image
  uint32_t _pushWaitMs = 0;     // discovery poll while waiting for reboot
  uint32_t _pushDeadline = 0;   // give-up waiting for the device to return
  uint8_t _pushPhase = 0;       // 0=push, 1=wait reboot, 2=result
  char _pushResult[24] = "";
  // bulk mode bookkeeping
  bool _bulk = false;
  size_t _bulkPos = 0;
  uint8_t _bulkOk = 0, _bulkFail = 0, _bulkSkip = 0;

  // device update state
  UpdState _updState = UPD_WAIT_ACK;
  uint32_t _updAckDeadline = 0;
  uint32_t _updOldVer = 0;      // version key before the update
  uint32_t _updPollMs = 0;      // last DISCOVER_REQ poll while ACTIVE
  uint32_t _updBackMs = 0;      // when the device reappeared
  char _updResult[24] = "";     // "Update OK: v0.2.2" / "Zurueck, unveraendert"

  // own update mode
  WebUpdateService _webUpd;
  String _rootPageHtml;  // persistent root page (WebUpdateService keeps a ptr)
  SelfUpdState _selfUpd = SELFUPD_OFF;
  uint32_t _selfUpdDeadline = 0;
  char _selfUpdAp[32] = "";

  // highest firmware version ever seen per device type (NVS)
  VersionMemo _memo;

  // stored device firmware images (LittleFS, Doc 21 E1)
  ImageStore _images;
  char _toolsMsg[24] = "";      // short-lived footer note in the tools menu
  uint32_t _toolsMsgUntil = 0;

  // live monitor
  HitEntry _hits[MONITOR_RING];
  size_t _hitCount = 0;

  uint32_t _lastRenderMs = 0;
  bool _dirty = true;
};
