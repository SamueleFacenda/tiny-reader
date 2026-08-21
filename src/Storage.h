#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

#include "Config.h"

struct BookInfo {
  String path;
  String name;
  size_t size = 0;
};

struct StorageStats {
  size_t totalBytes = 0;
  size_t usedBytes = 0;
};

// Where the reader is in a book, plus the trail of pages it came through, so Prev keeps
// working across deep sleep and reboots.
struct ReadingPosition {
  uint32_t pos = 0;
  std::vector<uint32_t> history;
};

bool storageBegin(bool autoFormat);
bool storageEnsureDirs();
std::vector<BookInfo> storageListBooks();
String storageNormalizeBookPath(const String& path);
String storageSanitizeFilename(const String& name);
String storageGetCurrentBook();
void storageSetCurrentBook(const String& path);
ReadingPosition storageLoadPosition(const String& path, uint32_t bookSize);
bool storageSavePosition(const String& path, uint32_t pos, const std::vector<uint32_t>& history);
StorageStats storageGetStats();
