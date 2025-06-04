#pragma once

#include "core/status.h"

namespace ecm {

Status LocalTimeToUnixMs(int64_t *result, absl::string_view time_fmt,
                         const std::string &time_str);
}