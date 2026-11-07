#pragma once

// Typed access to the design tokens generated from design/tokens.json.
// Never hard-code a colour, size or duration in UI code; add a token.

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/design/tokens.generated.hpp"

namespace kr::design {

[[nodiscard]] constexpr const Palette& PaletteFor(bool dark) {
  return dark ? kTokens.dark : kTokens.light;
}

[[nodiscard]] constexpr Color ToColor(std::uint32_t argb) {
  return Color{argb};
}

}  // namespace kr::design
