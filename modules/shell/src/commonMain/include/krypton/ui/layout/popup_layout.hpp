#pragma once

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/design/tokens.hpp"

namespace kr {
struct PopupExtent {
  float width = 0, height = 0;
};

[[nodiscard]] Rect PopupViewport(
    Rect client,
    const design::Tokens& tokens = design::kTokens) noexcept;
// Defaults to the right edge; an explicitly right-hand sidebar uses the left.
[[nodiscard]] Rect PlaceNotification(
    Rect client,
    PopupExtent desired,
    bool sidebar_on_right,
    const design::Tokens& tokens = design::kTokens) noexcept;
[[nodiscard]] Rect PlaceAnchoredPopup(
    Rect client,
    Rect anchor,
    PopupExtent desired,
    bool right_aligned,
    const design::Tokens& tokens = design::kTokens) noexcept;

// Site tools start below the trigger with one panel gap toward the inline end.
[[nodiscard]] Rect PlaceSiteToolsPopup(
    Rect client,
    Rect anchor,
    PopupExtent desired,
    bool right_to_left,
    const design::Tokens& tokens = design::kTokens) noexcept;
}  // namespace kr
