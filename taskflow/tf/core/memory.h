#pragma once

#include "core/status.h"

namespace ecm {

struct MemoryInfo {
  size_t vm_rss = 0; // Resident Set Size KB
};

Status GetMemoryInfo(MemoryInfo *info);

} // namespace ecm
