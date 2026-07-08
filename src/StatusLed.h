// Single SK6812RGBW status LED, color codes analog to the station
// (purple = setup mode, green = OK, red = error, blue = radio traffic).

#pragma once
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

class StatusLed {
 public:
  void begin();
  void tick();  // handles flash timeout, call from loop()

  void idle();                       // dim green
  void setupMode();                  // purple (station setup running)
  void flashTx();                    // short blue blink (packet sent)
  void flashOk();                    // green blink (ACK received)
  void flashError();                 // red blink (NACK / timeout)

 private:
  void set(uint8_t r, uint8_t g, uint8_t b, uint8_t w);
  void flash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms);

  Adafruit_NeoPixel _px;
  uint32_t _flashUntil = 0;
  bool _inSetupMode = false;

 public:
  StatusLed();
};
