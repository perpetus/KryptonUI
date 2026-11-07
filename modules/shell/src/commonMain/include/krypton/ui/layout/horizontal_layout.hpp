#pragma once

#include "krypton/ui/layout/shell_layout.hpp"

namespace kr {
// Widths of horizontal tabs, excluding their margins. Tabs share the strip
// equally up to the standard width. When every tab no longer fits at the active
// tab's minimum, the active tab keeps that minimum and the others shrink to
// theirs; past that the strip clips overflow.
struct HorizontalTabWidths {
  float active = 0;
  float inactive = 0;
};
[[nodiscard]] HorizontalTabWidths HorizontalTabWidthsFor(
    float available,
    std::size_t count,
    bool has_active,
    const design::Tokens& tokens = design::kTokens) noexcept;

[[nodiscard]] float HorizontalTabsExtent(
    HorizontalTabWidths widths,
    std::size_t count,
    bool has_active,
    const design::Tokens& tokens = design::kTokens) noexcept;
[[nodiscard]] float HorizontalNewTabOffset(float viewport_width,
                                           float tabs_extent,
                                           bool right_to_left) noexcept;
[[nodiscard]] float HorizontalTabHeight(
    float row_height,
    const design::Tokens& tokens = design::kTokens) noexcept;

// What a tab row shows. A centred row shows only its icon, in the middle.
struct TabRowContent {
  bool icon = false;
  bool audio = false;
  bool close = false;
  bool centered = false;
  friend bool operator==(const TabRowContent&, const TabRowContent&) = default;
};
// What fits in a horizontal tab `width` wide. The active tab keeps its close
// button, then fits the audio button and the site icon. An inactive tab fits
// the audio button and site icon first, shows a close button only when wide
// enough, and when nothing fits shows its site icon centred.
[[nodiscard]] TabRowContent HorizontalTabContent(
    float width,
    bool active,
    bool audio,
    const design::Tokens& tokens = design::kTokens) noexcept;
[[nodiscard]] TabLayout TabLayoutOf(Flags<LayoutFlag> flags) noexcept;
[[nodiscard]] Flags<LayoutFlag> WithTabLayout(Flags<LayoutFlag> flags,
                                              TabLayout layout) noexcept;
[[nodiscard]] ShellLayout ComputeHorizontalLayout(
    const WindowState& window,
    const LayoutPrefs& prefs,
    bool revealed,
    const design::Tokens& tokens,
    const NativeTitleBarMetrics& titlebar,
    std::size_t tab_count);

// Native controls supply measured slots and minimum content extents. The shared
// layer owns the reference's width-reduction and compact allocation rules.
[[nodiscard]] Rect CenteredAddressBounds(
    Rect slot,
    float toolbar_width,
    float minimum_width,
    const design::Tokens& tokens = design::kTokens) noexcept;
[[nodiscard]] float CompactAddressWidth(
    float available,
    float minimum_address,
    float minimum_tabs,
    const design::Tokens& tokens = design::kTokens) noexcept;
}  // namespace kr
