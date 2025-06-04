#pragma once

#include "core/status.h"

namespace ecm {

// \brief Load a dynamic library.
//
// Pass "library_filename" to a platform-specific mechanism for dynamically
// loading a library.  The rules for determining the exact location of the
// library are platform-specific and are not documented here.
//
// On success, returns a handle to the library in "*handle" and returns
// OK from the function.
// Otherwise returns nullptr in "*handle" and an error status from the
// function.
Status LoadDynamicLibrary(const char *library_filename, void **handle);

// \brief Get a pointer to a symbol from a dynamic library.
//
// "handle" should be a pointer returned from a previous call to LoadLibrary.
// On success, store a pointer to the located symbol in "*symbol" and return
// OK from the function. Otherwise, returns nullptr in "*symbol" and an error
// status from the function.
Status GetSymbolFromLibrary(void *handle, const char *symbol_name,
                            void **symbol);

// \brief build the name of dynamic library.
//
// "name" should be name of the library.
// "version" should be the version of the library or NULL
// returns the name that LoadLibrary() can use
std::string FormatLibraryFileName(const std::string &name,
                                  const std::string &version);

} // namespace ecm