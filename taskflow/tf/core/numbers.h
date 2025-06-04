#pragma once

#include "absl/strings/numbers.h"

namespace ecm {

template <class T> bool ParseNumber(absl::string_view s, T *v) {
  return absl::SimpleAtoi(s, v);
}

template <> bool ParseNumber<float>(absl::string_view s, float *v) {
  return absl::SimpleAtof(s, v);
}

template <> bool ParseNumber<double>(absl::string_view s, double *v) {
  return absl::SimpleAtod(s, v);
}

} // namespace ecm