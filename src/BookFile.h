#pragma once

#include <FS.h>

#include "Config.h"
#include "TrzFormat.h"

// File-like view over a stored book. Plain files pass through unchanged; a
// TRZ1 container presents its uncompressed byte stream, so pagination,
// progress records and history keep operating on plain text offsets either
// way — a .pos file saved on a plain book resumes its compressed twin.
// Nothing decompresses before the first read.
class BookFile {
public:
  ~BookFile();

  // Closes any book this wrapper already holds, so open is safe to call in
  // place. False means the path did not open or the container is unusable.
  bool open(const String& path);
  void close();

  // Logical size: uncompressed bytes for a TRZ book, file bytes otherwise.
  uint32_t size() const;

  bool seek(uint32_t pos);
  size_t read(uint8_t* buf, size_t maxLen);

  explicit operator bool() const { return static_cast<bool>(_file); }

private:
  struct CacheSlot {
    uint8_t* data;
    uint32_t chunk;
    uint8_t age;
  };

  bool openCompressed();
  void releaseCompressed();
  uint8_t* ensureChunk(uint32_t chunk);
  bool decompressInto(uint32_t chunk, uint8_t* out);
  uint32_t compressedChunkLen(uint32_t chunk) const;

  File _file;
  bool _compressed = false;
  uint32_t _pos = 0;
  Trz::Header _trz = {};
  uint32_t* _index = nullptr;               // compressed chunk offsets
  uint8_t* _inBuf = nullptr;                // one chunk's compressed bytes
  size_t _inCap = 0;
  CacheSlot _slot[Config::TRZ_CACHE_SLOTS] = {};
  uint8_t _tick = 0;
};
