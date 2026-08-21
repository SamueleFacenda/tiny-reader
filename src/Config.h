#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <type_traits>

#include "GxEPD2_213_JD79661.h"

namespace Config {
  // Set to true to hold the device the other way round: the screen is rotated 180
  // degrees and the button pairs a flipped grip swaps are exchanged.
  constexpr bool LEFT_HANDED = false;

  // Elecrow ships two display modules under one 2.13" part number: false is the SSD1680Z
  // panel, true the JD79661/EK79029 one. Pins, buttons and the visible area are identical,
  // the command set and BUSY polarity are not, so the wrong setting here shows a blank
  // screen and "Busy Timeout!" on serial.
  constexpr bool PANEL_JD79661 = true;

  // The JD79661 wants a 100ms reset pulse and has no software reset to fall back on;
  // GxEPD2's default of 10 is enough for the SSD1680.
  constexpr uint16_t EPD_RESET_DURATION_MS = PANEL_JD79661 ? 100 : 10;

  // EPD wiring (matches factory spi.h)
  constexpr int PIN_EPD_CS = 14;
  constexpr int PIN_EPD_DC = 13;
  constexpr int PIN_EPD_RST = 10;
  constexpr int PIN_EPD_BUSY = 9;
  constexpr int PIN_EPD_POWER = 7;

  // Physical button pins (align with factory mappings)
  constexpr int PIN_BTN_HOME = 2;
  constexpr int PIN_BTN_EXIT = 1;
  constexpr int PIN_BTN_PREV = 6;
  constexpr int PIN_BTN_NEXT = 4;
  constexpr int PIN_BTN_OK = 5;

  // Logical button mapping: what each UI action reads. Ok and Home are deliberately
  // crossed relative to the factory labels, so the central key is Ok.
  constexpr int PIN_OK = LEFT_HANDED ? PIN_BTN_EXIT : PIN_BTN_HOME;
  constexpr int PIN_EXIT = LEFT_HANDED ? PIN_BTN_HOME : PIN_BTN_EXIT;
  constexpr int PIN_PREV = LEFT_HANDED ? PIN_BTN_NEXT : PIN_BTN_PREV;
  constexpr int PIN_NEXT = LEFT_HANDED ? PIN_BTN_PREV : PIN_BTN_NEXT;
  constexpr int PIN_HOME = PIN_BTN_OK;
  // Deep sleep wake key. A physical pin, so the grip does not change it.
  constexpr int PIN_WAKE = PIN_BTN_OK;

  constexpr bool BUTTON_PULLUP = false;
  constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

  // Display and rendering
  constexpr uint8_t DISPLAY_ROTATION = LEFT_HANDED ? 1 : 3; // landscape
  constexpr uint8_t UI_TEXT_SIZE = 1;
  constexpr uint8_t READER_TEXT_SIZE = 1;
  constexpr int16_t UI_MIN_MARGIN = 4;
  constexpr uint8_t PARTIAL_REFRESH_LIMIT = 10;
  // Portal screen paints between full refreshes. It paints only on a change, so this
  // is a count of paints, not of seconds.
  constexpr uint16_t WIFI_SETTINGS_FULL_REFRESH_EVERY = 6;

  // Timing
  constexpr uint32_t SERVER_TIMEOUT_MS = 15UL * 60UL * 1000UL;
  constexpr uint32_t INACTIVITY_SLEEP_MS = 1UL * 60UL * 1000UL;
  constexpr uint32_t LONG_PRESS_MS = 900UL;
  constexpr uint32_t FS_FORMAT_HOLD_MS = 2500UL;

  // Below this there is nothing to gain from entering light sleep.
  constexpr uint64_t LIGHT_SLEEP_MIN_US = 20000ULL;

  // Keep this much filesystem space free for progress files and metadata
  constexpr uint32_t FS_RESERVE_BYTES = 64UL * 1024UL;

  // WiFi access point
  constexpr const char* WIFI_SSID = "TinyReader";
  constexpr const char* WIFI_PASS = "12345678";
  // Pinned rather than left to the default, so a failed join is reproducible.
  constexpr uint8_t WIFI_AP_CHANNEL = 1;
  constexpr uint8_t WIFI_AP_MAX_CONN = 2;

  // Storage paths
  constexpr const char* BOOKS_DIR = "/books";
  constexpr const char* PROGRESS_DIR = "/progress";
  constexpr const char* CURRENT_BOOK_FILE = "/current.txt";

  // Reader buffer size (bytes) used for raw reads per page
  constexpr int READ_BUFFER_SIZE = 512;

  // How many page positions to keep for backwards navigation
  constexpr size_t READER_HISTORY_MAX = 64;

  // Battery (set pin to valid ADC input to enable)
  constexpr int BATTERY_ADC_PIN = -1;
  constexpr float BATTERY_ADC_REF = 3.3f;
  constexpr int BATTERY_ADC_MAX = 4095;
  constexpr float BATTERY_DIVIDER = 2.0f;
  constexpr float BATTERY_MIN_V = 3.2f;
  constexpr float BATTERY_MAX_V = 4.2f;
}

// A panel class is a type, so this selector cannot be a ternary like the ones above.
// Both classes compile; the linker drops the unused one.
using EpdDriver = typename std::conditional<Config::PANEL_JD79661,
                                            GxEPD2_213_JD79661,
                                            GxEPD2_213_GDEY0213B74>::type;
using EpdDisplay = GxEPD2_BW<EpdDriver, EpdDriver::HEIGHT>;
