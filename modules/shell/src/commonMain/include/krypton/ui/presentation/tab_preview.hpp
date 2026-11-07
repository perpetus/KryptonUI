#pragma once

#include <chrono>
#include <string_view>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/layout/shell_layout.hpp"

namespace kr {
struct ThemeVm;

struct TabPreviewColors {
  Color background, border, thumbnail;
};

[[nodiscard]] TabPreviewColors ResolveTabPreviewColors(
    const ThemeVm& theme) noexcept;

enum class TabPreviewPlacement { kLeft, kRight, kBelow };

[[nodiscard]] constexpr TabPreviewPlacement TabPreviewPlacementFor(
    const ShellLayout& layout) noexcept {
  return layout.tabs != TabLayout::kVertical ? TabPreviewPlacement::kBelow
         : layout.sidebar_on_right           ? TabPreviewPlacement::kLeft
                                             : TabPreviewPlacement::kRight;
}

[[nodiscard]] Rect HorizontalTabPreviewViewport(
    Rect client,
    Rect chrome,
    const design::Tokens& tokens = design::kTokens) noexcept;
[[nodiscard]] Rect PlaceHorizontalTabPreview(
    Rect client,
    Rect chrome,
    Rect tab,
    float desired_height,
    bool right_to_left,
    const design::Tokens& tokens = design::kTokens) noexcept;

// Display-only authority with credentials removed; opaque URLs retain their
// original text. This performs no lookup or network work.
[[nodiscard]] std::string_view TabPreviewDomain(std::string_view url) noexcept;

struct TabPreviewMetrics {
  float width = 0;
  float thumbnail_height = 0;
  friend bool operator==(const TabPreviewMetrics&,
                         const TabPreviewMetrics&) = default;
};

// How long the pointer rests on a tab before its card opens: longer as tabs
// widen and their titles say more. `width` is the widest tab, or the sidebar's
// width; `minimum` and `standard` bound the scale.
[[nodiscard]] std::chrono::milliseconds TabPreviewShowDelay(
    float width,
    float minimum,
    float standard,
    const design::Tokens& tokens = design::kTokens) noexcept;

// The full-width thumbnail preserves its aspect ratio as the card narrows.
[[nodiscard]] TabPreviewMetrics MeasureTabPreview(
    float available_width,
    const design::Tokens& tokens = design::kTokens) noexcept;

[[nodiscard]] TabPreviewMetrics MeasureTabPreviewForLayout(
    Rect client,
    const ShellLayout& layout,
    const design::Tokens& tokens = design::kTokens) noexcept;

// Window-client coordinates. Never place the card over the sidebar; narrow
// the card to the remaining page-side space and clamp its height vertically.
[[nodiscard]] Rect PlaceTabPreview(
    Rect client,
    Rect sidebar,
    Rect tab,
    float desired_height,
    bool on_right,
    const design::Tokens& tokens = design::kTokens) noexcept;

}  // namespace kr
