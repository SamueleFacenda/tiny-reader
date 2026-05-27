#include "Ui.h"
#include <Fonts/FreeSerif9pt7b.h>

static UiLayout layout;
static UiLayout readerLayout;
static const char* kMenuLabels[] = {"Library", "Wi-Fi", "Info"};
static const uint8_t kMenuCount = sizeof(kMenuLabels) / sizeof(kMenuLabels[0]);

static void computeLayout(EpdDisplay& display) {
  layout.width = display.width();
  layout.height = display.height();
  layout.margin = max<int16_t>(Config::UI_MIN_MARGIN, layout.width / 40);
  layout.sidebarW = max<int16_t>(38, layout.width / 4);
  layout.headerH = 0;
  layout.footerH = 0;
  layout.contentX = layout.sidebarW + layout.margin;
  layout.contentY = layout.margin;
  layout.contentW = layout.width - layout.sidebarW - layout.margin * 2;
  layout.contentH = layout.height - layout.margin * 2;
  layout.lineHeight = max<int16_t>(12, static_cast<int16_t>(8 * Config::UI_TEXT_SIZE + 4));
  int16_t charWidth = max<int16_t>(6, static_cast<int16_t>(6 * Config::UI_TEXT_SIZE));
  layout.charsPerLine = max<int16_t>(10, layout.contentW / charWidth);
  layout.maxLines = max<int16_t>(1, layout.contentH / layout.lineHeight);
}

static void computeReaderLayout(EpdDisplay& display) {
  readerLayout.width = display.width();
  readerLayout.height = display.height();
  readerLayout.margin = max<int16_t>(2, readerLayout.width / 80);
  readerLayout.headerH = 0;
  readerLayout.footerH = 2;
  readerLayout.contentX = readerLayout.margin;
  readerLayout.contentY = readerLayout.margin;
  readerLayout.contentW = readerLayout.width - readerLayout.margin * 2;
  readerLayout.contentH = readerLayout.height - readerLayout.footerH - readerLayout.margin * 2;
  // FreeSerif9pt7b needs more line spacing to avoid overlap
  readerLayout.lineHeight = max<int16_t>(16, static_cast<int16_t>(8 * Config::READER_TEXT_SIZE + 8));
  int16_t charWidth = max<int16_t>(6, static_cast<int16_t>(6 * Config::READER_TEXT_SIZE));
  readerLayout.charsPerLine = max<int16_t>(10, readerLayout.contentW / charWidth);
  readerLayout.maxLines = max<int16_t>(1, readerLayout.contentH / readerLayout.lineHeight);
}

static void drawSidebar(EpdDisplay& display, uint8_t activeIndex) {
  display.fillRect(0, 0, layout.sidebarW, layout.height, GxEPD_WHITE);
  int16_t segmentH = layout.height / kMenuCount;
  for (uint8_t i = 0; i < kMenuCount; ++i) {
    int16_t y = segmentH * i;
    int16_t h = (i == kMenuCount - 1) ? (layout.height - y) : segmentH;
    bool active = (i == activeIndex);
    int16_t inset = active ? 1 : 4;
    int16_t boxX = inset;
    int16_t boxY = y + inset;
    int16_t boxW = layout.sidebarW - inset * 2;
    int16_t boxH = h - inset * 2;
    display.fillRoundRect(boxX, boxY, boxW, boxH, 5, active ? GxEPD_BLACK : GxEPD_WHITE);
    display.drawRoundRect(boxX, boxY, boxW, boxH, 5, GxEPD_BLACK);
    if (active) {
      display.drawRoundRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, 4, GxEPD_BLACK);
    }
    display.setTextColor(active ? GxEPD_WHITE : GxEPD_BLACK);
    int16_t labelX = boxX + layout.margin;
    int16_t labelY = boxY + boxH / 2 + 4;
    display.setCursor(labelX, labelY);
    display.print(kMenuLabels[i]);
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

// Note: wrapping now handled directly in uiDrawReader for raw text buffers

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
  display.setTextSize(Config::READER_TEXT_SIZE);
  display.setFont(&FreeSerif9pt7b);
  display.setTextWrap(false);
  
  int16_t sampleX1 = 0;
  int16_t sampleY1 = 0;
  uint16_t sampleW = 0;
  uint16_t sampleH = 0;
  display.getTextBounds("jAg", 0, 0, &sampleX1, &sampleY1, &sampleW, &sampleH);
  int16_t lineBaselineY = r.contentY + 2 - sampleY1;
  
  if (partial) {
    display.setPartialWindow(r.contentX, r.contentY, r.contentW, r.height - r.contentY);
  } else {
    display.setFullWindow();
  }
  
  display.firstPage();
  size_t bytesRendered = 0;
  const size_t textLen = view.text.length(); // Cache length to save method calls
  
  do {
    if (!partial) {
      display.fillScreen(GxEPD_WHITE);
    } else {
      display.fillRect(r.contentX, r.contentY, r.contentW, r.height - r.contentY, GxEPD_WHITE);
    }

    int16_t visualLineIndex = 0;
    bytesRendered = 0;
    size_t charPos = 0;

    // Text Parsing and Rendering
    while (charPos < textLen && visualLineIndex < r.maxLines) {
      String lineText = "";
      int16_t lastLineX1 = 0; // Cached to avoid recalculating text bounds

      while (charPos < textLen) {
        // Skip spaces, tabs, and carriage returns
        while (charPos < textLen && (view.text[charPos] == ' ' || view.text[charPos] == '\t' || view.text[charPos] == '\r')) {
          charPos++;
        }

        if (charPos >= textLen) break;

        // If explicit newline, consume it, save progress, and move to next visual line
        if (view.text[charPos] == '\n') {
          charPos++;
          bytesRendered = charPos;
          break; 
        }

        // Find the end of the current word
        size_t wordEnd = charPos;
        while (wordEnd < textLen && view.text[wordEnd] != ' ' && view.text[wordEnd] != '\t' && view.text[wordEnd] != '\n' && view.text[wordEnd] != '\r') {
          wordEnd++;
        }

        // Build and measure a test line
        String word = view.text.substring(charPos, wordEnd);
        String candidateLine = lineText.isEmpty() ? word : lineText + " " + word;
        int16_t x1 = 0, y1 = 0;
        uint16_t w = 0, h = 0;
        display.getTextBounds(candidateLine.c_str(), 0, 0, &x1, &y1, &w, &h);

        // If the word doesn't fit (and it's not the only word on the line), leave it for the next line
        if (static_cast<int16_t>(w) > r.contentW && !lineText.isEmpty()) {
          break; 
        }

        // Accept the word into the current line
        lineText = candidateLine;
        lastLineX1 = x1;       // Save x1 offset for printing later
        charPos = wordEnd;     // Advance pointer past this word
        bytesRendered = charPos;
      }

      // Render the compiled line (if any text was collected)
      if (!lineText.isEmpty()) {
        display.setCursor(r.contentX - lastLineX1, lineBaselineY + static_cast<int16_t>(visualLineIndex) * r.lineHeight);
        display.print(lineText);
      }

      // Always advance the line index (handles both printed lines and explicit empty \n lines)
      visualLineIndex++;
    }

    // Render Progress Bar
    int16_t barY = r.height - 1;
    int16_t barW = map(view.progressPercent, 0, 100, 0, r.contentW);
    display.fillRect(r.contentX, barY, r.contentW, 1, GxEPD_WHITE);
    display.fillRect(r.contentX, barY, barW, 1, GxEPD_BLACK);

  } while (display.nextPage());

  // Report consumed bytes
  const_cast<ReaderView&>(view).bytesConsumed = bytesRendered;

  display.setTextSize(Config::UI_TEXT_SIZE);
  display.setFont(nullptr);
  display.setTextWrap(true);
  display.powerOff();
}

void uiDrawLibrary(EpdDisplay& display, const std::vector<BookInfo>& books, int selectedIndex, int scrollIndex) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawSidebar(display, 0);

    int16_t startY = layout.contentY + layout.lineHeight;
    int16_t maxVisible = layout.maxLines;
    int16_t maxChars = layout.charsPerLine;

    if (books.empty()) {
      display.setCursor(layout.contentX, startY);
      display.print("No books yet");
      display.setCursor(layout.contentX, startY + layout.lineHeight);
      display.print("Next/Prev: switch menu");
      display.setCursor(layout.contentX, startY + layout.lineHeight * 2);
      display.print("OK: Wi-Fi upload");
    } else {
      for (int i = 0; i < maxVisible; ++i) {
        int bookIndex = scrollIndex + i;
        if (bookIndex >= static_cast<int>(books.size())) {
          break;
        }
        int16_t lineY = startY + i * layout.lineHeight;
        bool active = (bookIndex == selectedIndex);
        // compute font height and center text vertically
        int16_t fontH = max<int16_t>(8 * Config::UI_TEXT_SIZE, 8);
        int16_t textTop = lineY + (layout.lineHeight - fontH) / 2;
        if (active) {
          // selection box should tightly wrap the text with small vertical padding
          int16_t boxY = textTop - 1;
          int16_t boxH = fontH + 2;
          display.fillRect(layout.contentX, boxY, layout.contentW, boxH, GxEPD_BLACK);
          display.setTextColor(GxEPD_WHITE);
        } else {
          display.setTextColor(GxEPD_BLACK);
        }
        display.setCursor(layout.contentX + 2, textTop);
        display.print(trimToWidth(books[bookIndex].name, maxChars));
      }
      display.setTextColor(GxEPD_BLACK);
    }
  } while (display.nextPage());
  display.powerOff();
}

void uiDrawWifiOff(EpdDisplay& display) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawSidebar(display, 1);

    int16_t y = layout.contentY + layout.lineHeight;
    display.setCursor(layout.contentX, y);
    display.print("Wi-Fi off");
    y += layout.lineHeight * 2;

    display.setCursor(layout.contentX, y);
    display.print("Press OK to start");
    y += layout.lineHeight;
    display.setCursor(layout.contentX, y);
    display.print("the upload portal");
  } while (display.nextPage());
  display.powerOff();
}

void uiDrawWifiSettings(EpdDisplay& display, bool active, const String& ip, const String& ssid, const String& password, uint32_t uptimeMs, bool partial) {
  const UiLayout& r = layout;
  if (partial) {
    display.setPartialWindow(r.contentX, r.contentY, r.contentW, r.contentH);
  } else {
    display.setFullWindow();
  }

  display.firstPage();
  do {
    if (!partial) {
      display.fillScreen(GxEPD_WHITE);
      drawSidebar(display, 1);
    } else {
      display.fillRect(r.contentX, r.contentY, r.contentW, r.contentH, GxEPD_WHITE);
    }

    int16_t y = r.contentY + r.lineHeight;
    display.setCursor(layout.contentX, y);
    display.print(active ? "Wi-Fi portal: ON" : "Wi-Fi portal: OFF");
    y += layout.lineHeight;

    if (active) {
      display.setCursor(layout.contentX, y);
      display.print("IP: ");
      display.print(ip);
      y += layout.lineHeight;

      display.setCursor(layout.contentX, y);
      display.print("SSID: ");
      display.print(ssid);
      y += layout.lineHeight;

      display.setCursor(layout.contentX, y);
      display.print("Pass: ");
      display.print(password);
      y += layout.lineHeight;

      display.setCursor(layout.contentX, y);
      display.print("Uptime: ");
      display.print(uptimeMs / 1000);
      display.print("s");
      y += layout.lineHeight;
    }

    display.setCursor(layout.contentX, y + layout.lineHeight);
    display.print("Exit: stop portal");
  } while (display.nextPage());
  display.powerOff();
}

void uiDrawInfo(EpdDisplay& display, const StorageStats& stats, float battV, int battPercent) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawSidebar(display, 2);

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

    y += layout.lineHeight * 2;
    display.setCursor(layout.contentX, y);
    display.print("OK: refresh stats");
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
