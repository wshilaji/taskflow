#pragma once

#include "butil/logging.h"

#define check_status(expr)                                                     \
  {                                                                            \
    auto s_ = expr;                                                            \
    if (!s_.ok()) {                                                            \
      LOG(FATAL) << ": " << s_;                                                \
    }                                                                          \
  }

#define check_true(expr)                                                       \
  if (!(expr)) {                                                               \
    LOG(FATAL) << "check failed";                                              \
  }
