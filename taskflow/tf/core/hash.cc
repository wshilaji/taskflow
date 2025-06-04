#include "core/hash.h"

namespace ecm {

// 0xff is in case char is signed.
static inline uint32_t ByteAs32(char c) {
  return static_cast<uint32_t>(c) & 0xff;
}
static inline uint64_t ByteAs64(char c) {
  return static_cast<uint64_t>(c) & 0xff;
}

uint32_t Hash32(const char *data, size_t n, uint32_t seed) {
  // 'm' and 'r' are mixing constants generated offline.
  // They're not really 'magic', they just happen to work well.

  const uint32_t m = 0x5bd1e995;
  const int r = 24;

  // Initialize the hash to a 'random' value
  uint32_t h = seed ^ n;

  // Mix 4 bytes at a time into the hash
  while (n >= 4) {
    uint32_t k = *reinterpret_cast<const uint32_t *>(data);

    k *= m;
    k ^= k >> r;
    k *= m;

    h *= m;
    h ^= k;

    data += 4;
    n -= 4;
  }

  // Handle the last few bytes of the input array

  switch (n) {
  case 3:
    h ^= ByteAs32(data[2]) << 16;
  case 2:
    h ^= ByteAs32(data[1]) << 8;
  case 1:
    h ^= ByteAs32(data[0]);
    h *= m;
  }

  // Do a few final mixes of the hash to ensure the last few
  // bytes are well-incorporated.

  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;

  return h;
}

uint64_t Hash64(const char *data, size_t n, uint64_t seed) {
  const uint64_t m = 0xc6a4a7935bd1e995;
  const int r = 47;

  uint64_t h = seed ^ (n * m);

  while (n >= 8) {
    uint64_t k = *reinterpret_cast<const int64_t *>(data);
    data += 8;
    n -= 8;

    k *= m;
    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;
  }

  switch (n) {
  case 7:
    h ^= ByteAs64(data[6]) << 48;
  case 6:
    h ^= ByteAs64(data[5]) << 40;
  case 5:
    h ^= ByteAs64(data[4]) << 32;
  case 4:
    h ^= ByteAs64(data[3]) << 24;
  case 3:
    h ^= ByteAs64(data[2]) << 16;
  case 2:
    h ^= ByteAs64(data[1]) << 8;
  case 1:
    h ^= ByteAs64(data[0]);
    h *= m;
  }

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

} // namespace ecm