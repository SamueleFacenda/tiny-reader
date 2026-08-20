#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

#include "src/Config.h"
#include "src/Input.h"
#include "src/Storage.h"
#include "src/Ui.h"
#include "src/WebPortal.h"

#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>

EpdDisplay display(EPD_DRIVER_CLASS(Config::PIN_EPD_CS, Config::PIN_EPD_DC, Config::PIN_EPD_RST, Config::PIN_EPD_BUSY));

enum class ScreenId : uint8_t {
  Reader = 0,
  MenuLibrary = 1,
  ChooseBook = 2,
  MenuWifi = 3,
  MenuInfo = 4,
  WifiSettings = 5,
  Error = 6
};

struct ReaderState {
  File file;
  String path;
  size_t size = 0;
  uint32_t pagePos = 0;
  uint32_t nextPagePos = 0;
  std::vector<uint32_t> history;
};

static ReaderState reader;
// One page of raw bytes, reused for both drawing and paintless measurement.
static char pageBuffer[Config::READ_BUFFER_SIZE];
static ButtonManager buttons;
static ScreenId screen = ScreenId::MenuLibrary;
static unsigned long lastActivity = 0;
static unsigned long lastWifiRefresh = 0;
static uint16_t wifiPartialCount = 0;
static uint8_t partialsSinceFull = 0;
static std::vector<BookInfo> libraryBooks;
static int libraryIndex = 0;
static int libraryScroll = 0;
RTC_DATA_ATTR static uint8_t sleepResumeMode = 0; // 0=unknown, 1=reader, 2=menu

static const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:
      return "task-watchdog";
    case ESP_RST_WDT:
      return "other-watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    default:
      return "unknown";
  }
}

static void updateActivity() {
  lastActivity = millis();
}

static const char* screenName(ScreenId id) {
  switch (id) {
    case ScreenId::Reader:
      return "Reader";
    case ScreenId::MenuLibrary:
      return "MenuLibrary";
    case ScreenId::ChooseBook:
      return "ChooseBook";
    case ScreenId::MenuWifi:
      return "MenuWifi";
    case ScreenId::MenuInfo:
      return "MenuInfo";
    case ScreenId::WifiSettings:
      return "WifiSettings";
    case ScreenId::Error:
      return "Error";
    default:
      return "Unknown";
  }
}

static void logState(const char* tag) {
  Serial.printf(
    "%s screen=%s reader=%s books=%u idx=%d scroll=%d portal=%s uptime=%lu heap=%u min=%u maxalloc=%u\n",
    tag,
    screenName(screen),
    reader.file ? "open" : "closed",
    static_cast<unsigned>(libraryBooks.size()),
    libraryIndex,
    libraryScroll,
    webPortalActive() ? "on" : "off",
    static_cast<unsigned long>(webPortalUptimeMs()),
    static_cast<unsigned>(ESP.getFreeHeap()),
    static_cast<unsigned>(ESP.getMinFreeHeap()),
    static_cast<unsigned>(ESP.getMaxAllocHeap())
  );
}

static String titleFromPath(const String& path) {
  int slash = path.lastIndexOf('/');
  String name = (slash >= 0) ? path.substring(slash + 1) : path;
  if (name.length() == 0) {
    return "Book";
  }
  return name;
}

// Reads one page worth of raw bytes at pos into pageBuffer, returning the count.
static size_t readPageAt(uint32_t pos) {
  if (!reader.file) {
    return 0;
  }
  reader.file.seek(pos);
  int bytesRead = reader.file.read(reinterpret_cast<uint8_t*>(pageBuffer), sizeof(pageBuffer));
  return (bytesRead > 0) ? static_cast<size_t>(bytesRead) : 0;
}

// Where the page starting at pos would end, without drawing anything.
static uint32_t pageEndAt(uint32_t pos) {
  size_t available = readPageAt(pos);
  size_t consumed = uiMeasurePage(pageBuffer, available);
  uint32_t end = pos + static_cast<uint32_t>(consumed);
  return (end > reader.size) ? reader.size : end;
}

static void refreshLibrary() {
  libraryBooks = storageListBooks();
  if (libraryBooks.empty()) {
    libraryIndex = 0;
    libraryScroll = 0;
    return;
  }

  String currentBook = storageGetCurrentBook();
  if (currentBook.length() > 0) {
    for (size_t i = 0; i < libraryBooks.size(); ++i) {
      if (libraryBooks[i].path == currentBook) {
        libraryIndex = static_cast<int>(i);
        break;
      }
    }
  }

  if (libraryIndex >= static_cast<int>(libraryBooks.size())) {
    libraryIndex = static_cast<int>(libraryBooks.size()) - 1;
  }
  if (libraryIndex < 0) {
    libraryIndex = 0;
  }
  int maxVisible = uiLayout().maxLines;
  if (libraryIndex < libraryScroll) {
    libraryScroll = libraryIndex;
  }
  if (libraryIndex >= libraryScroll + maxVisible) {
    libraryScroll = libraryIndex - maxVisible + 1;
  }
}

static uint8_t menuIndexForScreen(ScreenId target) {
  switch (target) {
    case ScreenId::MenuLibrary:
      return 0;
    case ScreenId::MenuWifi:
    case ScreenId::WifiSettings:
      return 1;
    case ScreenId::MenuInfo:
      return 2;
    default:
      return 0;
  }
}

static ScreenId previousMenu(ScreenId target) {
  switch (target) {
    case ScreenId::MenuLibrary:
      return ScreenId::MenuInfo;
    case ScreenId::MenuWifi:
    case ScreenId::WifiSettings:
      return ScreenId::MenuLibrary;
    case ScreenId::MenuInfo:
      return ScreenId::MenuWifi;
    default:
      return ScreenId::MenuLibrary;
  }
}

static ScreenId nextMenu(ScreenId target) {
  switch (target) {
    case ScreenId::MenuLibrary:
      return ScreenId::MenuWifi;
    case ScreenId::MenuWifi:
    case ScreenId::WifiSettings:
      return ScreenId::MenuInfo;
    case ScreenId::MenuInfo:
      return ScreenId::MenuLibrary;
    default:
      return ScreenId::MenuLibrary;
  }
}

static bool isMenuScreen(ScreenId target) {
  return target == ScreenId::MenuLibrary || target == ScreenId::MenuWifi || target == ScreenId::MenuInfo || target == ScreenId::WifiSettings;
}

static void openBook(const String& path, bool resetPos) {
  if (reader.file) {
    reader.file.close();
  }
  reader.path = storageNormalizeBookPath(path);
  reader.file = LittleFS.open(reader.path, "r");
  if (!reader.file) {
    Serial.printf("Failed to open book: %s\n", reader.path.c_str());
    reader.size = 0;
    return;
  }

  reader.size = reader.file.size();
  ReadingPosition saved;
  if (!resetPos) {
    saved = storageLoadPosition(reader.path, reader.size);
  }
  reader.pagePos = saved.pos;
  reader.nextPagePos = saved.pos;
  reader.file.seek(reader.pagePos);
  reader.history = saved.history;
  storageSetCurrentBook(reader.path);
  partialsSinceFull = 0;
  Serial.printf("Opened book: %s (%u bytes)\n", reader.path.c_str(), static_cast<unsigned>(reader.size));
}

static void saveReaderPosition() {
  ReadingPosition position;
  position.pos = reader.pagePos;
  position.history = reader.history;
  storageSavePosition(reader.path, position);
}

static void renderCurrentPage(bool allowPartial) {
  if (!reader.file) {
    return;
  }
  size_t available = readPageAt(reader.pagePos);
  saveReaderPosition();

  ReaderView view;
  view.title = titleFromPath(reader.path);
  view.text = pageBuffer;
  view.textLen = available;
  view.bytesConsumed = 0;
  view.progressPercent = (reader.size > 0)
                           ? static_cast<uint8_t>(min<uint32_t>(100, (reader.pagePos * 100UL) / reader.size))
                           : 0;

  // Every PARTIAL_REFRESH_LIMIT turns one page is drawn fully instead of
  // partially, which clears the ghosting the partial updates leave behind. With
  // several queued presses collapsing into one paint, this counts paints rather
  // than presses, so fast scrolling costs fewer full refreshes than it used to.
  const bool usePartial = allowPartial && (partialsSinceFull < Config::PARTIAL_REFRESH_LIMIT);
  uiDrawReader(display, view, usePartial);
  
  // Set nextPagePos based on how many bytes were actually rendered
  // This ensures continuous scrolling with no text loss
  reader.nextPagePos = reader.pagePos + view.bytesConsumed;
  
  // Don't go past EOF
  if (reader.nextPagePos >= reader.size) {
    reader.nextPagePos = reader.size;
  }
  
  if (usePartial) {
    partialsSinceFull++;
  } else {
    partialsSinceFull = 0;
  }
  Serial.printf("Rendered page at %u next=%u consumed=%u heap=%u maxalloc=%u\n",
                static_cast<unsigned>(reader.pagePos),
                static_cast<unsigned>(reader.nextPagePos),
                static_cast<unsigned>(view.bytesConsumed),
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

// Advances count pages but paints only the one we land on: the intermediate
// boundaries are measured without touching the panel, which is what makes
// holding the lever feel like scrolling instead of queueing refreshes.
static void advancePages(uint8_t count) {
  if (!reader.file || count == 0) {
    return;
  }
  uint8_t skipped = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (reader.nextPagePos >= reader.size || reader.nextPagePos == reader.pagePos) {
      break;   // end of book, or a page that consumed nothing
    }
    reader.history.push_back(reader.pagePos);
    if (reader.history.size() > Config::READER_HISTORY_MAX) {
      reader.history.erase(reader.history.begin());
    }
    reader.pagePos = reader.nextPagePos;
    skipped++;
    if (i + 1 < count) {
      reader.nextPagePos = pageEndAt(reader.pagePos);
    }
  }
  if (skipped == 0) {
    return;
  }
  if (skipped > 1) {
    Serial.printf("Skipped %u pages without painting\n", static_cast<unsigned>(skipped - 1));
  }
  renderCurrentPage(true);
}

static void goBackPages(uint8_t count) {
  if (reader.history.empty() || count == 0) {
    return;
  }
  while (count > 0 && !reader.history.empty()) {
    reader.pagePos = reader.history.back();
    reader.history.pop_back();
    count--;
  }
  reader.nextPagePos = reader.pagePos;
  renderCurrentPage(true);
}

// Sunlight makes accumulated ghosting obvious. A white flash plus a full
// re-render is the strongest cleanup the panel offers from firmware.
static void deepClean() {
  Serial.println("Deep clean refresh");
  display.clearScreen();
  partialsSinceFull = 0;
  renderCurrentPage(false);
}

static float readBatteryVoltage() {
  if (Config::BATTERY_ADC_PIN < 0) {
    return -1.0f;
  }
  uint16_t raw = analogRead(Config::BATTERY_ADC_PIN);
  float v = (static_cast<float>(raw) / Config::BATTERY_ADC_MAX) * Config::BATTERY_ADC_REF;
  return v * Config::BATTERY_DIVIDER;
}

static int batteryPercentFromVoltage(float v) {
  if (v < 0.0f) {
    return 0;
  }
  float clamped = min(max(v, Config::BATTERY_MIN_V), Config::BATTERY_MAX_V);
  float pct = (clamped - Config::BATTERY_MIN_V) / (Config::BATTERY_MAX_V - Config::BATTERY_MIN_V);
  return static_cast<int>(pct * 100.0f + 0.5f);
}

static void showScreen(ScreenId target) {
  Serial.printf("showScreen: %s -> %s\n", screenName(screen), screenName(target));
  if (target == ScreenId::Reader && !reader.file) {
    Serial.println("showScreen: no book open, falling back to MenuLibrary");
    target = ScreenId::MenuLibrary;
  }
  screen = target;
  logState("before-draw");

  switch (screen) {
    case ScreenId::Reader:
      Serial.printf("draw Reader at pos=%lu size=%u history=%u\n",
                    static_cast<unsigned long>(reader.pagePos),
                    static_cast<unsigned>(reader.size),
                    static_cast<unsigned>(reader.history.size()));
      renderCurrentPage(false);
      break;
    case ScreenId::MenuLibrary:
      refreshLibrary();
      Serial.printf("draw MenuLibrary (menu focus) books=%u idx=%d scroll=%d\n",
                    static_cast<unsigned>(libraryBooks.size()),
                    libraryIndex,
                    libraryScroll);
      // menu-focused: show library pane with no active selection
      uiDrawLibrary(display, libraryBooks, -1, libraryScroll);
      break;
    case ScreenId::ChooseBook:
      refreshLibrary();
      Serial.printf("draw ChooseBook books=%u selected=%d scroll=%d\n",
                    static_cast<unsigned>(libraryBooks.size()),
                    libraryIndex,
                    libraryScroll);
      if (libraryBooks.empty()) {
        libraryIndex = 0;
        libraryScroll = 0;
      } else if (libraryIndex >= static_cast<int>(libraryBooks.size())) {
        libraryIndex = static_cast<int>(libraryBooks.size()) - 1;
      }
      uiDrawLibrary(display, libraryBooks, libraryIndex, libraryScroll);
      break;
    case ScreenId::MenuWifi:
      Serial.println("draw MenuWifi");
      uiDrawWifiOff(display);
      break;
    case ScreenId::MenuInfo: {
      StorageStats stats = storageGetStats();
      float v = readBatteryVoltage();
      int pct = batteryPercentFromVoltage(v);
      Serial.printf("draw MenuInfo used=%u total=%u batt=%.2f pct=%d\n",
                    static_cast<unsigned>(stats.usedBytes),
                    static_cast<unsigned>(stats.totalBytes),
                    v,
                    pct);
      uiDrawInfo(display, stats, v, pct);
      break;
    }
    case ScreenId::WifiSettings:
      wifiPartialCount = 0;
      Serial.printf("draw WifiSettings active=%s ip=%s\n",
                    webPortalActive() ? "yes" : "no",
                    webPortalIp().c_str());
      uiDrawWifiSettings(display, webPortalActive(), webPortalIp(), Config::WIFI_SSID, Config::WIFI_PASS, webPortalUptimeMs(), false);
      break;
    case ScreenId::Error:
      Serial.println("draw Error");
      break;
  }

  logState("after-draw");
}

static void onUploadComplete(const String& path, bool success) {
  if (!success) {
    Serial.println("Upload failed");
    return;
  }
  Serial.printf("Upload complete: %s\n", path.c_str());
  storageEnsureDirs();
  refreshLibrary();
  updateActivity();
}

static void stopWifiPortal() {
  if (webPortalActive()) {
    webPortalStop();
  }
}

static bool ensureStorageReady() {
  if (storageBegin(false)) {
    return true;
  }
  screen = ScreenId::Error;
  uiDrawError(display, "LittleFS error", "Mount failed", "Hold OK to format");

  unsigned long okStart = 0;
  while (true) {
    buttons.update();
    if (buttons.isDown(ButtonId::Ok)) {
      if (okStart == 0) {
        okStart = millis();
      }
      if (millis() - okStart > Config::FS_FORMAT_HOLD_MS) {
        Serial.println("Formatting LittleFS...");
        LittleFS.format();
        ESP.restart();
      }
    } else {
      okStart = 0;
    }

    if (buttons.consumeShortPress(ButtonId::Exit)) {
      if (storageBegin(true)) {
        return true;
      }
      uiDrawError(display, "LittleFS error", "Mount failed", "Press Exit to retry");
    }
    delay(20);
  }
}

// Steps through the menu cycle without repainting the screens in between.
static void stepMenu(int steps) {
  ScreenId target = screen;
  for (int i = 0; i < steps; ++i) {
    target = nextMenu(target);
  }
  for (int i = 0; i > steps; --i) {
    target = previousMenu(target);
  }
  showScreen(target);
}

// How long the loop may sleep before it has work to do again.
static uint64_t microsecondsUntilDeadline() {
  const unsigned long elapsed = millis() - lastActivity;
  if (elapsed >= Config::INACTIVITY_SLEEP_MS) {
    return 0;
  }
  return static_cast<uint64_t>(Config::INACTIVITY_SLEEP_MS - elapsed) * 1000ULL;
}

// Reading a page is idle time: rather than spinning at full clock until the
// deep sleep timeout, the CPU sleeps and a button wakes it. RAM and execution
// state survive, so the loop simply carries on. Light sleep GPIO wake is level
// triggered, hence the low level and the check that nothing is held down.
static void maybeLightSleep() {
  if (webPortalActive() || screen == ScreenId::Error) {
    return;
  }
  if (buttons.anyPending() || buttons.anyDown()) {
    return;
  }
  const uint64_t us = microsecondsUntilDeadline();
  if (us < Config::LIGHT_SLEEP_MIN_US) {
    return;
  }

  const gpio_num_t wakePins[] = {
    static_cast<gpio_num_t>(Config::PIN_OK),
    static_cast<gpio_num_t>(Config::PIN_EXIT),
    static_cast<gpio_num_t>(Config::PIN_PREV),
    static_cast<gpio_num_t>(Config::PIN_NEXT),
    static_cast<gpio_num_t>(Config::PIN_HOME)
  };
  for (gpio_num_t pin : wakePins) {
    gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(us);

  Serial.flush();   // the UART would otherwise garble mid-line
  esp_light_sleep_start();

  // gpio_wakeup_enable() rewrites the pin interrupt type to the level it wakes
  // on, which would leave the press handler re-firing for as long as a button
  // is held. Put the falling edge back and drop the wake registration.
  for (gpio_num_t pin : wakePins) {
    gpio_wakeup_disable(pin);
    gpio_set_intr_type(pin, GPIO_INTR_NEGEDGE);
  }
}

static void maybeDeepSleep() {
  if (screen == ScreenId::Error) {
    return;
  }
  if (webPortalActive()) {
    return;
  }
  if (millis() - lastActivity < Config::INACTIVITY_SLEEP_MS) {
    return;
  }
  Serial.println("Entering deep sleep");
  sleepResumeMode = (screen == ScreenId::Reader) ? 1 : 2;
  if (screen == ScreenId::Reader && reader.file) {
    saveReaderPosition();
  }
  display.hibernate();
  // Light sleep armed these; deep sleep wakes from ext0 alone.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)Config::PIN_WAKE, 0);
  delay(50);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);

  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 1200) {
    delay(10);
  }
  Serial.printf("TinyReader boot, last reset: %s\n", resetReasonName(esp_reset_reason()));

  pinMode(Config::PIN_EPD_POWER, OUTPUT);
  digitalWrite(Config::PIN_EPD_POWER, HIGH);

  uiInit(display);
  buttons.begin();
  Serial.printf("Buttons pullup: %s\n", Config::BUTTON_PULLUP ? "on" : "off");

  if (Config::BATTERY_ADC_PIN >= 0) {
    analogReadResolution(12);
  }

  if (!ensureStorageReady()) {
    return;
  }
  storageEnsureDirs();

  StorageStats stats = storageGetStats();
  Serial.printf("Filesystem: %u of %u bytes used\n",
                static_cast<unsigned>(stats.usedBytes),
                static_cast<unsigned>(stats.totalBytes));

  String current = storageGetCurrentBook();
  bool wokeFromSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
  if (wokeFromSleep && current.length() > 0 && sleepResumeMode == 1) {
    openBook(current, false);
    showScreen(ScreenId::Reader);
  } else {
    showScreen(ScreenId::MenuLibrary);
  }

  sleepResumeMode = 0;

  lastActivity = millis();
}

void loop() {
  buttons.update();

  if (webPortalActive()) {
    webPortalHandle();
    if (screen == ScreenId::WifiSettings && millis() - lastWifiRefresh >= 1000) {
      lastWifiRefresh = millis();
      bool doFullRefresh = (wifiPartialCount >= Config::WIFI_SETTINGS_FULL_REFRESH_EVERY);
      uiDrawWifiSettings(display, true, webPortalIp(), Config::WIFI_SSID, Config::WIFI_PASS, webPortalUptimeMs(), !doFullRefresh);
      if (doFullRefresh) {
        wifiPartialCount = 0;
      } else {
        wifiPartialCount++;
      }
    }
    if (webPortalUptimeMs() > Config::SERVER_TIMEOUT_MS) {
      stopWifiPortal();
      if (screen == ScreenId::WifiSettings) {
        showScreen(ScreenId::MenuWifi);
      }
    }
  }

  bool action = false;

  if (buttons.consumeShortPress(ButtonId::Exit)) {
    Serial.println("BTN Exit");
    switch (screen) {
      case ScreenId::Reader:
        showScreen(ScreenId::MenuLibrary);
        break;
      case ScreenId::ChooseBook:
        showScreen(ScreenId::MenuLibrary);
        break;
      case ScreenId::WifiSettings:
        stopWifiPortal();
        showScreen(ScreenId::MenuWifi);
        break;
      case ScreenId::MenuWifi:
      case ScreenId::MenuInfo:
        showScreen(ScreenId::MenuLibrary);
        break;
      case ScreenId::MenuLibrary:
        if (libraryBooks.empty()) {
          Serial.println("BTN Exit: empty library -> MenuWifi");
          showScreen(ScreenId::MenuWifi);
        } else if (reader.file) {
          showScreen(ScreenId::Reader);
        } else {
          Serial.println("BTN Exit: library -> MenuWifi");
          showScreen(ScreenId::MenuWifi);
        }
        break;
      case ScreenId::Error:
        break;
    }
    action = true;
  }

  // Next/Prev step the menu cycle identically on every menu screen, and several
  // presses collapse into a single repaint (these screens refresh fully).
  if (isMenuScreen(screen)) {
    uint8_t forward = buttons.consumePresses(ButtonId::Next);
    if (forward > 0) {
      Serial.printf("BTN Next x%u\n", static_cast<unsigned>(forward));
      stepMenu(forward);
      action = true;
    }
    uint8_t backward = buttons.consumePresses(ButtonId::Prev);
    if (backward > 0) {
      Serial.printf("BTN Prev x%u\n", static_cast<unsigned>(backward));
      stepMenu(-static_cast<int>(backward));
      action = true;
    }
  }

  switch (screen) {
    case ScreenId::Reader: {
      uint8_t forward = buttons.consumePresses(ButtonId::Next);
      if (forward > 0) {
        Serial.printf("BTN Next x%u\n", static_cast<unsigned>(forward));
        advancePages(forward);
        action = true;
      }
      uint8_t backward = buttons.consumePresses(ButtonId::Prev);
      if (backward > 0) {
        Serial.printf("BTN Prev x%u\n", static_cast<unsigned>(backward));
        goBackPages(backward);
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Ok)) {
        Serial.println("BTN Ok");
        deepClean();
        action = true;
      }
      break;
    }
    case ScreenId::MenuLibrary:
      // Menu-focused: Next/Prev move the active menu, OK enters ChooseBook
      if (buttons.consumeShortPress(ButtonId::Ok)) {
        Serial.println("BTN Ok");
        refreshLibrary();
        if (!libraryBooks.empty()) {
          // enter book-choose mode
          if (libraryIndex >= static_cast<int>(libraryBooks.size())) {
            libraryIndex = static_cast<int>(libraryBooks.size()) - 1;
          }
          showScreen(ScreenId::ChooseBook);
        } else {
          // no books -> go to wifi
          showScreen(ScreenId::MenuWifi);
        }
        action = true;
      }
      break;
    case ScreenId::ChooseBook:
      {
      uint8_t forward = buttons.consumePresses(ButtonId::Next);
      if (forward > 0) {
        Serial.printf("BTN Next x%u\n", static_cast<unsigned>(forward));
        if (!libraryBooks.empty()) {
          libraryIndex = min(libraryIndex + forward, static_cast<int>(libraryBooks.size()) - 1);
          int maxVisible = uiLayout().maxLines;
          if (libraryIndex >= libraryScroll + maxVisible) {
            libraryScroll = libraryIndex - maxVisible + 1;
          }
          Serial.printf("ChooseBook move next -> idx=%d scroll=%d books=%u\n",
                        libraryIndex,
                        libraryScroll,
                        static_cast<unsigned>(libraryBooks.size()));
          uiDrawLibrary(display, libraryBooks, libraryIndex, libraryScroll);
        }
        action = true;
      }
      uint8_t backward = buttons.consumePresses(ButtonId::Prev);
      if (backward > 0) {
        Serial.printf("BTN Prev x%u\n", static_cast<unsigned>(backward));
        if (!libraryBooks.empty()) {
          libraryIndex = max(libraryIndex - backward, 0);
          if (libraryIndex < libraryScroll) {
            libraryScroll = libraryIndex;
          }
          Serial.printf("ChooseBook move prev -> idx=%d scroll=%d books=%u\n",
                        libraryIndex,
                        libraryScroll,
                        static_cast<unsigned>(libraryBooks.size()));
          uiDrawLibrary(display, libraryBooks, libraryIndex, libraryScroll);
        }
        action = true;
      }
      }
      if (buttons.consumeShortPress(ButtonId::Ok)) {
        Serial.println("BTN Ok");
        if (!libraryBooks.empty()) {
          Serial.printf("ChooseBook open book idx=%d path=%s\n", libraryIndex, libraryBooks[libraryIndex].path.c_str());
          openBook(libraryBooks[libraryIndex].path, false);
          showScreen(ScreenId::Reader);
        } else {
          Serial.println("ChooseBook: no books");
          showScreen(ScreenId::MenuLibrary);
        }
        action = true;
      }
      break;
    case ScreenId::MenuWifi:
      if (buttons.consumeShortPress(ButtonId::Ok)) {
        Serial.println("BTN Ok");
        if (!webPortalActive()) {
          Serial.println("Starting web portal");
          webPortalStart(onUploadComplete);
          lastWifiRefresh = 0;
        } else {
          Serial.println("Web portal already active");
        }
        showScreen(ScreenId::WifiSettings);
        action = true;
      }
      break;
    case ScreenId::MenuInfo:
      if (buttons.consumeShortPress(ButtonId::Ok)) {
        Serial.println("BTN Ok");
        Serial.println("Refreshing info screen");
        showScreen(ScreenId::MenuInfo);
        action = true;
      }
      break;
    case ScreenId::WifiSettings:
      if (buttons.consumeShortPress(ButtonId::Ok)) {
        Serial.println("BTN Ok");
        Serial.println("Refreshing wifi settings screen");
        showScreen(ScreenId::WifiSettings);
        action = true;
      }
      break;
    case ScreenId::Error:
      break;
  }

  if (action) {
    updateActivity();
  }

  maybeDeepSleep();
  maybeLightSleep();
}
