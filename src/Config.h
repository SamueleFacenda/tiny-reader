#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>

// Display driver selection
#define EPD_DRIVER_CLASS GxEPD2_213_GDEY0213B74
using EpdDisplay = GxEPD2_BW<EPD_DRIVER_CLASS, EPD_DRIVER_CLASS::HEIGHT>;

namespace Config {
  // Set to true to hold the device the other way round: the screen is rotated
  // 180 degrees and the button pairs a flipped grip swaps are exchanged.
  constexpr bool LEFT_HANDED = false;

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

  // Logical button mapping: what each UI action reads. Note that Ok and Home are
  // deliberately crossed relative to the factory labels, the central key is Ok.
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
  constexpr uint16_t WIFI_SETTINGS_FULL_REFRESH_EVERY = 30;

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
