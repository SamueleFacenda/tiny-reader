#include "Ui.h"

static UiLayout layout;
static UiLayout readerLayout;
static const char* kTabs[] = {"Read", "Library", "Server", "Info"};
static const uint8_t kTabCount = sizeof(kTabs) / sizeof(kTabs[0]);

static void computeLayout(EpdDisplay& display) {
  layout.width = display.width();
  layout.height = display.height();
  layout.margin = max<int16_t>(Config::UI_MIN_MARGIN, layout.width / 40);
  layout.headerH = max<int16_t>(18, layout.height / 6);
  layout.footerH = max<int16_t>(12, layout.height / 10);
  layout.contentX = layout.margin;
  layout.contentY = layout.headerH + layout.margin;
  layout.contentW = layout.width - layout.margin * 2;
  layout.contentH = layout.height - layout.headerH - layout.footerH - layout.margin * 2;
  layout.lineHeight = max<int16_t>(12, static_cast<int16_t>(8 * Config::UI_TEXT_SIZE + 4));
  int16_t charWidth = max<int16_t>(6, static_cast<int16_t>(6 * Config::UI_TEXT_SIZE));
  layout.charsPerLine = max<int16_t>(10, layout.contentW / charWidth);
  layout.maxLines = max<int16_t>(1, layout.contentH / layout.lineHeight);
}

static void computeReaderLayout(EpdDisplay& display) {
  readerLayout.width = display.width();
  readerLayout.height = display.height();
  readerLayout.margin = max<int16_t>(Config::UI_MIN_MARGIN, readerLayout.width / 40);
  readerLayout.headerH = 0;
  readerLayout.footerH = max<int16_t>(12, readerLayout.height / 12);
  readerLayout.contentX = readerLayout.margin;
  readerLayout.contentY = readerLayout.margin;
  readerLayout.contentW = readerLayout.width - readerLayout.margin * 2;
  readerLayout.contentH = readerLayout.height - readerLayout.footerH - readerLayout.margin * 2;
  readerLayout.lineHeight = max<int16_t>(12, static_cast<int16_t>(8 * Config::UI_TEXT_SIZE + 4));
  int16_t charWidth = max<int16_t>(6, static_cast<int16_t>(6 * Config::UI_TEXT_SIZE));
  readerLayout.charsPerLine = max<int16_t>(10, readerLayout.contentW / charWidth);
  readerLayout.maxLines = max<int16_t>(1, readerLayout.contentH / readerLayout.lineHeight);
}

static void drawTabs(EpdDisplay& display, uint8_t activeIndex) {
  int16_t tabW = layout.width / kTabCount;
  for (uint8_t i = 0; i < kTabCount; ++i) {
    int16_t x = tabW * i;
    int16_t w = (i == kTabCount - 1) ? (layout.width - x) : tabW;
    bool active = (i == activeIndex);
    display.setTextColor(active ? GxEPD_WHITE : GxEPD_BLACK);
    display.fillRect(x, 0, w, layout.headerH, active ? GxEPD_BLACK : GxEPD_WHITE);
    display.drawRect(x, 0, w, layout.headerH, GxEPD_BLACK);
    int16_t labelX = x + layout.margin;
    int16_t labelY = layout.headerH - layout.margin;
    display.setCursor(labelX, labelY);
    display.print(kTabs[i]);
  }
  display.setTextColor(GxEPD_BLACK);
}

static String trimToWidth(const String& text, int16_t maxChars) {
  if (text.length() <= static_cast<size_t>(maxChars)) {
    return text;
  }
  if (maxChars < 3) {
    return text.substring(0, maxChars);
  }
  return text.substring(0, maxChars - 3) + "...";
}

void uiInit(EpdDisplay& display) {
  display.init(115200);
  display.setRotation(Config::DISPLAY_ROTATION);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(Config::UI_TEXT_SIZE);
  computeLayout(display);
  computeReaderLayout(display);
}

const UiLayout& uiLayout() {
  return layout;
}

const UiLayout& uiReaderLayout() {
  return readerLayout;
}

void uiDrawReader(EpdDisplay& display, const ReaderView& view, bool partial) {
  const UiLayout& r = readerLayout;
  if (partial) {
    display.setPartialWindow(r.contentX, r.contentY, r.contentW, r.contentH + r.footerH);
  } else {
    display.setFullWindow();
  }

  display.firstPage();
  do {
    if (!partial) {
      display.fillScreen(GxEPD_WHITE);
    } else {
      display.fillRect(r.contentX, r.contentY, r.contentW, r.contentH + r.footerH, GxEPD_WHITE);
    }

    int16_t textY = r.contentY + r.lineHeight;
    int16_t maxChars = r.charsPerLine;

    for (size_t i = 0; i < view.lines.size(); ++i) {
      display.setCursor(r.contentX, textY + static_cast<int16_t>(i) * r.lineHeight);
      display.print(trimToWidth(view.lines[i], maxChars));
    }

    String progress = String(view.progressPercent) + "%";
    int16_t footerY = r.height - r.margin;
    int16_t footerX = r.contentX;
    display.setCursor(footerX, footerY);
    display.print(progress);
  } while (display.nextPage());

  display.powerOff();
}

void uiDrawLibrary(EpdDisplay& display, const std::vector<BookInfo>& books, int selectedIndex, int scrollIndex) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawTabs(display, 1);

    int16_t startY = layout.contentY + layout.lineHeight;
    int16_t maxVisible = layout.maxLines;
    int16_t maxChars = layout.charsPerLine;

    if (books.empty()) {
      display.setCursor(layout.contentX, startY);
      display.print("No books yet");
    } else {
      for (int i = 0; i < maxVisible; ++i) {
        int bookIndex = scrollIndex + i;
        if (bookIndex >= static_cast<int>(books.size())) {
          break;
        }
        int16_t lineY = startY + i * layout.lineHeight;
        bool active = (bookIndex == selectedIndex);
        if (active) {
          display.fillRect(layout.contentX, lineY - layout.lineHeight + 2, layout.contentW, layout.lineHeight, GxEPD_BLACK);
          display.setTextColor(GxEPD_WHITE);
        } else {
          display.setTextColor(GxEPD_BLACK);
        }
        display.setCursor(layout.contentX + 2, lineY);
        display.print(trimToWidth(books[bookIndex].name, maxChars));
      }
      display.setTextColor(GxEPD_BLACK);
    }
  } while (display.nextPage());
  display.powerOff();
}

void uiDrawServer(EpdDisplay& display, bool active, const String& ip, uint32_t uptimeMs) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawTabs(display, 2);

    int16_t y = layout.contentY + layout.lineHeight;
    display.setCursor(layout.contentX, y);
    display.print(active ? "Server: ON" : "Server: OFF");
    y += layout.lineHeight;

    if (active) {
      display.setCursor(layout.contentX, y);
      display.print("IP: ");
      display.print(ip);
      y += layout.lineHeight;

      display.setCursor(layout.contentX, y);
      display.print("Uptime: ");
      display.print(uptimeMs / 1000);
      display.print("s");
      y += layout.lineHeight;
    }

    display.setCursor(layout.contentX, y + layout.lineHeight);
    display.print("OK: toggle server");
    y += layout.lineHeight * 2;
    display.setCursor(layout.contentX, y);
    display.print("Home: back to reader");
  } while (display.nextPage());
  display.powerOff();
}

void uiDrawInfo(EpdDisplay& display, const StorageStats& stats, float battV, int battPercent) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawTabs(display, 3);

    int16_t y = layout.contentY + layout.lineHeight;
    size_t usedKb = stats.usedBytes / 1024;
    size_t totalKb = stats.totalBytes / 1024;
    int usedPercent = (stats.totalBytes > 0) ? static_cast<int>((stats.usedBytes * 100UL) / stats.totalBytes) : 0;

    display.setCursor(layout.contentX, y);
    display.print("Storage: ");
    display.print(usedKb);
    display.print("/");
    display.print(totalKb);
    display.print(" KB (");
    display.print(usedPercent);
    display.print("%)");
    y += layout.lineHeight;

    display.setCursor(layout.contentX, y);
    if (battV < 0.0f) {
      display.print("Battery: n/a");
    } else {
      display.print("Battery: ");
      display.print(battV, 2);
      display.print("V (");
      display.print(battPercent);
      display.print("%)");
    }
  } while (display.nextPage());
  display.powerOff();
}

void uiDrawError(EpdDisplay& display, const String& title, const String& message, const String& action) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(layout.contentX, layout.contentY + layout.lineHeight);
    display.print(title);
    display.setCursor(layout.contentX, layout.contentY + layout.lineHeight * 2);
    display.print(message);
    display.setCursor(layout.contentX, layout.contentY + layout.lineHeight * 4);
    display.print(action);
  } while (display.nextPage());
  display.powerOff();
}
