#include "krypton/ui/layout/shell_layout.hpp"

#include <algorithm>
#include <cmath>

#include "krypton/ui/layout/horizontal_layout.hpp"

namespace kr {
namespace {

CompactMode CompactTarget(Flags<LayoutFlag> flags,
                          ToolbarLayout toolbar_layout) noexcept {
  if (!flags.Has(LayoutFlag::kCompactMode))
    return CompactMode::kOff;
  if (flags.Has(LayoutFlag::kCompactTargetsExplicit) &&
      !flags.HasAny(LayoutFlag::kCompactHideSidebar |
                    LayoutFlag::kCompactHideToolbar))
    return CompactMode::kOff;
  // In Only Sidebar, navigation and tabs share the same bar. Compact mode hides
  // that whole bar for compact mode regardless of the remembered target choice.
  if (toolbar_layout == ToolbarLayout::kSingle)
    return !flags.Has(LayoutFlag::kCompactTargetsExplicit) ||
                   flags.Has(LayoutFlag::kCompactHideSidebar)
               ? CompactMode::kHideSidebar
               : CompactMode::kOff;
  const bool sidebar = flags.Has(LayoutFlag::kCompactHideSidebar);
  const bool toolbar = flags.Has(LayoutFlag::kCompactHideToolbar);
  if (sidebar && toolbar)
    return CompactMode::kHideBoth;
  return toolbar ? CompactMode::kHideToolbar : CompactMode::kHideSidebar;
}

}  // namespace

ToolbarLayout BrowserLayoutOf(Flags<LayoutFlag> flags) noexcept {
  if (flags.HasAny(LayoutFlag::kToolbarCollapsed |
                   LayoutFlag::kSidebarIconOnly))
    return ToolbarLayout::kCollapsed;
  return flags.Has(LayoutFlag::kToolbarSingle) ? ToolbarLayout::kSingle
                                               : ToolbarLayout::kMultiple;
}

Flags<LayoutFlag> WithBrowserLayout(Flags<LayoutFlag> flags,
                                    ToolbarLayout layout) noexcept {
  flags.Set(LayoutFlag::kToolbarSingle, layout == ToolbarLayout::kSingle);
  flags.Set(LayoutFlag::kToolbarCollapsed, layout == ToolbarLayout::kCollapsed);
  flags.Set(LayoutFlag::kSidebarIconOnly, layout == ToolbarLayout::kCollapsed);
  flags.Set(LayoutFlag::kSidebarAutohide, false);
  flags.Set(LayoutFlag::kSidebarCollapsed, false);
  return flags;
}

SidebarMode SidebarModeOf(Flags<LayoutFlag> flags) noexcept {
  // A fully hidden sidebar only comes back through a toggle, like a floating
  // one that is never revealed.
  if (flags.HasAny(LayoutFlag::kSidebarAutohide |
                   LayoutFlag::kSidebarCollapsed))
    return SidebarMode::kFloating;
  if (flags.HasAny(LayoutFlag::kSidebarIconOnly |
                   LayoutFlag::kToolbarCollapsed))
    return SidebarMode::kCollapsed;
  return SidebarMode::kExpanded;
}

Flags<LayoutFlag> WithSidebarMode(Flags<LayoutFlag> flags,
                                  SidebarMode mode) noexcept {
  if (mode == SidebarMode::kCollapsed)
    return WithBrowserLayout(flags, ToolbarLayout::kCollapsed);
  if (mode == SidebarMode::kExpanded)
    return WithBrowserLayout(flags,
                             BrowserLayoutOf(flags) == ToolbarLayout::kSingle
                                 ? ToolbarLayout::kSingle
                                 : ToolbarLayout::kMultiple);
  flags.Set(LayoutFlag::kSidebarAutohide, true);
  flags.Set(LayoutFlag::kSidebarIconOnly, false);
  flags.Set(LayoutFlag::kSidebarCollapsed, false);
  flags.Set(LayoutFlag::kToolbarCollapsed, false);
  return flags;
}

SidebarMode NextSidebarMode(SidebarMode mode, SidebarToggle toggle) noexcept {
  if (toggle == SidebarToggle::kCollapse)
    return mode == SidebarMode::kExpanded ? SidebarMode::kCollapsed
                                          : SidebarMode::kExpanded;
  switch (mode) {
    case SidebarMode::kExpanded:
      return SidebarMode::kCollapsed;
    case SidebarMode::kCollapsed:
      return SidebarMode::kFloating;
    case SidebarMode::kFloating:
      break;
  }
  return SidebarMode::kExpanded;
}

float MinSidebarWidth(Flags<LayoutFlag> flags,
                      const design::Tokens& tokens,
                      bool native_titlebar) noexcept {
  if (BrowserLayoutOf(flags) == ToolbarLayout::kSingle && !native_titlebar)
    return tokens.sidebar.caption_row_min_width +
           tokens.caption.button_width * 3;
  return tokens.sidebar.min_width;
}

float ClampSidebarWidth(float width,
                        Flags<LayoutFlag> flags,
                        const design::Tokens& tokens,
                        bool native_titlebar) noexcept {
  const float minimum = MinSidebarWidth(flags, tokens, native_titlebar);
  return std::clamp(std::round(width), minimum,
                    std::max(minimum, tokens.sidebar.max_width));
}

void PlaceCompactSurfaces(ShellLayout& layout,
                          Rect client,
                          bool square_frame,
                          const design::Tokens& tokens) {
  const auto& compact = tokens.compact;
  const float inset = std::min(
      {square_frame ? compact.no_padding_float_inset : compact.float_inset,
       client.width / 4, client.height / 4});
  const auto shadow_bounds = [&](Rect surface, Rect available) {
    const float left =
        std::max(available.x, surface.x - compact.shadow_padding);
    const float top = std::max(available.y, surface.y - compact.shadow_padding);
    return Rect{
        left, top,
        std::min(available.right(), surface.right() + compact.shadow_padding) -
            left,
        std::min(available.bottom(),
                 surface.bottom() + compact.shadow_padding) -
            top};
  };
  layout.toolbar_island = layout.toolbar_hover = layout.toolbar;
  if (layout.toolbar_overlays && !layout.toolbar.empty()) {
    const Rect slot{layout.toolbar.x, client.y, layout.toolbar.width,
                    client.height};
    const float horizontal = std::min(inset, slot.width / 4);
    layout.toolbar.x += horizontal;
    layout.toolbar.y += inset / 2;
    layout.toolbar.width -= 2 * horizontal;
    layout.toolbar.height = std::min(
        layout.toolbar.height,
        std::max(0.0f, client.bottom() - inset / 2 - layout.toolbar.y));
    layout.toolbar_island = shadow_bounds(layout.toolbar, slot);
    layout.toolbar_hover = {slot.x, slot.y, slot.width,
                            layout.toolbar_island.bottom() - slot.y};
  }
  layout.sidebar_island = layout.sidebar_hover = layout.sidebar;
  if (layout.sidebar_overlays && !layout.sidebar.empty()) {
    // Keep the two islands disjoint, including their shadow padding.
    const float top =
        std::max(layout.sidebar.y, layout.toolbar_island.empty()
                                       ? client.y
                                       : layout.toolbar_island.bottom());
    const Rect slot{client.x, top, client.width,
                    std::max(0.0f, client.bottom() - top)};
    layout.sidebar.width =
        std::min(layout.sidebar.width, client.width - 2 * inset);
    layout.sidebar.x = layout.sidebar_on_right
                           ? client.right() - inset - layout.sidebar.width
                           : client.x + inset;
    layout.sidebar.y = top + std::min(inset / 2, slot.height / 2);
    layout.sidebar.height =
        std::max(0.0f, slot.bottom() - inset / 2 - layout.sidebar.y);
    layout.sidebar_island = shadow_bounds(layout.sidebar, slot);
    const float left =
        layout.sidebar_on_right ? layout.sidebar_island.x : client.x;
    const float right = layout.sidebar_on_right ? client.right()
                                                : layout.sidebar_island.right();
    layout.sidebar_hover = {left, slot.y, right - left, slot.height};
  }
}

ShellLayout ComputeLayout(const WindowState& window,
                          const LayoutPrefs& prefs,
                          bool sidebar_revealed,
                          const design::Tokens& tokens) {
  return ComputeLayout(window, prefs, sidebar_revealed, false, tokens);
}

ShellLayout ComputeLayout(const WindowState& window,
                          const LayoutPrefs& prefs,
                          bool sidebar_revealed,
                          bool toolbar_revealed,
                          const design::Tokens& tokens,
                          const NativeTitleBarMetrics& native_titlebar,
                          std::size_t tab_count) {
  if (TabLayoutOf(prefs.flags) != TabLayout::kVertical)
    return ComputeHorizontalLayout(window, prefs, toolbar_revealed, tokens,
                                   native_titlebar, tab_count);
  ShellLayout layout;
  layout.native_titlebar = native_titlebar;
  const Rect client{window.client.x, window.client.y,
                    std::max(0.0f, window.client.width),
                    std::max(0.0f, window.client.height)};
  layout.sidebar_on_right = prefs.flags.Has(LayoutFlag::kSidebarRight);
  layout.toolbar_layout = BrowserLayoutOf(prefs.flags);
  const bool single = layout.toolbar_layout == ToolbarLayout::kSingle;
  layout.caption_overlay =
      single && native_titlebar.enabled && !layout.sidebar_on_right;
  layout.centered_address = prefs.flags.Has(LayoutFlag::kCenteredAddress);
  layout.compact_mode = CompactTarget(prefs.flags, layout.toolbar_layout);
  layout.sidebar_icon_only =
      layout.toolbar_layout == ToolbarLayout::kCollapsed ||
      prefs.flags.Has(LayoutFlag::kSidebarIconOnly);

  if (window.flags.HasAny(WindowFlag::kFullscreen |
                          WindowFlag::kTabFullscreen) ||
      client.empty()) {
    layout.card = client;
    return layout;
  }

  const float margin = std::min({prefs.flags.Has(LayoutFlag::kSquareFrame)
                                     ? 0.0f
                                     : tokens.spacing.window_margin,
                                 client.width / 2, client.height / 2});
  const float requested = prefs.sidebar_width > 0
                              ? prefs.sidebar_width
                              : tokens.sidebar.default_width;
  const float expanded = ClampSidebarWidth(requested, prefs.flags, tokens,
                                           native_titlebar.enabled);
  const float width = std::min(
      client.width,
      layout.sidebar_icon_only ? tokens.sidebar.collapsed_width : expanded);
  layout.sidebar_width = width;
  layout.card_radius =
      prefs.flags.Has(LayoutFlag::kSquareFrame) ? 0 : tokens.radius.card;

  const bool compact_sidebar =
      layout.compact_mode == CompactMode::kHideSidebar ||
      layout.compact_mode == CompactMode::kHideBoth;
  const bool compact_toolbar =
      layout.compact_mode == CompactMode::kHideToolbar ||
      layout.compact_mode == CompactMode::kHideBoth;
  const bool too_narrow = client.width < width + margin + kMinCardWidth;
  const bool autohide = prefs.flags.Has(LayoutFlag::kSidebarAutohide);
  const bool fully_hidden = prefs.flags.Has(LayoutFlag::kSidebarCollapsed);
  const bool sidebar_reserved =
      !fully_hidden && !too_narrow && !autohide && !compact_sidebar;
  if (single && native_titlebar.enabled && !sidebar_reserved)
    layout.caption_overlay = true;
  const bool sidebar_revealable =
      !fully_hidden && (compact_sidebar || autohide || (too_narrow && single));
  const bool has_toolbar = layout.toolbar_layout != ToolbarLayout::kSingle;
  const float native_height =
      native_titlebar.enabled
          ? std::clamp(native_titlebar.height, 0.0f, client.height)
          : 0;
  layout.chrome_row_height =
      native_height > 0 ? native_height
                        : std::min(single ? tokens.spacing.single_header_height
                                          : tokens.spacing.header_height,
                                   client.height);
  const float toolbar_height =
      native_height > 0
          ? native_height
          : std::min(tokens.spacing.toolbar_height, client.height);
  const float reserved_width = sidebar_reserved ? width : 0;
  const float toolbar_x =
      client.x + (layout.sidebar_on_right ? 0 : reserved_width);
  const float toolbar_width = client.width - reserved_width;

  if (has_toolbar) {
    layout.toolbar_visible = !compact_toolbar || toolbar_revealed;
    layout.toolbar_overlays = compact_toolbar && toolbar_revealed;
    layout.toolbar = {toolbar_x, client.y, toolbar_width,
                      layout.toolbar_visible
                          ? toolbar_height
                          : std::min({tokens.sidebar.reveal_width, client.width,
                                      client.height})};
    if (native_titlebar.enabled) {
      const float left = std::clamp(client.x + native_titlebar.left_inset,
                                    layout.toolbar.x, layout.toolbar.right());
      const float right =
          std::clamp(client.right() - native_titlebar.right_inset, left,
                     layout.toolbar.right());
      layout.toolbar.x = left;
      layout.toolbar.width = right - left;
    }
  }

  // Thin event strips fit within the page margin. Reveals leave the page in
  // place: only deliberately revealed compact bars may cover its contents.
  const float top =
      std::max(single ? 0.0f : native_height,
               has_toolbar && !compact_toolbar ? toolbar_height : margin);
  layout.card = client.Inset(
      layout.sidebar_on_right ? margin : std::max(reserved_width, margin), top,
      layout.sidebar_on_right ? std::max(reserved_width, margin) : margin,
      margin);

  if (layout.caption_overlay) {
    layout.caption_visible = toolbar_revealed;
    layout.caption_reveal = {
        layout.card.x, client.y, layout.card.width,
        toolbar_revealed ? std::max(native_height, margin) : margin};
  }

  const auto sidebar_at_edge = [&](float sidebar_width, float top_offset) {
    return Rect{
        layout.sidebar_on_right ? client.right() - sidebar_width : client.x,
        client.y + top_offset, sidebar_width,
        std::max(0.0f, client.height - top_offset)};
  };
  if (sidebar_reserved) {
    const bool shares_caption_edge =
        native_titlebar.enabled &&
        (!layout.caption_overlay || layout.caption_visible) &&
        (layout.sidebar_on_right ? native_titlebar.right_inset > 0
                                 : native_titlebar.left_inset > 0);
    layout.sidebar =
        sidebar_at_edge(width, shares_caption_edge ? native_height : 0);
    layout.sidebar_visible = true;
  } else if (sidebar_revealable) {
    layout.sidebar_visible = sidebar_revealed;
    layout.sidebar_overlays = sidebar_revealed;
    // The floating sidebar starts below the top bar (or its event strip), so
    // the two native islands never compete for the same input coordinates.
    layout.sidebar = sidebar_at_edge(
        sidebar_revealed ? width
                         : std::min({tokens.sidebar.reveal_width, client.width,
                                     client.height}),
        std::max(layout.toolbar.height,
                 !layout.caption_overlay ||
                         (layout.sidebar_on_right && layout.caption_visible)
                     ? native_height
                     : 0.0f));
  }

  PlaceCompactSurfaces(layout, client,
                       prefs.flags.Has(LayoutFlag::kSquareFrame), tokens);

  // Native views hit-test their toolbar's blank space for window movement.
  const float drag_left = std::max(
      layout.card.x,
      client.x + (native_titlebar.enabled &&
                          (!layout.caption_overlay || layout.caption_visible)
                      ? native_titlebar.left_inset
                      : 0));
  const float drag_right = std::min(
      layout.card.right(),
      client.right() - (native_titlebar.enabled && (!layout.caption_overlay ||
                                                    layout.caption_visible)
                            ? native_titlebar.right_inset
                            : 0));
  const Rect page_strip{drag_left, client.y + layout.toolbar.height,
                        std::max(0.0f, drag_right - drag_left),
                        std::max(0.0f, top - layout.toolbar.height)};
  if (!page_strip.empty())
    layout.drag_regions[layout.drag_region_count++] = page_strip;
  if (layout.sidebar_visible) {
    const Rect strip{layout.sidebar.x, layout.sidebar.y, layout.sidebar.width,
                     native_titlebar.enabled
                         ? std::min(native_height, layout.sidebar.height)
                         : std::min({tokens.spacing.drag_strip_height,
                                     tokens.spacing.sidebar_padding,
                                     layout.sidebar.height})};
    if (!strip.empty())
      layout.drag_regions[layout.drag_region_count++] = strip;
  }
  if (layout.toolbar_visible && !layout.toolbar.empty())
    layout.drag_regions[layout.drag_region_count++] = layout.toolbar;
  return layout;
}

}  // namespace kr
