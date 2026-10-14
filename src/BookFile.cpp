#include "BookFile.h"

#include <LittleFS.h>
#include <string.h>

#include "TrzDecode.h"

BookFile::~BookFile() {
  close();
}

uint32_t BookFile::size() const {
  if (!_file) {
    return 0;
  }
  return _compressed ? _trz.uncompSize : (uint32_t)_file.size();
}

bool BookFile::open(const String& path) {
  close();
  _file = LittleFS.open(path, "r");
  if (!_file) {
    return false;
  }

  uint8_t raw[Trz::HEADER_SIZE];
  const int probed = _file.read(raw, sizeof(raw));
  if (probed == (int)sizeof(raw) && Trz::parseHeader(raw, _trz) && openCompressed()) {
    _compressed = true;
    Serial.printf("Book is TRZ: %u bytes in %u chunks of %u\n",
                  static_cast<unsigned>(_trz.uncompSize),
                  static_cast<unsigned>(_trz.chunkCount),
                  static_cast<unsigned>(_trz.chunkSize));
  } else {
    // The probe consumed the first bytes; the passthrough reads below
    // expect the raw cursor to still be wherever seek() left it.
    _file.seek(0);
    _compressed = false;
  }
  _pos = 0;
  return true;
}

bool BookFile::openCompressed() {
  if (_trz.chunkSize > Config::TRZ_MAX_CHUNK_BYTES) {
    Serial.printf("TRZ chunk size %u is not sane\n", static_cast<unsigned>(_trz.chunkSize));
    return false;
  }
  if ((size_t)_file.size() < Trz::dataStart(_trz.chunkCount)) {
    return false;
  }

  const size_t isz = Trz::indexSize(_trz.chunkCount);
  _index = static_cast<uint32_t*>(malloc(isz));
  if (!_index || _file.read(reinterpret_cast<uint8_t*>(_index), isz) != isz) {
    return false;
  }

  // Index sanity in one pass: increasing offsets, each chunk inside the file.
  // Also sizes the compressed-input buffer to the largest chunk exactly.
  const size_t base = Trz::dataStart(_trz.chunkCount);
  const size_t fileSize = _file.size();
  size_t maxCompressed = 0;
  for (uint32_t i = 0; i < _trz.chunkCount; ++i) {
    const size_t begin = base + _index[i];
    const size_t end = (i + 1 < _trz.chunkCount) ? base + _index[i + 1] : fileSize;
    if (begin >= end || end > fileSize || (i > 0 && _index[i] <= _index[i - 1])) {
      Serial.printf("TRZ index is inconsistent at chunk %u\n", static_cast<unsigned>(i));
      return false;
    }
    if (end - begin > maxCompressed) {
      maxCompressed = end - begin;
    }
  }

  // Decompression lives in PSRAM: the board has 8MB of it, and bzip2's
  // working set (blockSize100k * 100k Int32s) does not belong in internal
  // RAM. A null here means PSRAM is missing or fragmented.
  _inBuf = static_cast<uint8_t*>(ps_malloc(maxCompressed));
  if (!_inBuf) {
    Serial.println("TRZ needs PSRAM the build does not have");
    return false;
  }
  _inCap = maxCompressed;
  for (uint8_t i = 0; i < Config::TRZ_CACHE_SLOTS; ++i) {
    _slot[i].data = static_cast<uint8_t*>(ps_malloc(_trz.chunkSize));
    _slot[i].chunk = UINT32_MAX;
    _slot[i].age = 0;
    if (!_slot[i].data) {
      Serial.println("TRZ needs PSRAM the build does not have");
      return false;
    }
  }
  return true;
}

void BookFile::releaseCompressed() {
  free(_index);
  _index = nullptr;
  for (uint8_t i = 0; i < Config::TRZ_CACHE_SLOTS; ++i) {
    if (_slot[i].data) {
      heap_caps_free(_slot[i].data);
      _slot[i].data = nullptr;
      _slot[i].chunk = UINT32_MAX;
      _slot[i].age = 0;
    }
  }
  if (_inBuf) {
    heap_caps_free(_inBuf);
    _inBuf = nullptr;
    _inCap = 0;
  }
}

void BookFile::close() {
  releaseCompressed();
  _compressed = false;
  _pos = 0;
  _trz = {};
  if (_file) {
    _file.close();
  }
}

bool BookFile::seek(uint32_t pos) {
  _pos = (size() > pos) ? pos : size();
  return true;
}

uint32_t BookFile::compressedChunkLen(uint32_t chunk) const {
  const size_t base = Trz::dataStart(_trz.chunkCount);
  const size_t begin = base + _index[chunk];
  const size_t end = (chunk + 1 < _trz.chunkCount) ? base + _index[chunk + 1] : (size_t)_file.size();
  return static_cast<uint32_t>(end - begin);
}

uint8_t* BookFile::ensureChunk(uint32_t chunk) {
  uint8_t victim = 0;
  for (uint8_t i = 0; i < Config::TRZ_CACHE_SLOTS; ++i) {
    if (_slot[i].chunk == chunk) {
      _slot[i].age = ++_tick;
      return _slot[i].data;
    }
    if (_slot[i].age < _slot[victim].age) {
      victim = i;
    }
  }
  if (!decompressInto(chunk, _slot[victim].data)) {
    return nullptr;
  }
  _slot[victim].chunk = chunk;
  _slot[victim].age = ++_tick;
  return _slot[victim].data;
}

bool BookFile::decompressInto(uint32_t chunk, uint8_t* out) {
  const uint32_t compLen = compressedChunkLen(chunk);
  if (compLen > _inCap) {
    return false;
  }
  if (!_file.seek(Trz::dataStart(_trz.chunkCount) + _index[chunk]) ||
      _file.read(_inBuf, compLen) != compLen) {
    Serial.printf("TRZ chunk %u read failed\n", static_cast<unsigned>(chunk));
    return false;
  }
  const uint32_t want = Trz::chunkBytes(_trz.uncompSize, _trz.chunkSize, chunk);
  const unsigned long started = millis();
  const bool ok = trzBunzip2(_inBuf, compLen, out, want);
  Serial.printf("TRZ chunk %u: %u->%u in %lums psFree=%u %s\n",
                static_cast<unsigned>(chunk),
                static_cast<unsigned>(compLen),
                static_cast<unsigned>(want),
                static_cast<unsigned long>(millis() - started),
                static_cast<unsigned>(ESP.getFreePsram()),
                ok ? "ok" : "CORRUPT");
  return ok;
}

size_t BookFile::read(uint8_t* buf, size_t maxLen) {
  if (!_file) {
    return 0;
  }
  if (!_compressed) {
    if (_file.position() != _pos && !_file.seek(_pos)) {
      return 0;
    }
    const size_t got = _file.read(buf, maxLen);
    _pos += got;
    return got;
  }

  const uint32_t logical = size();
  const uint32_t room = (logical > _pos) ? logical - _pos : 0;
  const uint32_t want = (room < maxLen) ? room : static_cast<uint32_t>(maxLen);
  uint32_t copied = 0;
  while (copied < want) {
    const uint32_t at = _pos + copied;
    const uint32_t chunk = Trz::chunkFor(at, _trz.chunkSize);
    const uint32_t off = Trz::offsetInChunk(at, _trz.chunkSize);
    uint8_t* data = ensureChunk(chunk);
    if (!data) {
      break;   // a short read, but the corruption has already hit the log
    }
    const uint32_t inChunk = Trz::chunkBytes(_trz.uncompSize, _trz.chunkSize, chunk) - off;
    const uint32_t n = (inChunk < want - copied) ? inChunk : want - copied;
    memcpy(buf + copied, data + off, n);
    copied += n;
  }
  _pos += copied;
  return copied;
}
