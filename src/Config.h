// Pin map and constants for the Infinitag Config-Box.
// Hardware: ESP32-C3 Super Mini, see wissensbasis/18-config-tool.md §4.3.
// GPIO plan v3 (2026-07-09): status LED dropped, battery sense added.

#pragma once
#include <stdint.h>

namespace cfg {

// --- Firmware version (reported in DISCOVER_REPLY, shown in Tools menu) ----
// Bump on every flashed release – the version check in the device list
// compares these numbers.
constexpr uint8_t FW_MAJOR = 0;
constexpr uint8_t FW_MINOR = 2;
constexpr uint8_t FW_PATCH = 2;

// --- GPIO plan v3 (C3 Super Mini) -------------------------------------------
constexpr uint8_t PIN_ENC_A   = 0;   // rotary encoder A (interrupt)
constexpr uint8_t PIN_ENC_B   = 1;   // rotary encoder B (interrupt)
constexpr uint8_t PIN_VBAT    = 3;   // battery voltage via divider (ADC1_CH3)
constexpr uint8_t PIN_BTN_K1  = 4;   // OLED module K1 = menu/back
constexpr uint8_t PIN_BTN_K2  = 5;   // OLED module K2 = up / big-step modifier
constexpr uint8_t PIN_I2C_SDA = 6;
constexpr uint8_t PIN_I2C_SCL = 7;
constexpr uint8_t PIN_BTN_K3  = 10;  // OLED module K3 = down / identify toggle
constexpr uint8_t PIN_BTN_K4  = 20;  // OLED module K4 = OK
constexpr uint8_t PIN_ENC_SW  = 21;  // encoder push, active low
constexpr int8_t  ENC_DIRECTION = -1;  // +1 or -1, flips rotation sense to match wiring
// GPIO2 = free (strapping pin, keep loads without pulldown only)
// GPIO8 = onboard blue LED (inverted), usable as heartbeat later
// GPIO9 = onboard BOOT button, keep free

// --- Battery sense -------------------------------------------------------------
// Divider: VBAT --[100k]-- PIN_VBAT --[10k+12k=22k]-- GND  (6.4 V -> ~1.15 V)
// (originally planned 47k low side; built with 10k+12k series, 2026-07-09)
constexpr float VBAT_DIVIDER = (100.0f + 22.0f) / 22.0f;

// --- Radio -----------------------------------------------------------------------
constexpr uint8_t ESPNOW_CHANNEL = 1;  // all Infinitag devices are pinned here

// --- Timing (Doc 18 §7) ----------------------------------------------------------
constexpr uint32_t IDENTIFY_PERIOD_MS      = 500;   // resend interval
constexpr uint8_t  IDENTIFY_DURATION_100MS = 7;     // device blinks 700 ms
constexpr uint32_t ACK_TIMEOUT_MS          = 800;   // CFG_WRITE -> CFG_ACK
constexpr uint32_t RENDER_INTERVAL_MS      = 50;    // ~20 fps OLED

// --- Firmware update (Doc 18, OTA since 2026-07-12) --------------------------------
constexpr uint8_t  UPDATE_TIMEOUT_MIN     = 5;      // device SoftAP window
constexpr uint32_t UPDATE_ACK_TIMEOUT_MS  = 1500;   // UPDATE_BEGIN -> UPDATE_ACK
constexpr uint32_t SELF_UPDATE_TIMEOUT_MS = 10UL * 60UL * 1000UL;  // own AP window
// Below this pack voltage no update is started (box must not die mid-flash).
// Readings < 3.0 V mean "running from USB" (divider floats) -> always OK.
constexpr float VBAT_MIN_FOR_UPDATE = 3.6f;

// --- Sounds ------------------------------------------------------------------------
constexpr uint8_t SOUND_ID_MAX = 13;  // StationSounds/sound-effects 0..13

}  // namespace cfg
