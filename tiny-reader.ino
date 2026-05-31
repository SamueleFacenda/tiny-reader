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

struct PageData {
  String text;           // raw text (not pre-wrapped)
  uint32_t startPos = 0; // byte position in file where this text starts
  uint32_t endPos = 0;   // byte position in file after this text
  bool eof = false;      // true if we've reached end of file
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
static ButtonManager buttons;
static ScreenId screen = ScreenId::MenuLibrary;
static unsigned long lastActivity = 0;
static unsigned long lastWifiRefresh = 0;
static uint16_t wifiPartialCount = 0;
static uint8_t partialCount = 0;
static std::vector<BookInfo> libraryBooks;
static int libraryIndex = 0;
static int libraryScroll = 0;
RTC_DATA_ATTR static uint8_t sleepResumeMode = 0; // 0=unknown, 1=reader, 2=menu

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
    "%s screen=%s reader=%s books=%u idx=%d scroll=%d portal=%s uptime=%lu\n",
    tag,
    screenName(screen),
    reader.file ? "open" : "closed",
    static_cast<unsigned>(libraryBooks.size()),
    libraryIndex,
    libraryScroll,
    webPortalActive() ? "on" : "off",
    static_cast<unsigned long>(webPortalUptimeMs())
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

static PageData readPage(File& file) {
  // Read a raw chunk of bytes (Config::READ_BUFFER_SIZE) from the file and return as text.
  PageData page;
  page.startPos = file.position();

  const int READ_BUFFER = Config::READ_BUFFER_SIZE;

  // stack allocation is OK for moderate buffer sizes
  char buffer[READ_BUFFER];
  int bytesRead = file.read(reinterpret_cast<uint8_t*>(buffer), READ_BUFFER);

  if (bytesRead > 0) {
    page.text = String(reinterpret_cast<char*>(buffer), bytesRead);
  } else {
    page.text = "";
  }

  page.endPos = file.position();
  page.eof = !file.available();
  return page;
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
  uint32_t pos = resetPos ? 0 : storageLoadProgress(reader.path);
  if (pos >= reader.size) {
    pos = 0;
  }
  reader.pagePos = pos;
  reader.nextPagePos = pos;
  reader.file.seek(reader.pagePos);
  reader.history.clear();
  storageSetCurrentBook(reader.path);
  partialCount = 0;
  Serial.printf("Opened book: %s (%u bytes)\n", reader.path.c_str(), static_cast<unsigned>(reader.size));
}

static void renderCurrentPage(bool allowPartial) {
  if (!reader.file) {
    return;
  }
  const UiLayout& layout = uiReaderLayout();
  reader.file.seek(reader.pagePos);
  PageData page = readPage(reader.file);
  storageSaveProgress(reader.path, reader.pagePos);

  ReaderView view;
  view.title = titleFromPath(reader.path);
  view.text = page.text;
  view.bytesConsumed = 0;
  view.progressPercent = (reader.size > 0)
                           ? static_cast<uint8_t>(min<uint32_t>(100, (reader.pagePos * 100UL) / reader.size))
                           : 0;

  bool usePartial = allowPartial && (partialCount < Config::PARTIAL_REFRESH_LIMIT);
  uiDrawReader(display, view, usePartial);
  
  // Set nextPagePos based on how many bytes were actually rendered
  // This ensures continuous scrolling with no text loss
  reader.nextPagePos = reader.pagePos + view.bytesConsumed;
  
  // Don't go past EOF
  if (reader.nextPagePos >= reader.size) {
    reader.nextPagePos = reader.size;
  }
  
  if (usePartial) {
    partialCount++;
  } else {
    partialCount = 0;
  }
  Serial.printf("Rendered page at %u next=%u consumed=%u\n",
                static_cast<unsigned>(reader.pagePos),
                static_cast<unsigned>(reader.nextPagePos),
                static_cast<unsigned>(view.bytesConsumed));
}

static void renderNextPage() {
  if (!reader.file) {
    return;
  }
  reader.history.push_back(reader.pagePos);
  if (reader.history.size() > 50) {
    reader.history.erase(reader.history.begin());
  }
  reader.pagePos = reader.nextPagePos;
  renderCurrentPage(true);
}

static void renderPrevPage() {
  if (reader.history.empty()) {
    return;
  }
  reader.pagePos = reader.history.back();
  reader.history.pop_back();
  reader.nextPagePos = reader.pagePos;
  renderCurrentPage(true);
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
    storageSaveProgress(reader.path, reader.pagePos);
  }
  display.hibernate();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 0);
  delay(50);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);

  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 1200) {
    delay(10);
  }
  Serial.println("TinyReader boot");

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

  switch (screen) {
    case ScreenId::Reader:
      if (buttons.consumeShortPress(ButtonId::Next)) {
        Serial.println("BTN Next");
        renderNextPage();
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Prev)) {
        Serial.println("BTN Prev");
        renderPrevPage();
        action = true;
      }
      break;
    case ScreenId::MenuLibrary:
      // Menu-focused: Next/Prev move the active menu, OK enters ChooseBook
      if (buttons.consumeShortPress(ButtonId::Next)) {
        Serial.println("BTN Next");
        showScreen(nextMenu(screen));
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Prev)) {
        Serial.println("BTN Prev");
        showScreen(previousMenu(screen));
        action = true;
      }
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
      if (buttons.consumeShortPress(ButtonId::Next)) {
        Serial.println("BTN Next");
        if (!libraryBooks.empty()) {
          libraryIndex = min(libraryIndex + 1, static_cast<int>(libraryBooks.size()) - 1);
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
      if (buttons.consumeShortPress(ButtonId::Prev)) {
        Serial.println("BTN Prev");
        if (!libraryBooks.empty()) {
          libraryIndex = max(libraryIndex - 1, 0);
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
      if (buttons.consumeShortPress(ButtonId::Next)) {
        Serial.println("BTN Next");
        showScreen(nextMenu(screen));
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Prev)) {
        Serial.println("BTN Prev");
        showScreen(previousMenu(screen));
        action = true;
      }
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
      if (buttons.consumeShortPress(ButtonId::Next)) {
        Serial.println("BTN Next");
        showScreen(nextMenu(screen));
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Prev)) {
        Serial.println("BTN Prev");
        showScreen(previousMenu(screen));
        action = true;
      }
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
      if (buttons.consumeShortPress(ButtonId::Next)) {
        Serial.println("BTN Next");
        showScreen(nextMenu(screen));
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Prev)) {
        Serial.println("BTN Prev");
        showScreen(previousMenu(screen));
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
}
