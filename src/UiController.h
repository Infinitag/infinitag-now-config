// OLED menu state machine (Doc 18 §5 + §8) for the 0.96" SSD1315 128x64.
// Two-color panel: rows 0..15 yellow (title bar), rows 16..63 blue (body).
//
// Interaction model (since 2026-07-11): EVERYTHING is reachable with the
// encoder alone (turn + push). Every level has an explicit "< Zurueck" row.
// The OLED buttons are optional shortcuts only:
//   K1 = back / cancel value edit   K2 (hold) = x10 step / (press) rescan
//   K3 = identify blink on/off      K4 = OK (same as encoder push)
// Fast turning accelerates value edits (x10 without any button).

#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

#include "DeviceRegistry.h"
#include "EspNowService.h"
#include "InputController.h"

class UiController {
 public:
  UiController(EspNowService &net, DeviceRegistry &reg, InputController &in);

  void begin();
  void tick();                      // input handling + periodic timers
  void onPacket(const RxPacket &rx);  // feed every received packet here

 private:
  // --- screens ---------------------------------------------------------------
  // Device-first navigation (since 2026-07-11): pick the device from the
  // list, THEN choose an action in its device menu (Prusa-style).
  enum Screen : uint8_t {
    SCR_MAIN,
    SCR_DEVICE_LIST,   // stations or targets, see _listType
    SCR_DEVICE_MENU,   // actions for the selected device (_editDev)
    SCR_DEVICE_EDIT,
    SCR_SETUP_WAIT,    // "Neue Station": SETUP_BEGIN active
    SCR_SOUND_TEST,    // pick sound + fire CFG_TEST_SOUND at chosen station
    SCR_SELF_TEST,     // remote self-test (DEBUG_CMD/DEBUG_RESULT)
    SCR_LIVE_MONITOR,
    SCR_TOOLS_INFO,
    SCR_NOTICE,        // one-line info screen (e.g. "Web-UI ab V0.2")
  };

  // Static rows before the devices in a device list:
  // stations: 0 = "< Zurueck", 1 = "Neu suchen", 2 = "Neue Station (Stab)"
  // targets:  0 = "< Zurueck", 1 = "Neu suchen"
  uint8_t listStaticRows() const;

  // --- editing ---------------------------------------------------------------
  static constexpr size_t MAX_FIELDS = 8;
  struct Field {
    const char *label;
    int32_t value, min, max, step;
    const char *unit;   // may be nullptr
    bool isBitmask;     // render as ●○● (sw_channels)
  };

  // --- live monitor ------------------------------------------------------------
  static constexpr size_t MONITOR_RING = 8;
  struct HitEntry {
    uint32_t ms;
    uint8_t targetId, stationId, soundId;
  };

  // --- helpers -----------------------------------------------------------------
  void gotoScreen(Screen s);
  void handleInput();
  void handleTimers();
  void render();

  void startDiscovery(uint8_t deviceType);
  void sendIdentify(const Device &d);
  void enterEdit(Device &d);
  void sendCfgWrite();
  void sendTestSound();
  void beginSetupMode();
  void runSelfTest(uint8_t test);  // sends DEBUG_CMD, arms deadline

  void drawTitle(const char *title, int count = -1);
  void drawFooter(const char *text);
  void macSuffix(const uint8_t mac[6], char *out);  // "AABBCC"

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

  // setup state
  uint32_t _setupStartedMs = 0;
  uint32_t _lastSetupTxMs = 0;
  char _setupResult[24] = "";
  uint8_t _setupNewId = 1;   // id to assign, sent in the SETUP_BEGIN header

  // sound test state
  uint8_t _testSound = 0;

  // self-test state (tests 1..5, results indexed test-1)
  static constexpr uint8_t SELF_TESTS = 5;
  char _selfResult[SELF_TESTS] = {' ', ' ', ' ', ' ', ' '};
  uint8_t _selfRunning = 0;     // DebugTest id currently awaited, 0 = none
  bool _selfAllMode = false;    // "Alle testen": auto-advance on result
  uint32_t _selfDeadline = 0;

  // live monitor
  HitEntry _hits[MONITOR_RING];
  size_t _hitCount = 0;

  uint32_t _lastRenderMs = 0;
  bool _dirty = true;
};
