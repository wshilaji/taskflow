#include <dlfcn.h>
#include <string>

#include "core/load_library.h"

namespace ecm {

Status LoadDynamicLibrary(const char *library_filename, void **handle) {
  *handle = dlopen(library_filename, RTLD_NOW | RTLD_LOCAL);
  if (!*handle) {
    // Note that in C++17 std::string_view(nullptr) gives segfault!
    const char *error_msg = dlerror();
    return absl::NotFoundError(error_msg ? error_msg : "(null error message)");
  }
  return OkStatus();
}

Status GetSymbolFromLibrary(void *handle, const char *symbol_name,
                            void **symbol) {
  // Check that the handle is not NULL to avoid dlsym's RTLD_DEFAULT behavior.
  if (!handle) {
    *symbol = nullptr;
  } else {
    *symbol = dlsym(handle, symbol_name);
  }
  if (!*symbol) {
    // Note that in C++17 std::string_view(nullptr) gives segfault!
    const char *error_msg = dlerror();
    return absl::NotFoundError(error_msg ? error_msg : "(null error message)");
  }
  return OkStatus();
}

std::string FormatLibraryFileName(const std::string &name,
                                  const std::string &version) {
  std::string filename;
#if defined(__APPLE__)
  if (version.size() == 0) {
    filename = "lib" + name + ".dylib";
  } else {
    filename = "lib" + name + "." + version + ".dylib";
  }
#else
  if (version.empty()) {
    filename = "lib" + name + ".so";
  } else {
    filename = "lib" + name + ".so" + "." + version;
  }
#endif
  return filename;
}

} // namespace ecm