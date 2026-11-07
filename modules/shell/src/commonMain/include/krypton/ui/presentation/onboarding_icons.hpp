#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace kr {

// Immutable, bundled artwork. The platform decodes it lazily; no host image ids
// are reserved and the host's own image always takes precedence.
struct OnboardingIcon {
  std::string_view name;
  std::wstring_view geometry;
  std::string_view svg, dark_svg;
  std::span<const std::uint8_t> png;
};

[[nodiscard]] const OnboardingIcon* OnboardingBrowserIcon(
    std::string_view name) noexcept;
[[nodiscard]] const OnboardingIcon* OnboardingSearchIcon(
    std::string_view keyword,
    std::string_view name) noexcept;

}  // namespace kr
