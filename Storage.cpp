#include "Storage.h"

#include <ctype.h>

static String makeProgressPath(const String& bookPath) {
  String base = bookPath;
  int slash = base.lastIndexOf('/');
  if (slash >= 0) {
    base = base.substring(slash + 1);
  }
  String safe = storageSanitizeFilename(base);
  int dot = safe.lastIndexOf('.');
  if (dot > 0) {
    safe = safe.substring(0, dot);
  }
  return String(Config::PROGRESS_DIR) + "/" + safe + ".pos";
}

bool storageBegin(bool autoFormat) {
  if (!LittleFS.begin(autoFormat)) {
    Serial.println("LittleFS mount failed");
    return false;
  }
  return true;
}

bool storageEnsureDirs() {
  bool ok = true;
  if (!LittleFS.exists(Config::BOOKS_DIR)) {
    ok &= LittleFS.mkdir(Config::BOOKS_DIR);
  }
  if (!LittleFS.exists(Config::PROGRESS_DIR)) {
    ok &= LittleFS.mkdir(Config::PROGRESS_DIR);
  }
  return ok;
}

std::vector<BookInfo> storageListBooks() {
  std::vector<BookInfo> books;
  File root = LittleFS.open(Config::BOOKS_DIR);
  if (!root || !root.isDirectory()) {
    return books;
  }

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String path = String(file.name());
      if (!path.startsWith("/")) {
        path = String(Config::BOOKS_DIR) + "/" + path;
      }
      BookInfo info;
      info.path = path;
      int slash = path.lastIndexOf('/');
      info.name = (slash >= 0) ? path.substring(slash + 1) : path;
      info.size = file.size();
      books.push_back(info);
    }
    file = root.openNextFile();
  }
  return books;
}

bool storageBookExists(const String& path) {
  return LittleFS.exists(path);
}

String storageNormalizeBookPath(const String& path) {
  if (path.startsWith(Config::BOOKS_DIR)) {
    return path;
  }
  if (path.startsWith("/")) {
    return path;
  }
  return String(Config::BOOKS_DIR) + "/" + path;
}

String storageSanitizeFilename(const String& name) {
  String out;
  out.reserve(name.length() + 4);
  for (size_t i = 0; i < name.length(); ++i) {
    char c = name[i];
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
      out += c;
    } else {
      out += '_';
    }
  }
  if (!out.endsWith(".txt")) {
    out += ".txt";
  }
  if (out.length() == 0) {
    out = "book.txt";
  }
  return out;
}

String storageGetCurrentBook() {
  File file = LittleFS.open(Config::CURRENT_BOOK_FILE, "r");
  if (!file) {
    return String();
  }
  String path = file.readStringUntil('\n');
  file.close();
  path.trim();
  return path;
}

void storageSetCurrentBook(const String& path) {
  File file = LittleFS.open(Config::CURRENT_BOOK_FILE, "w");
  if (!file) {
    return;
  }
  file.print(path);
  file.close();
}

uint32_t storageLoadProgress(const String& path) {
  String progressPath = makeProgressPath(path);
  File file = LittleFS.open(progressPath, "r");
  if (!file) {
    return 0;
  }
  uint32_t pos = file.parseInt();
  file.close();
  return pos;
}

void storageSaveProgress(const String& path, uint32_t pos) {
  String progressPath = makeProgressPath(path);
  File file = LittleFS.open(progressPath, "w");
  if (!file) {
    return;
  }
  file.print(pos);
  file.close();
}

StorageStats storageGetStats() {
  StorageStats stats;
  stats.totalBytes = LittleFS.totalBytes();
  stats.usedBytes = LittleFS.usedBytes();
  return stats;
}
