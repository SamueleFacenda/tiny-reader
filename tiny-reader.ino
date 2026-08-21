#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

#include "src/Config.h"
#include "src/Input.h"
#include "src/Storage.h"
#include "src/Ui.h"
#include "src/WebPortal.h"

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
// What the portal screen currently shows, so it can repaint on change instead of
// on a timer. An e-ink paint costs 600-800ms of loop time and loads the panel
// charge pump while the radio is transmitting, so a once-per-second uptime tick
// was the most expensive thing on the screen.
static uint32_t wifiShownMinutes = UINT32_MAX;
static size_t wifiShownClients = SIZE_MAX;
static uint16_t wifiPartialsSinceFull = 0;
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
    "%s screen=%s reader=%s pos=%u books=%u idx=%d scroll=%d portal=%s uptime=%lu heap=%u min=%u maxalloc=%u\n",
    tag,
    screenName(screen),
    reader.file ? "open" : "closed",
    static_cast<unsigned>(reader.pagePos),
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

// Keeps the selection inside the library and the scroll window around it.
static void clampLibrarySelection() {
  if (libraryBooks.empty()) {
    libraryIndex = 0;
    libraryScroll = 0;
    return;
  }

  const int lastIndex = static_cast<int>(libraryBooks.size()) - 1;
  libraryIndex = constrain(libraryIndex, 0, lastIndex);

  const int visible = uiLayout().maxLines;
  if (libraryIndex < libraryScroll) {
    libraryScroll = libraryIndex;
  } else if (libraryIndex >= libraryScroll + visible) {
    libraryScroll = libraryIndex - visible + 1;
  }
}

static void refreshLibrary() {
  libraryBooks = storageListBooks();

  const String currentBook = storageGetCurrentBook();
  for (size_t i = 0; i < libraryBooks.size() && currentBook.length() > 0; ++i) {
    if (libraryBooks[i].path == currentBook) {
      libraryIndex = static_cast<int>(i);
      break;
    }
  }
  clampLibrarySelection();
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
  storageSavePosition(reader.path, reader.pagePos, reader.history);
}

static void renderCurrentPage(bool allowPartial) {
  if (!reader.file) {
    return;
  }
  size_t available = readPageAt(reader.pagePos);
  saveReaderPosition();

  ReaderView view;
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
  if (target == ScreenId::Reader && !reader.file) {
    Serial.println("showScreen: no book open, falling back to MenuLibrary");
    target = ScreenId::MenuLibrary;
  }
  Serial.printf("showScreen: %s -> %s\n", screenName(screen), screenName(target));
  screen = target;

  switch (screen) {
    case ScreenId::Reader:
      renderCurrentPage(false);
      break;
    case ScreenId::MenuLibrary:
      refreshLibrary();
      uiDrawLibrary(display, libraryBooks, -1, libraryScroll);   // no selection: menu has focus
      break;
    case ScreenId::ChooseBook:
      refreshLibrary();
      uiDrawLibrary(display, libraryBooks, libraryIndex, libraryScroll);
      break;
    case ScreenId::MenuWifi:
      uiDrawWifiOff(display);
      break;
    case ScreenId::MenuInfo: {
      const float volts = readBatteryVoltage();
      uiDrawInfo(display, storageGetStats(), volts, batteryPercentFromVoltage(volts));
      break;
    }
    case ScreenId::WifiSettings:
      wifiPartialsSinceFull = 0;
      wifiShownMinutes = webPortalUptimeMs() / 60000;
      wifiShownClients = webPortalClientCount();
      uiDrawWifiSettings(display, webPortalActive(), webPortalIp(), Config::WIFI_SSID, Config::WIFI_PASS,
                         webPortalUptimeMs(), webPortalClientCount(), false);
      break;
    case ScreenId::Error:
      break;
  }

  logState("drawn");
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

// Blocks on the error screen until the filesystem mounts or the user formats it.
static void ensureStorageReady() {
  if (storageBegin(false)) {
    return;
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
        return;
      }
      uiDrawError(display, "LittleFS error", "Mount failed", "Press Exit to retry");
    }
    delay(20);
  }
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
// state survive, so the loop simply carries on. ButtonManager owns the wake
// arming because it collides with the press interrupt.
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

  buttons.prepareForLightSleep();
  esp_sleep_enable_timer_wakeup(us);

  Serial.flush();   // the UART would otherwise garble mid-line
  esp_light_sleep_start();

  buttons.resumeAfterLightSleep();
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
  // Clear whatever light sleep armed, then wake from ext0 alone. Disabling the
  // sources one by one logs an error for any that is already inactive.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
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

  ensureStorageReady();
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

// Net movement of a latched direction pair, so a burst of presses costs one
// repaint instead of one per press.
static int consumeDirection() {
  const int forward = buttons.consumePresses(ButtonId::Next);
  const int backward = buttons.consumePresses(ButtonId::Prev);
  if (forward != 0 || backward != 0) {
    Serial.printf("BTN next=%d prev=%d\n", forward, backward);
  }
  return forward - backward;
}

static void serviceWebPortal() {
  if (!webPortalActive()) {
    return;
  }
  webPortalHandle();

  // Repaint only when something on the screen actually changed. Anything else
  // keeps the panel refreshing for the whole SERVER_TIMEOUT_MS window, which
  // starves handleClient() and loads the rail while clients are associating.
  if (screen == ScreenId::WifiSettings) {
    const uint32_t minutes = webPortalUptimeMs() / 60000;
    const size_t clients = webPortalClientCount();
    if (minutes != wifiShownMinutes || clients != wifiShownClients) {
      wifiShownMinutes = minutes;
      wifiShownClients = clients;
      const bool partial = (wifiPartialsSinceFull < Config::WIFI_SETTINGS_FULL_REFRESH_EVERY);
      uiDrawWifiSettings(display, true, webPortalIp(), Config::WIFI_SSID, Config::WIFI_PASS,
                         webPortalUptimeMs(), clients, partial);
      wifiPartialsSinceFull = partial ? (wifiPartialsSinceFull + 1) : 0;
    }
  }

  if (webPortalUptimeMs() > Config::SERVER_TIMEOUT_MS) {
    webPortalStop();
    if (screen == ScreenId::WifiSettings) {
      showScreen(ScreenId::MenuWifi);
    }
  }
}

// Exit leaves the current screen: back to the book if one is open, otherwise to
// somewhere useful.
static bool handleExit() {
  if (!buttons.consumeShortPress(ButtonId::Exit)) {
    return false;
  }
  Serial.println("BTN Exit");

  switch (screen) {
    case ScreenId::WifiSettings:
      webPortalStop();
      showScreen(ScreenId::MenuWifi);
      break;
    case ScreenId::MenuLibrary:
      showScreen(reader.file ? ScreenId::Reader : ScreenId::MenuWifi);
      break;
    case ScreenId::Error:
      break;
    default:
      showScreen(ScreenId::MenuLibrary);
      break;
  }
  return true;
}

// Next and Prev walk the menu cycle on every menu screen alike.
static bool handleMenuStep() {
  if (!isMenuScreen(screen)) {
    return false;
  }
  int steps = consumeDirection();
  if (steps == 0) {
    return false;
  }

  ScreenId target = screen;
  for (; steps > 0; --steps) {
    target = nextMenu(target);
  }
  for (; steps < 0; ++steps) {
    target = previousMenu(target);
  }
  showScreen(target);
  return true;
}

static bool handleReaderInput() {
  const int pages = consumeDirection();
  if (pages > 0) {
    advancePages(pages);
  } else if (pages < 0) {
    goBackPages(-pages);
  }

  bool acted = (pages != 0);
  if (buttons.consumeShortPress(ButtonId::Ok)) {
    Serial.println("BTN Ok");
    deepClean();
    acted = true;
  }
  return acted;
}

static bool handleChooseBookInput() {
  bool acted = false;

  const int steps = consumeDirection();
  if (steps != 0 && !libraryBooks.empty()) {
    libraryIndex += steps;
    clampLibrarySelection();
    uiDrawLibrary(display, libraryBooks, libraryIndex, libraryScroll);
    acted = true;
  }

  if (buttons.consumeShortPress(ButtonId::Ok)) {
    Serial.println("BTN Ok");
    if (libraryBooks.empty()) {
      showScreen(ScreenId::MenuLibrary);
    } else {
      openBook(libraryBooks[libraryIndex].path, false);
      showScreen(ScreenId::Reader);
    }
    acted = true;
  }
  return acted;
}

// Ok means something different on every menu: enter the library, start the
// portal, or just redraw what is on screen.
static bool handleMenuOk() {
  if (!buttons.consumeShortPress(ButtonId::Ok)) {
    return false;
  }
  Serial.println("BTN Ok");

  switch (screen) {
    case ScreenId::MenuLibrary:
      showScreen(libraryBooks.empty() ? ScreenId::MenuWifi : ScreenId::ChooseBook);
      break;
    case ScreenId::MenuWifi:
      if (!webPortalActive() && !webPortalStart(onUploadComplete)) {
        // The AP genuinely failed to come up: say so instead of showing an SSID
        // and password that nothing is listening on.
        Serial.println("Web portal failed to start");
        showScreen(ScreenId::MenuWifi);
        break;
      }
      showScreen(ScreenId::WifiSettings);
      break;
    default:
      showScreen(screen);   // MenuInfo and WifiSettings just refresh
      break;
  }
  return true;
}

static bool handleScreenInput() {
  switch (screen) {
    case ScreenId::Reader:
      return handleReaderInput();
    case ScreenId::ChooseBook:
      return handleChooseBookInput();
    case ScreenId::MenuLibrary:
    case ScreenId::MenuWifi:
    case ScreenId::MenuInfo:
    case ScreenId::WifiSettings:
      return handleMenuOk();
    case ScreenId::Error:
      return false;
  }
  return false;
}

void loop() {
  buttons.update();
  serviceWebPortal();

  bool acted = handleExit();
  acted |= handleMenuStep();
  acted |= handleScreenInput();

  if (acted) {
    updateActivity();
  }

  maybeDeepSleep();
  maybeLightSleep();
}
