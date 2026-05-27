#pragma once

#include <Arduino.h>
#include <vector>

#include "Config.h"
#include "Storage.h"

struct UiLayout {
  int16_t width = 0;
  int16_t height = 0;
  int16_t margin = 0;
  int16_t sidebarW = 0;
  int16_t headerH = 0;
  int16_t footerH = 0;
  int16_t contentX = 0;
  int16_t contentY = 0;
  int16_t contentW = 0;
  int16_t contentH = 0;
  int16_t lineHeight = 0;
  int16_t charsPerLine = 0;
  int16_t maxLines = 0;
};

struct ReaderView {
  String title;
  String text;               // raw unwrapped text to render
  uint8_t progressPercent = 0;
  size_t bytesConsumed = 0;  // set by uiDrawReader: how many bytes of text were actually rendered
};

void uiInit(EpdDisplay& display);
const UiLayout& uiLayout();
const UiLayout& uiReaderLayout();

void uiDrawReader(EpdDisplay& display, const ReaderView& view, bool partial);
void uiDrawLibrary(EpdDisplay& display, const std::vector<BookInfo>& books, int selectedIndex, int scrollIndex);
void uiDrawWifiOff(EpdDisplay& display);
void uiDrawWifiSettings(EpdDisplay& display, bool active, const String& ip, const String& ssid, const String& password, uint32_t uptimeMs, bool partial);
void uiDrawInfo(EpdDisplay& display, const StorageStats& stats, float battV, int battPercent);
void uiDrawError(EpdDisplay& display, const String& title, const String& message, const String& action);
