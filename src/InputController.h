// Rotary encoder (interrupt-based quadrature decoding, no PCNT on the C3)
// plus the four OLED-module buttons and the encoder push switch.

#pragma once
#include <Arduino.h>

enum Button : uint8_t {
  BTN_ENC = 0,  // encoder push
  BTN_K1,       // menu / back
  BTN_K2,       // up / big-step modifier (hold)
  BTN_K3,       // down / identify toggle
  BTN_K4,       // OK
  BTN_COUNT
};

class InputController {
 public:
  void begin();

  // Call from loop() as often as possible (button debouncing).
  void poll();

  // Detent steps since the last call. Positive = clockwise.
  int takeEncoderDelta();

  // True exactly once per (debounced) press.
  bool takePress(Button b);

  // Current debounced level (true = pressed). Used for the K2 hold modifier.
  bool isHeld(Button b) const;

 private:
  static void IRAM_ATTR isrEncoder();

  static volatile int32_t s_quarters;   // quarter steps accumulated in ISR
  static volatile uint8_t s_lastAB;

  int32_t _takenQuarters = 0;

  struct Btn {
    uint8_t pin;
    bool stable = false;        // debounced pressed-state (active low inverted)
    bool raw = false;
    uint32_t lastChangeMs = 0;
    bool pressEvent = false;
  };
  Btn _btn[BTN_COUNT];
};
