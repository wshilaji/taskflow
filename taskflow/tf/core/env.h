#pragma once

#include "absl/strings/string_view.h"

namespace ecm {

// deploy env
inline absl::string_view GetEnv(const char *name) {
  const char *value = getenv(name);
  return value ? absl::string_view(value) : absl::string_view();
}
inline bool IsProd() { return GetEnv("DEPLOY_ENV") == "prod"; }
inline bool IsPre() { return GetEnv("DEPLOY_ENV") == "pre"; }

// stage env
constexpr const char *kStageExp = "exp";
constexpr const char *kStagePg = "pg";
constexpr const char *kStageReplay = "replay";
inline bool IsStage(const absl::string_view stage) {
  return GetEnv("ECOM_STAGE") == stage;
}

} // namespace ecm
