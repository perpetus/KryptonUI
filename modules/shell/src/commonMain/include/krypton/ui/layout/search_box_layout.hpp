#pragma once

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/design/tokens.hpp"

namespace kr {

// The reference combines a preferred width with a CSS minimum expressed in
// viewport width and rem. The native root font supplies that relative unit.
[[nodiscard]] Rect ComputeSearchBoxBounds(
    Rect client,
    float measured_height,
    float root_font_size,
    const design::Tokens& tokens = design::kTokens) noexcept;

// Attach the editor to the measured URL field and leave results room below it.
[[nodiscard]] Rect ComputeAttachedSearchBoxBounds(
    Rect client,
    Rect address,
    float measured_height,
    const design::Tokens& tokens = design::kTokens) noexcept;

// Reserve the measured input and list margin before limiting the scrollable
// results viewport. The input can grow independently with system text scaling.
[[nodiscard]] float ComputeSearchResultsMaxHeight(
    Rect client,
    float measured_input_height,
    const design::Tokens& tokens = design::kTokens) noexcept;

}  // namespace kr
