#pragma once

#include <Arduino.h>
#include <GxEPD2_BW.h>

// Display driver selection
#define EPD_DRIVER_CLASS GxEPD2_213_GDEY0213B74
using EpdDisplay = GxEPD2_BW<EPD_DRIVER_CLASS, EPD_DRIVER_CLASS::HEIGHT>;

namespace Config {
  // EPD wiring (matches factory spi.h)
  constexpr int PIN_EPD_CS = 14;
  constexpr int PIN_EPD_DC = 13;
  constexpr int PIN_EPD_RST = 10;
  constexpr int PIN_EPD_BUSY = 9;
  constexpr int PIN_EPD_POWER = 7;

  // Button pins (align with factory mappings)
  constexpr int PIN_BTN_HOME = 2;
  constexpr int PIN_BTN_EXIT = 1;
  constexpr int PIN_BTN_PREV = 6;
  constexpr int PIN_BTN_NEXT = 4;
  constexpr int PIN_BTN_OK = 5;

  constexpr bool BUTTON_PULLUP = false;
  constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

  // Display and rendering
  constexpr uint8_t DISPLAY_ROTATION = 3; // landscape
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

  // Upload limits
  constexpr uint32_t MAX_UPLOAD_BYTES = 1500UL * 1024UL;

  // WiFi access point
  constexpr const char* WIFI_SSID = "TinyReader";
  constexpr const char* WIFI_PASS = "12345678";

  // Storage paths
  constexpr const char* BOOKS_DIR = "/books";
  constexpr const char* PROGRESS_DIR = "/progress";
  constexpr const char* CURRENT_BOOK_FILE = "/current.txt";

  // Reader buffer size (bytes) used for raw reads per page
  constexpr int READ_BUFFER_SIZE = 512;

  // Battery (set pin to valid ADC input to enable)
  constexpr int BATTERY_ADC_PIN = -1;
  constexpr float BATTERY_ADC_REF = 3.3f;
  constexpr int BATTERY_ADC_MAX = 4095;
  constexpr float BATTERY_DIVIDER = 2.0f;
  constexpr float BATTERY_MIN_V = 3.2f;
  constexpr float BATTERY_MAX_V = 4.2f;
}
