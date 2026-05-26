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

bool storageBegin(bool autoFormat);
bool storageEnsureDirs();
std::vector<BookInfo> storageListBooks();
bool storageBookExists(const String& path);
String storageNormalizeBookPath(const String& path);
String storageSanitizeFilename(const String& name);
String storageGetCurrentBook();
void storageSetCurrentBook(const String& path);
uint32_t storageLoadProgress(const String& path);
void storageSaveProgress(const String& path, uint32_t pos);
StorageStats storageGetStats();
