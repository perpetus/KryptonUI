#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/model/model.hpp"

namespace kr {

// Shared window geometry. Native sidebar and toolbar regions are disjoint
// from the host's page, except while a compact bar is transiently revealed.
struct ShellLayout {
  // Only Sidebar's native Windows captions reveal above the page without
  // reserving a title row. A right sidebar already contains their native edge.
  Rect caption_reveal;
  bool caption_overlay = false;
  bool caption_visible = false;
  TabLayout tabs = TabLayout::kVertical;
  Rect navigation;
  Rect tab_strip;
  bool tab_strip_visible = false;
  bool centered_address = false;
  NativeTitleBarMetrics native_titlebar;
  // Native UI bounds. With autohide this can be a thin reveal strip.
  Rect sidebar;
  // Native island bounds include the floating surface's shadow. Hover bounds
  // also bridge the inset back to the window edge, so reveals stay reachable.
  Rect sidebar_island;
  Rect sidebar_hover;
  bool sidebar_visible = false;
  // Sidebar floats over the card (autohide reveal) instead of beside it.
  bool sidebar_overlays = false;
  bool sidebar_on_right = false;
  float sidebar_width = 0;  // effective width after clamping
  bool sidebar_icon_only = false;

  Rect toolbar;
  Rect toolbar_island;
  Rect toolbar_hover;
  bool toolbar_visible = false;
  bool toolbar_overlays = false;
  ToolbarLayout toolbar_layout = ToolbarLayout::kMultiple;
  CompactMode compact_mode = CompactMode::kOff;

  Rect card;
  float card_radius = 0;

  // Shared window-move areas in host client coordinates. Native views exclude
  // interactive controls when hit-testing the toolbar and sidebar top strip.
  std::array<Rect, 3> drag_regions{};
  std::size_t drag_region_count = 0;
  // Header rows match the native caption band when the host supplies one.
  float chrome_row_height = 0;

  [[nodiscard]] std::span<const Rect> drag_area() const {
    return std::span(drag_regions).first(drag_region_count);
  }

  friend bool operator==(const ShellLayout&, const ShellLayout&) = default;
};

// Applies Zen's floating compact geometry without changing the page bounds.
// Both vertical and horizontal layouts use the same insets and hover corridors.
void PlaceCompactSurfaces(ShellLayout& layout,
                          Rect client,
                          bool square_frame,
                          const design::Tokens& tokens);

// Narrowest the card may get before the sidebar collapses automatically.
inline constexpr float kMinCardWidth =
    design::kTokens.window_constraints.minimum_page_width;

[[nodiscard]] constexpr float SidebarWidthAfterDrag(float initial_width,
                                                    float delta,
                                                    bool on_right) noexcept {
  return initial_width + (on_right ? -delta : delta);
}

// The three base layouts, independent of compact-mode visibility.
[[nodiscard]] ToolbarLayout BrowserLayoutOf(Flags<LayoutFlag> flags) noexcept;
[[nodiscard]] Flags<LayoutFlag> WithBrowserLayout(
    Flags<LayoutFlag> flags,
    ToolbarLayout layout) noexcept;

// The sidebar presentation that a set of layout flags selects.
[[nodiscard]] SidebarMode SidebarModeOf(Flags<LayoutFlag> flags) noexcept;

// `flags` changed to present the sidebar as `mode`; other choices are kept.
// Expanding from the Collapsed Sidebar layout selects Sidebar and Top Toolbar.
[[nodiscard]] Flags<LayoutFlag> WithSidebarMode(Flags<LayoutFlag> flags,
                                                SidebarMode mode) noexcept;

// What pressing `toggle` switches to from `mode`.
[[nodiscard]] SidebarMode NextSidebarMode(SidebarMode mode,
                                          SidebarToggle toggle) noexcept;

// The narrowest expanded sidebar. Only Sidebar puts the window caption
// buttons into the sidebar's top row, so it needs room for all three.
[[nodiscard]] float MinSidebarWidth(
    Flags<LayoutFlag> flags,
    const design::Tokens& tokens = design::kTokens,
    bool native_titlebar = false) noexcept;

// A requested expanded width, rounded to whole DIPs and kept in range.
[[nodiscard]] float ClampSidebarWidth(
    float width,
    Flags<LayoutFlag> flags,
    const design::Tokens& tokens = design::kTokens,
    bool native_titlebar = false) noexcept;

[[nodiscard]] ShellLayout ComputeLayout(
    const WindowState& window,
    const LayoutPrefs& prefs,
    bool sidebar_revealed,
    const design::Tokens& tokens = design::kTokens);

[[nodiscard]] ShellLayout ComputeLayout(
    const WindowState& window,
    const LayoutPrefs& prefs,
    bool sidebar_revealed,
    bool toolbar_revealed,
    const design::Tokens& tokens = design::kTokens,
    const NativeTitleBarMetrics& native_titlebar = {},
    std::size_t tab_count = 0);

}  // namespace kr
