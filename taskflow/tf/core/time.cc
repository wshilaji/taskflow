#include "absl/time/time.h"
#include "core/time.h"

namespace ecm {

Status LocalTimeToUnixMs(int64_t *result, absl::string_view time_fmt,
                         const std::string &time_str) {
  absl::Time time;
  std::string error;
  auto tz = absl::LocalTimeZone();
  if (!absl::ParseTime(time_fmt, time_str, tz, &time, &error)) {
    return ECM_ERROR(StatusCode::kFailedPrecondition,
                     "parse time fmt: {}, str: {}, failed: {}", time_fmt,
                     time_str, error);
  }
  *result = absl::ToUnixMillis(time);
  return OkStatus();
}

} // namespace ecm