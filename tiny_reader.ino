#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

#include "Config.h"
#include "Input.h"
#include "Storage.h"
#include "Ui.h"
#include "WebPortal.h"

#include <esp_sleep.h>
#include <esp_system.h>

EpdDisplay display(EPD_DRIVER_CLASS(Config::PIN_EPD_CS, Config::PIN_EPD_DC, Config::PIN_EPD_RST, Config::PIN_EPD_BUSY));

enum class ScreenId : uint8_t {
  Reader = 0,
  Library = 1,
  Server = 2,
  Info = 3,
  Error = 4
};

struct PageData {
  std::vector<String> lines;
  uint32_t startPos = 0;
  uint32_t endPos = 0;
  bool eof = false;
};

struct ReaderState {
  File file;
  String path;
  size_t size = 0;
  uint32_t pagePos = 0;
  std::vector<uint32_t> history;
};

static ReaderState reader;
static ButtonManager buttons;
static ScreenId screen = ScreenId::Reader;
static unsigned long lastActivity = 0;
static uint8_t partialCount = 0;
static std::vector<BookInfo> libraryBooks;
static int libraryIndex = 0;
static int libraryScroll = 0;

static void updateActivity() {
  lastActivity = millis();
}

static String titleFromPath(const String& path) {
  int slash = path.lastIndexOf('/');
  String name = (slash >= 0) ? path.substring(slash + 1) : path;
  if (name.length() == 0) {
    return "Book";
  }
  return name;
}

static PageData readPage(File& file, int maxLines, int maxChars) {
  PageData page;
  page.lines.reserve(maxLines);
  page.startPos = file.position();

  String line;
  uint32_t lineStartPos = file.position();

  while (file.available() && static_cast<int>(page.lines.size()) < maxLines) {
    char c = static_cast<char>(file.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      page.lines.push_back(line);
      line = "";
      lineStartPos = file.position();
      continue;
    }

    line += c;
    if (line.length() >= static_cast<size_t>(maxChars)) {
      page.lines.push_back(line);
      line = "";
      lineStartPos = file.position();
    }
  }

  if (static_cast<int>(page.lines.size()) >= maxLines && line.length() > 0) {
    file.seek(lineStartPos);
  } else if (line.length() > 0 && static_cast<int>(page.lines.size()) < maxLines) {
    page.lines.push_back(line);
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
  PageData page = readPage(reader.file, layout.maxLines, layout.charsPerLine);
  reader.pagePos = page.endPos;
  storageSaveProgress(reader.path, reader.pagePos);

  ReaderView view;
  view.title = titleFromPath(reader.path);
  view.lines = page.lines;
  view.progressPercent = (reader.size > 0)
                           ? static_cast<uint8_t>(min<uint32_t>(100, (reader.pagePos * 100UL) / reader.size))
                           : 0;

  bool usePartial = allowPartial && (partialCount < Config::PARTIAL_REFRESH_LIMIT);
  uiDrawReader(display, view, usePartial);
  if (usePartial) {
    partialCount++;
  } else {
    partialCount = 0;
  }
  Serial.printf("Rendered page at %u\n", static_cast<unsigned>(reader.pagePos));
}

static void renderNextPage() {
  if (!reader.file) {
    return;
  }
  reader.history.push_back(reader.pagePos);
  if (reader.history.size() > 50) {
    reader.history.erase(reader.history.begin());
  }
  renderCurrentPage(true);
}

static void renderPrevPage() {
  if (reader.history.empty()) {
    return;
  }
  reader.pagePos = reader.history.back();
  reader.history.pop_back();
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
  if (target == ScreenId::Reader && !reader.file) {
    target = ScreenId::Library;
  }
  screen = target;

  switch (screen) {
    case ScreenId::Reader:
      renderCurrentPage(false);
      break;
    case ScreenId::Library:
      refreshLibrary();
      uiDrawLibrary(display, libraryBooks, libraryIndex, libraryScroll);
      break;
    case ScreenId::Server:
      uiDrawServer(display, webPortalActive(), webPortalIp(), webPortalUptimeMs());
      break;
    case ScreenId::Info: {
      StorageStats stats = storageGetStats();
      float v = readBatteryVoltage();
      int pct = batteryPercentFromVoltage(v);
      uiDrawInfo(display, stats, v, pct);
      break;
    }
    case ScreenId::Error:
      break;
  }
}

static void onUploadComplete(const String& path, bool success) {
  if (!success) {
    Serial.println("Upload failed");
    return;
  }
  Serial.printf("Upload complete: %s\n", path.c_str());
  storageEnsureDirs();
  openBook(path, true);
  showScreen(ScreenId::Reader);
  updateActivity();
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

    if (buttons.consumeShortPress(ButtonId::Home)) {
      if (storageBegin(true)) {
        return true;
      }
      uiDrawError(display, "LittleFS error", "Mount failed", "Hold OK to format");
    }
    delay(20);
  }
}

static void maybeDeepSleep() {
  if (webPortalActive()) {
    return;
  }
  if (millis() - lastActivity < Config::INACTIVITY_SLEEP_MS) {
    return;
  }
  Serial.println("Entering deep sleep");
  if (reader.file) {
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
  if (current.length() == 0) {
    libraryBooks = storageListBooks();
    if (!libraryBooks.empty()) {
      current = libraryBooks.front().path;
    }
  }

  if (current.length() > 0) {
    openBook(current, false);
    showScreen(ScreenId::Reader);
  } else {
    showScreen(ScreenId::Library);
  }

  lastActivity = millis();
}

void loop() {
  buttons.update();

  if (webPortalActive()) {
    webPortalHandle();
    if (webPortalUptimeMs() > Config::SERVER_TIMEOUT_MS) {
      webPortalStop();
      if (screen == ScreenId::Server) {
        uiDrawServer(display, webPortalActive(), webPortalIp(), webPortalUptimeMs());
      }
    }
  }

  bool action = false;

  if (buttons.consumeShortPress(ButtonId::Home)) {
    Serial.println("BTN Home");
    showScreen(ScreenId::Reader);
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
      if (buttons.consumeShortPress(ButtonId::Ok)) {
        Serial.println("BTN Ok");
        showScreen(ScreenId::Library);
        action = true;
      }
      if (buttons.consumeLongPress(ButtonId::Ok)) {
        Serial.println("BTN Ok (long)");
        showScreen(ScreenId::Server);
        action = true;
      }
      break;
    case ScreenId::Library:
      if (buttons.consumeShortPress(ButtonId::Next)) {
        Serial.println("BTN Next");
        if (!libraryBooks.empty()) {
          libraryIndex = min(libraryIndex + 1, static_cast<int>(libraryBooks.size()) - 1);
          int maxVisible = uiLayout().maxLines;
          if (libraryIndex >= libraryScroll + maxVisible) {
            libraryScroll = libraryIndex - maxVisible + 1;
          }
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
          uiDrawLibrary(display, libraryBooks, libraryIndex, libraryScroll);
        }
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Ok)) {
        Serial.println("BTN Ok");
        if (!libraryBooks.empty()) {
          openBook(libraryBooks[libraryIndex].path, false);
          showScreen(ScreenId::Reader);
        }
        action = true;
      }
      if (buttons.consumeLongPress(ButtonId::Ok)) {
        Serial.println("BTN Ok (long)");
        showScreen(ScreenId::Server);
        action = true;
      }
      break;
    case ScreenId::Server:
      if (buttons.consumeShortPress(ButtonId::Ok)) {
        Serial.println("BTN Ok");
        if (webPortalActive()) {
          webPortalStop();
        } else {
          webPortalStart(onUploadComplete);
        }
        uiDrawServer(display, webPortalActive(), webPortalIp(), webPortalUptimeMs());
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Next)) {
        Serial.println("BTN Next");
        showScreen(ScreenId::Info);
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Prev)) {
        Serial.println("BTN Prev");
        showScreen(ScreenId::Library);
        action = true;
      }
      break;
    case ScreenId::Info:
      if (buttons.consumeShortPress(ButtonId::Next)) {
        Serial.println("BTN Next");
        showScreen(ScreenId::Reader);
        action = true;
      }
      if (buttons.consumeShortPress(ButtonId::Prev)) {
        Serial.println("BTN Prev");
        showScreen(ScreenId::Server);
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
