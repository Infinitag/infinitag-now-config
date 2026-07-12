// Infinitag Config-Box – main entry point.
//
// Hardware: ESP32-C3 Super Mini + 0.96" SSD1315 OLED (I2C, 4 buttons)
//           + KY-040 rotary encoder. Status feedback happens on the OLED.
// Concept & protocol: wissensbasis/18-config-tool.md and
//                     wissensbasis/12-refactor-station-v2.md §3.

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "DeviceRegistry.h"
#include "FwMarker.h"
#include "EspNowService.h"
#include "InputController.h"
#include "UiController.h"

// Firmware identity for the image store / ESP-NOW push (Doc 21 E1).
INOW_FW_MARKER(inow::DEV_CONFIG_BOX, cfg::FW_MAJOR, cfg::FW_MINOR,
               cfg::FW_PATCH)

static EspNowService gNet;
static DeviceRegistry gRegistry;
static InputController gInput;
static UiController gUi(gNet, gRegistry, gInput);

void setup() {
  Serial.begin(115200);  // USB-CDC, no waiting: box must boot without host

  gInput.begin();

  Wire.begin(cfg::PIN_I2C_SDA, cfg::PIN_I2C_SCL);
  gUi.begin();

  if (!gNet.begin(cfg::ESPNOW_CHANNEL)) {
    Serial.println("[ERR] ESP-NOW init failed");
  } else {
    Serial.printf("[OK] Config-Box up, MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                  gNet.ownMac()[0], gNet.ownMac()[1], gNet.ownMac()[2],
                  gNet.ownMac()[3], gNet.ownMac()[4], gNet.ownMac()[5]);
  }
}

void loop() {
  gInput.poll();

  RxPacket rx;
  while (gNet.receive(rx)) gUi.onPacket(rx);

  gUi.tick();
}
