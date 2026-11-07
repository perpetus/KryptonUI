#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "krypton/ui/core/flags.hpp"

namespace kr {
enum class AddressFlag : std::uint32_t {
  kOpen = 1u << 0,
  kFocused = 1u << 1,
  kNewTab = 1u << 2,
};
template <>
inline constexpr bool kIsFlagEnum<AddressFlag> = true;

struct AddressSelection {
  std::uint32_t start = 0;
  std::uint32_t end = 0;
  bool composing = false;
  friend bool operator==(const AddressSelection&,
                         const AddressSelection&) = default;
};

// Validate UTF-8 and native UTF-16 selection bounds, including surrogate pairs.
[[nodiscard]] std::optional<std::uint32_t> AddressTextLength(
    std::string_view text,
    std::optional<AddressSelection> selection = {}) noexcept;
}  // namespace kr
