#include "StatusLed.h"
#include "Config.h"

StatusLed::StatusLed() : _px(1, cfg::PIN_NEOPIXEL, NEO_GRBW + NEO_KHZ800) {}

void StatusLed::begin() {
  _px.begin();
  _px.setBrightness(60);
  idle();
}

void StatusLed::set(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  _px.setPixelColor(0, _px.Color(r, g, b, w));
  _px.show();
}

void StatusLed::flash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
  set(r, g, b, 0);
  _flashUntil = millis() + ms;
}

void StatusLed::tick() {
  if (_flashUntil != 0 && millis() >= _flashUntil) {
    _flashUntil = 0;
    _inSetupMode ? setupMode() : idle();
  }
}

void StatusLed::idle() {
  _inSetupMode = false;
  set(0, 20, 0, 0);
}

void StatusLed::setupMode() {
  _inSetupMode = true;
  set(80, 0, 80, 0);
}

void StatusLed::flashTx() { flash(0, 0, 120, 60); }
void StatusLed::flashOk() { flash(0, 120, 0, 120); }
void StatusLed::flashError() { flash(150, 0, 0, 200); }
