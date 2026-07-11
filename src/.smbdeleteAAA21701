#include "InputController.h"
#include "Config.h"

volatile int32_t InputController::s_quarters = 0;
volatile uint8_t InputController::s_lastAB = 0;

// Full-step quadrature transition table: index = (prevAB << 2) | curAB.
// Invalid transitions (bounce) contribute 0.
static const int8_t QDEC[16] = {0,  -1, 1, 0,   1, 0, 0, -1,
                                -1, 0,  0, 1,   0, 1, -1, 0};

static constexpr uint32_t DEBOUNCE_MS = 30;

void IRAM_ATTR InputController::isrEncoder() {
  const uint8_t a = (uint8_t)digitalRead(cfg::PIN_ENC_A);
  const uint8_t b = (uint8_t)digitalRead(cfg::PIN_ENC_B);
  const uint8_t cur = (a << 1) | b;
  s_quarters += QDEC[(s_lastAB << 2) | cur];
  s_lastAB = cur;
}

void InputController::begin() {
  pinMode(cfg::PIN_ENC_A, INPUT_PULLUP);  // KY-040 has 10k onboard pullups too
  pinMode(cfg::PIN_ENC_B, INPUT_PULLUP);
  s_lastAB = ((uint8_t)digitalRead(cfg::PIN_ENC_A) << 1) |
             (uint8_t)digitalRead(cfg::PIN_ENC_B);
  attachInterrupt(digitalPinToInterrupt(cfg::PIN_ENC_A), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(cfg::PIN_ENC_B), isrEncoder, CHANGE);

  _btn[BTN_ENC].pin = cfg::PIN_ENC_SW;
  _btn[BTN_K1].pin = cfg::PIN_BTN_K1;
  _btn[BTN_K2].pin = cfg::PIN_BTN_K2;
  _btn[BTN_K3].pin = cfg::PIN_BTN_K3;
  _btn[BTN_K4].pin = cfg::PIN_BTN_K4;
  for (auto &b : _btn) pinMode(b.pin, INPUT_PULLUP);
}

void InputController::poll() {
  const uint32_t now = millis();
  for (auto &b : _btn) {
    const bool raw = (digitalRead(b.pin) == LOW);  // active low
    if (raw != b.raw) {
      b.raw = raw;
      b.lastChangeMs = now;
    }
    if (raw != b.stable && (now - b.lastChangeMs) >= DEBOUNCE_MS) {
      b.stable = raw;
      if (raw) b.pressEvent = true;  // falling edge = press
    }
  }
}

int InputController::takeEncoderDelta() {
  noInterrupts();
  const int32_t q = s_quarters;
  interrupts();
  // One mechanical detent on the KY-040 = 4 quarter steps.
  const int32_t total = q - _takenQuarters;
  const int32_t steps = total / 4;
  _takenQuarters += steps * 4;
  return (int)steps;
}

bool InputController::takePress(Button b) {
  if (_btn[b].pressEvent) {
    _btn[b].pressEvent = false;
    return true;
  }
  return false;
}

bool InputController::isHeld(Button b) const { return _btn[b].stable; }
