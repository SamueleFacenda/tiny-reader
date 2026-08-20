#include "Storage.h"

#include <ctype.h>

static const char* kRecordPrefix = "v1 ";

// Reads the next space separated decimal, returning false at end of record.
static bool nextUint(const String& record, int& idx, uint32_t& out) {
  const int len = static_cast<int>(record.length());
  while (idx < len && record[idx] == ' ') {
    idx++;
  }
  const int start = idx;
  while (idx < len && isdigit(static_cast<unsigned char>(record[idx]))) {
    idx++;
  }
  if (idx == start) {
    return false;
  }
  out = strtoul(record.substring(start, idx).c_str(), nullptr, 10);
  return true;
}

// Guards against a book that was replaced or truncated since it was last read.
static void dropInvalid(ReadingPosition& position, uint32_t bookSize) {
  if (bookSize == 0) {
    return;
  }
  if (position.pos >= bookSize) {
    position.pos = 0;
    position.history.clear();
    return;
  }
  size_t kept = 0;
  for (size_t i = 0; i < position.history.size(); ++i) {
    if (position.history[i] < bookSize) {
      position.history[kept++] = position.history[i];
    }
  }
  position.history.resize(kept);
}

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

ReadingPosition storageLoadPosition(const String& path, uint32_t bookSize) {
  ReadingPosition position;

  File file = LittleFS.open(makeProgressPath(path), "r");
  if (!file) {
    return position;
  }
  String record = file.readStringUntil('\n');
  file.close();
  record.trim();

  if (record.length() == 0) {
    return position;
  }

  if (!record.startsWith(kRecordPrefix)) {
    // Records written before history was persisted: a bare byte offset.
    position.pos = static_cast<uint32_t>(record.toInt());
    dropInvalid(position, bookSize);
    return position;
  }

  int idx = strlen(kRecordPrefix);
  uint32_t pos = 0;
  uint32_t count = 0;
  if (!nextUint(record, idx, pos) || !nextUint(record, idx, count)) {
    return position;
  }
  position.pos = pos;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t entry = 0;
    if (!nextUint(record, idx, entry)) {
      break;
    }
    position.history.push_back(entry);
  }
  dropInvalid(position, bookSize);
  return position;
}

bool storageSavePosition(const String& path, const ReadingPosition& position) {
  String finalPath = makeProgressPath(path);
  String tempPath = finalPath + ".tmp";

  File file = LittleFS.open(tempPath, "w");
  if (!file) {
    return false;
  }

  size_t total = position.history.size();
  size_t count = (total > Config::READER_HISTORY_MAX) ? Config::READER_HISTORY_MAX : total;
  file.print(kRecordPrefix);
  file.print(position.pos);
  file.print(' ');
  file.print(count);
  for (size_t i = total - count; i < total; ++i) {
    file.print(' ');
    file.print(position.history[i]);
  }
  file.print('\n');
  bool written = (file.getWriteError() == 0);
  file.close();

  if (!written) {
    LittleFS.remove(tempPath);
    Serial.printf("Failed to write %s\n", tempPath.c_str());
    return false;
  }

  // Rename over the live record instead of truncating it: littlefs swaps the
  // two atomically, so losing power here leaves either the previous record or
  // the new one, never an empty file that would reopen the book at page one.
  if (LittleFS.rename(tempPath, finalPath)) {
    return true;
  }
  LittleFS.remove(finalPath);
  return LittleFS.rename(tempPath, finalPath);
}

StorageStats storageGetStats() {
  StorageStats stats;
  stats.totalBytes = LittleFS.totalBytes();
  stats.usedBytes = LittleFS.usedBytes();
  return stats;
}
