// Pin map and constants for the Infinitag Config-Box.
// Hardware: ESP32-C3 Super Mini, see wissensbasis/18-config-tool.md §4.3.
// GPIO plan v3 (2026-07-09): status LED dropped, battery sense added.

#pragma once
#include <stdint.h>

namespace cfg {

// --- Firmware version (reported in DISCOVER_REPLY, shown in Tools menu) ----
// Bump on every flashed release – the version check in the device list
// compares these numbers.
constexpr uint8_t FW_MAJOR = 1;
constexpr uint8_t FW_MINOR = 2;
constexpr uint8_t FW_PATCH = 4;

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
// OLED I2C clock. A full 128x64 frame is ~1.1 KB = ~100 ms at 100 kHz,
// ~25 ms at 400 kHz (SSD1315 fast mode). Every render blocks the loop
// that feeds the web server / push sender - keep this high. If the
// display ever shows garbage, drop back to 100000.
constexpr uint32_t I2C_CLOCK_HZ            = 400000;

// --- Firmware update (Doc 18, OTA since 2026-07-12) --------------------------------
constexpr uint8_t  UPDATE_TIMEOUT_MIN     = 5;      // device SoftAP window
constexpr uint32_t UPDATE_ACK_TIMEOUT_MS  = 1500;   // UPDATE_BEGIN -> UPDATE_ACK
constexpr uint32_t SELF_UPDATE_TIMEOUT_MS = 10UL * 60UL * 1000UL;  // own AP window
constexpr uint8_t CAL_MINUTES = 10;  // Kalibriermodus: Auto-Aus der Station
// --- Battery supervision (4xAA alkaline, Doc 18 §4.2) -------------------------
// Field observation 2026-10-22: at 5.07-5.17 V idle the pack sags so far
// under radio TX bursts (discovery broadcast, SoftAP start) that the C3
// brownout-resets - the box "jumps back" to the main menu while screens
// without radio (Firmware-Info) keep working. Fresh cells read ~5.8 V.
// Readings <= VBAT_USB_V mean "running from USB" (divider floats) -> OK.
constexpr float VBAT_USB_V     = 3.0f;
constexpr float VBAT_WARN_V    = 5.3f;   // <= : blinking battery icon (title bar)
constexpr float VBAT_REPLACE_V = 5.2f;   // <= : "Batterie tauschen" on main screen
constexpr float VBAT_HYST_V    = 0.1f;   // readings scatter ~0.1 V -> no flicker
constexpr uint8_t VBAT_DEBOUNCE = 2;     // consecutive low samples before stepping down
constexpr uint32_t VBAT_POLL_MS  = 2000; // ADC sample interval (idle radio only)
constexpr uint32_t VBAT_BLINK_MS = 500;  // icon blink half-period
// Below this pack voltage no update is started (box must not die mid-flash):
// a multi-minute flash is the heaviest TX load, so the gate sits at the
// icon level - "if it blinks, no update".
constexpr float VBAT_MIN_FOR_UPDATE = VBAT_WARN_V;

// Sounds: IDs, Dateien und Anzeigenamen kommen zentral aus dem
// SoundCatalog.h der Core-Lib (inow::SOUND_COUNT / inow::soundName()).

}  // namespace cfg
