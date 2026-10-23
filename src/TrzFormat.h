#pragma once

// TRZ1 compressed-book container: the on-disk shape of an opt-in compressed
// upload. One independent, byte-aligned bzip2 stream per uncompressed chunk,
// addressed by a front index, so serving a page decompresses at most one
// chunk. Chunk i expands to a derivable byte count, so uncompressed offsets
// are never stored: pagination, progress records and history keep operating
// on plain text offsets and stay valid across plain/compressed re-uploads.
//
// Layout, all multi-byte fields little-endian:
//   0x00 "TRZ1"              magic
//   0x04 u8  container version
//   0x05 u8  bzip2 blockSize100k (informational; the stream header rules)
//   0x06 u16 reserved
//   0x08 u32 chunkSize       uncompressed bytes per chunk
//   0x0C u32 uncompSize      logical size of the book text
//   0x10 u32 chunkCount
//   0x14 u32 chunkOffset[]   compressed chunk starts, relative to dataStart
//
// dataStart() is where chunk data begins; chunk i spans
// [dataStart()+chunkOffset[i], dataStart()+chunkOffset[i+1]), the last one to
// the end of the container file.
//
// Free of Arduino and bzip2 includes on purpose: the pagination invariants are
// host-checkable with g++, like TextWrap.

#include <stddef.h>
#include <stdint.h>

namespace Trz {

constexpr uint8_t MAGIC[4] = {'T', 'R', 'Z', '1'};
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_SIZE = 0x14;
// bzip2's own blockSize100k ceiling; a stored field past it means garbage.
constexpr uint8_t MAX_BLOCK_SIZE_100K = 9;

struct Header {
  uint32_t chunkSize;
  uint32_t uncompSize;
  uint32_t chunkCount;
};

inline uint32_t readU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline uint32_t chunkCountFor(uint32_t uncompSize, uint32_t chunkSize) {
  return (uncompSize == 0) ? 0 : (uncompSize - 1) / chunkSize + 1;
}

inline size_t indexSize(uint32_t chunkCount) {
  return (size_t)chunkCount * 4;
}

inline size_t dataStart(uint32_t chunkCount) {
  return HEADER_SIZE + indexSize(chunkCount);
}

inline uint32_t chunkFor(uint32_t pos, uint32_t chunkSize) {
  return pos / chunkSize;
}

inline uint32_t offsetInChunk(uint32_t pos, uint32_t chunkSize) {
  return pos % chunkSize;
}

// Bytes chunk i must expand to: full chunks except the short last one.
inline uint32_t chunkBytes(uint32_t uncompSize, uint32_t chunkSize, uint32_t chunk) {
  const uint32_t begin = chunk * chunkSize;
  return (uncompSize - begin > chunkSize) ? chunkSize : uncompSize - begin;
}

// True when raw[0..HEADER_SIZE) is a well-formed fixed header whose derived
// fields agree with each other. Index entries and chunk data are validated by
// the reader, which has the file size.
inline bool parseHeader(const uint8_t* raw, Header& out) {
  if (raw[0] != MAGIC[0] || raw[1] != MAGIC[1] || raw[2] != MAGIC[2] || raw[3] != MAGIC[3]) {
    return false;
  }
  if (raw[4] != VERSION || raw[5] > MAX_BLOCK_SIZE_100K) {
    return false;
  }
  out.chunkSize = readU32(raw + 0x08);
  out.uncompSize = readU32(raw + 0x0C);
  out.chunkCount = readU32(raw + 0x10);
  if (out.chunkSize == 0) {
    return false;
  }
  return out.chunkCount == chunkCountFor(out.uncompSize, out.chunkSize);
}

} // namespace Trz
