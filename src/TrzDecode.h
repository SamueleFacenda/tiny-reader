#pragma once

// Decompresses one TRZ chunk: a single independent bzip2 stream expected to
// expand to exactly outLen bytes. The whole compressed chunk is already in
// memory (its length comes from the container index, so the buffer is sized
// exactly), which keeps this pure and host-testable: the device only swaps
// the allocator for PSRAM, everything else is the flake's libbz2 library.

#include <stddef.h>
#include <stdint.h>

#include <bzlib.h>

#ifdef ARDUINO
#include <Arduino.h>
// bzip2's decompression working set (blockSize100k * 100k Int32s) has no
// business in internal RAM; the board carries 8MB of PSRAM for it.
inline void* trzBzAlloc(void* opaque, int items, int size) {
  (void)opaque;
  return heap_caps_malloc((size_t)items * (size_t)size, MALLOC_CAP_SPIRAM);
}
inline void trzBzFree(void* opaque, void* ptr) {
  (void)opaque;
  heap_caps_free(ptr);
}
#else
#include <stdlib.h>
inline void* trzBzAlloc(void* opaque, int items, int size) {
  (void)opaque;
  return calloc((size_t)items, (size_t)size);
}
inline void trzBzFree(void* opaque, void* ptr) {
  (void)opaque;
  free(ptr);
}
#endif

inline bool trzBunzip2(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen) {
  bz_stream s = {};
  s.bzalloc = trzBzAlloc;
  s.bzfree = trzBzFree;
  if (BZ2_bzDecompressInit(&s, 0, 0) != BZ_OK) {
    return false;
  }
  s.next_in = const_cast<char*>(reinterpret_cast<const char*>(in));
  s.avail_in = (unsigned int)inLen;
  s.next_out = reinterpret_cast<char*>(out);
  s.avail_out = (unsigned int)outLen;

  bool ok = false;
  while (true) {
    const int result = BZ2_bzDecompress(&s);
    if (result == BZ_STREAM_END) {
      ok = (s.total_out_lo32 == (unsigned int)outLen);
      break;
    }
    // The stream carries the exact chunk; anything but progress here is a
    // corrupt or mismatched book, and bzip2 has already CRC-checked it.
    if (result != BZ_OK || s.avail_in == 0) {
      break;
    }
  }
  BZ2_bzDecompressEnd(&s);
  return ok;
}
