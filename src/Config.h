// Pin map and constants for the Infinitag Config-Box.
// Hardware: ESP32-C3 Super Mini, see wissensbasis/18-config-tool.md §4.3.

#pragma once
#include <stdint.h>

namespace cfg {

// --- Firmware version (reported in DISCOVER_REPLY, shown in Tools menu) ----
constexpr uint8_t FW_MAJOR = 0;
constexpr uint8_t FW_MINOR = 1;
constexpr uint8_t FW_PATCH = 0;

// --- GPIO plan v2 (C3 Super Mini) -------------------------------------------
constexpr uint8_t PIN_ENC_A    = 0;   // rotary encoder A (interrupt)
constexpr uint8_t PIN_ENC_B    = 1;   // rotary encoder B (interrupt)
constexpr uint8_t PIN_ENC_SW   = 3;   // encoder push, active low
constexpr uint8_t PIN_BTN_K1   = 4;   // OLED module K1 = menu/back
constexpr uint8_t PIN_BTN_K2   = 5;   // OLED module K2 = up / big-step modifier
constexpr uint8_t PIN_I2C_SDA  = 6;
constexpr uint8_t PIN_I2C_SCL  = 7;
constexpr uint8_t PIN_BTN_K3   = 10;  // OLED module K3 = down / identify toggle
constexpr uint8_t PIN_BTN_K4   = 20;  // OLED module K4 = OK
constexpr uint8_t PIN_NEOPIXEL = 21;  // status LED SK6812RGBW

// --- Radio -------------------------------------------------------------------
constexpr uint8_t ESPNOW_CHANNEL = 1;  // all Infinitag devices are pinned here

// --- Timing (Doc 18 §7/§8) ----------------------------------------------------
constexpr uint32_t IDENTIFY_PERIOD_MS      = 500;   // resend interval
constexpr uint8_t  IDENTIFY_DURATION_100MS = 7;     // device blinks 700 ms
constexpr uint32_t SETUP_REBROADCAST_MS    = 5000;  // SETUP_BEGIN repeat
constexpr uint8_t  SETUP_TIMEOUT_S         = 60;
constexpr uint32_t ACK_TIMEOUT_MS          = 800;   // CFG_WRITE -> CFG_ACK
constexpr uint32_t RENDER_INTERVAL_MS      = 50;    // ~20 fps OLED

// --- Sounds -------------------------------------------------------------------
constexpr uint8_t SOUND_ID_MAX = 13;  // StationSounds/sound-effects 0..13

}  // namespace cfg
