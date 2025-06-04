#pragma once

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "fmt/format.h"

namespace ecm {

using StatusCode = absl::StatusCode;
using Status = absl::Status;

template <class T> using StatusOr = absl::StatusOr<T>;

template <class... Args>
std::string format(const std::string &fmt, Args &&...args) {
  try {
    return fmt::format(fmt, args...);
  } catch (std::exception &e) {
    return fmt::format("format `{}` failed", fmt);
  }
}

template <class... Args>
Status FmtStatus(StatusCode code, const std::string &fmt, Args &&...args) {
  return Status(code, format(fmt, args...));
}

inline Status OkStatus() { return Status(); }

// Maps UNIX errors into a Status.
Status IOError(const std::string &context, int err_number);

#define ECM_STR1(x) #x
#define ECM_STR2(x) ECM_STR1(x)
#define ECM_ERROR(code, fmt, ...)                                              \
  ecm::Status(code, ecm::format(__FILE__ ":" ECM_STR2(__LINE__) ": " fmt,      \
                                ##__VA_ARGS__))

#define ECM_RETURN_IF_ERROR(status)                                            \
  {                                                                            \
    auto __internal_status = status;                                           \
    if (!__internal_status.ok()) {                                             \
      return __internal_status;                                                \
    }                                                                          \
  }

#define ECM_RETURN_CHECK(expr)                                                 \
  if (!(expr)) {                                                               \
    return ECM_ERROR(ecm::StatusCode::kFailedPrecondition,                     \
                     "check `" ECM_STR2(expr) "` failed");                     \
  }

} // namespace ecm