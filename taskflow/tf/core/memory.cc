#include "core/memory.h"
#include "absl/strings/str_split.h"
#include "core/file_system.h"
#include "core/inputbuffer.h"
#include "status.h"

namespace ecm {

Status GetMemoryInfo(MemoryInfo *info) {
  std::unique_ptr<RandomAccessFile> file;
  ECM_RETURN_IF_ERROR(
      FileSystem::Default()->NewRandomAccessFile("/proc/self/status", &file));

  std::string line;
  InputBuffer ib(file.get(), 1024);
  do {
    auto s = ib.ReadLine(&line);
    if (s.code() == StatusCode::kOutOfRange) {
      break;
    }
    ECM_RETURN_IF_ERROR(s);
    std::vector<absl::string_view> result =
        absl::StrSplit(line, absl::ByAnyChar(" \t"), absl::SkipEmpty());
    if (result.size() < 2) {
      continue;
    }
    if (absl::StartsWith(result[0], "VmRSS")) {
      ECM_RETURN_CHECK(absl::SimpleAtoi(result[1], &info->vm_rss));
    }
  } while (true);

  return OkStatus();
}

} // namespace ecm
