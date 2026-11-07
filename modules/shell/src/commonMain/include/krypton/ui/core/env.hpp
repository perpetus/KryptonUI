#pragma once

#include <optional>
#include <string>

namespace kr {

// Reads an environment variable. Only used for developer switches such as
// KR_UI_STATS; behaviour must never depend on the environment.
[[nodiscard]] std::optional<std::string> GetEnv(const char* name);

[[nodiscard]] inline bool EnvFlag(const char* name) {
  const auto value = GetEnv(name);
  return value && !value->empty() && *value != "0";
}

}  // namespace kr
