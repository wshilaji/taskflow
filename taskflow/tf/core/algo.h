#pragma once

#include <algorithm>
#include <cstdint>

namespace ecm {

template <typename Container>
bool Contains(const Container &container,
              const typename Container::value_type &value) {
  // 通用线性查找
  return std::find(container.begin(), container.end(), value) !=
         container.end();
}

template <typename Container>
bool Exists(const Container &container,
            const typename Container::value_type &value) {
  return container.find(value) != container.end();
}

inline uint64_t FindPower2(uint64_t b) {
  b -= 1;
  b |= (b >> 1);
  b |= (b >> 2);
  b |= (b >> 4);
  b |= (b >> 8);
  b |= (b >> 16);
  b |= (b >> 32);
  return b + 1;
}

} // namespace ecm
