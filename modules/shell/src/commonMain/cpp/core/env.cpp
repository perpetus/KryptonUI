#include "krypton/ui/core/env.hpp"

#include <cstdlib>
#include <memory>

namespace kr {

std::optional<std::string> GetEnv(const char* name) {
#if defined(_MSC_VER)
  // MSVC flags std::getenv as unsafe; _dupenv_s is its sanctioned spelling.
  char* buffer = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&buffer, &size, name) != 0 || buffer == nullptr) {
    return std::nullopt;
  }
  std::unique_ptr<char, decltype(&std::free)> owned(buffer, &std::free);
  return std::string(owned.get());
#else
  const char* value = std::getenv(name);
  return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

}  // namespace kr
