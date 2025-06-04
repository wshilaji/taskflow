#pragma once

#include <string>

namespace ecm {

uint32_t Hash32(const char *data, size_t n, uint32_t seed);
uint64_t Hash64(const char *data, size_t n, uint64_t seed);

inline uint64_t Hash64(const char *data, size_t n) {
  return Hash64(data, n, 0xDECAFCAFFE);
}

inline uint64_t Hash64(const std::string &str) {
  return Hash64(str.data(), str.size());
}

inline uint64_t Hash64Combine(uint64_t a, uint64_t b) {
  return a ^ (b + 0x9e3779b97f4a7800ULL + (a << 10) + (a >> 4));
}

} // namespace ecm