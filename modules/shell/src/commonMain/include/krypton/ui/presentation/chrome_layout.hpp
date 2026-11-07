#pragma once

#include <algorithm>
#include <cmath>
#include <span>

#include "krypton/ui/model/model.hpp"

namespace kr {
// Reserve a native button target for the input and one for site information.
// The remaining actions overflow into the host's unified site-data menu.
[[nodiscard]] inline std::size_t AddressActionCapacity(
    float field_width) noexcept {
  const auto& t = design::kTokens.spacing;
  const float extent = t.button_size + 2 * t.button_margin;
  if (!std::isfinite(field_width) || field_width <= 2 * t.field_padding ||
      extent <= 0)
    return 0;
  const auto slots =
      static_cast<std::size_t>((field_width - 2 * t.field_padding) / extent);
  return slots > 2 ? slots - 2 : 0;
}

inline void PlaceAddressActions(std::span<const ActionKind> kinds,
                                std::size_t capacity,
                                std::span<std::uint8_t> visible) noexcept {
  std::ranges::fill(visible, std::uint8_t{0});
  capacity =
      std::max(capacity, static_cast<std::size_t>(
                             std::ranges::count(kinds, ActionKind::kSiteTools) +
                             std::ranges::count(kinds, ActionKind::kCopyLink)));
  const auto priority = [](ActionKind kind) {
    if (kind == ActionKind::kSiteTools)
      return -1;
    if (kind == ActionKind::kCopyLink)
      return 0;
    return kind == ActionKind::kSiteSetting ? 0
           : kind == ActionKind::kPage      ? 1
                                            : 2;
  };
  std::size_t placed = 0;
  for (int rank = -1; rank != 3 && placed < capacity; ++rank)
    for (std::size_t i = 0;
         i < std::min(kinds.size(), visible.size()) && placed < capacity; ++i)
      if (priority(kinds[i]) == rank) {
        visible[i] = 1;
        ++placed;
      }
}
}  // namespace kr
