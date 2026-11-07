#include "krypton/ui/layout/horizontal_layout.hpp"

#include <algorithm>
#include <cmath>

namespace kr {
HorizontalTabWidths HorizontalTabWidthsFor(
    float available,
    std::size_t count,
    bool has_active,
    const design::Tokens& tokens) noexcept {
  if (!count || !std::isfinite(available) || available <= 0)
    return {};
  const auto& h = tokens.horizontal_tabs;
  const float margins = 2 * tokens.spacing.tab_inset;
  const float shared = available / static_cast<float>(count) - margins;
  if (shared >= h.tab_minimum_active_width) {
    const float width = std::min(shared, h.tab_standard_width);
    return {width, width};
  }
  const float active = h.tab_minimum_active_width;
  const std::size_t inactive = count - (has_active ? 1 : 0);
  if (!inactive)
    return {active, active};
  const float rest = available - (has_active ? active + margins : 0);
  return {active, std::clamp(rest / static_cast<float>(inactive) - margins,
                             h.tab_minimum_inactive_width, active)};
}

float HorizontalTabsExtent(HorizontalTabWidths widths,
                           std::size_t count,
                           bool has_active,
                           const design::Tokens& tokens) noexcept {
  if (!count)
    return 0;
  const std::size_t active = has_active ? 1 : 0;
  const float margins = 2 * tokens.spacing.tab_inset;
  return static_cast<float>(active) * (widths.active + margins) +
         static_cast<float>(count - active) * (widths.inactive + margins);
}

float HorizontalNewTabOffset(float viewport_width,
                             float tabs_extent,
                             bool right_to_left) noexcept {
  if (!std::isfinite(viewport_width) || !std::isfinite(tabs_extent) ||
      viewport_width <= 0)
    return 0;
  const float gap =
      viewport_width - std::clamp(tabs_extent, 0.0f, viewport_width);
  return right_to_left ? gap : -gap;
}

float HorizontalTabHeight(float row_height,
                          const design::Tokens& tokens) noexcept {
  if (!std::isfinite(row_height) || row_height <= 0)
    return tokens.spacing.tab_height;
  return std::clamp(row_height - 2 * tokens.spacing.tab_gap, 0.0f,
                    tokens.spacing.tab_height);
}

TabRowContent HorizontalTabContent(float width,
                                   bool active,
                                   bool audio,
                                   const design::Tokens& tokens) noexcept {
  const auto& s = tokens.spacing;
  const auto& h = tokens.horizontal_tabs;
  TabRowContent result;
  if (!(width >= h.tab_minimum_inactive_width))
    return result;
  // Icons follow the row's start padding; the close button ends the row.
  float available = width - s.tab_padding;
  const auto fits = [&available](float size) {
    if (size > available)
      return false;
    available -= size;
    return true;
  };
  const float icon = s.icon_size + s.tab_icon_gap;
  if (active) {
    result.close = true;
    available -= s.close_button_size;
    result.audio = audio && fits(icon);
    result.icon = fits(icon);
    return result;
  }
  result.audio = audio && fits(icon);
  result.icon = fits(icon);
  result.close =
      width - 2 * h.tab_content_inset >= h.tab_close_minimum_content_width &&
      fits(s.close_button_size);
  if (!result.audio && !result.icon && !result.close)
    result.icon = result.centered = true;
  return result;
}
TabLayout TabLayoutOf(Flags<LayoutFlag> flags) noexcept {
  if (flags.Has(LayoutFlag::kTabsDynamic))
    return TabLayout::kDynamic;
  if (flags.Has(LayoutFlag::kTabsCompact))
    return TabLayout::kCompact;
  return flags.Has(LayoutFlag::kTabsClassic) ? TabLayout::kClassic
                                             : TabLayout::kVertical;
}

Flags<LayoutFlag> WithTabLayout(Flags<LayoutFlag> flags,
                                TabLayout layout) noexcept {
  flags.Set(LayoutFlag::kTabsClassic, layout == TabLayout::kClassic);
  flags.Set(LayoutFlag::kTabsCompact, layout == TabLayout::kCompact);
  flags.Set(LayoutFlag::kTabsDynamic, layout == TabLayout::kDynamic);
  return flags;
}

ShellLayout ComputeHorizontalLayout(const WindowState& window,
                                    const LayoutPrefs& prefs,
                                    bool revealed,
                                    const design::Tokens& tokens,
                                    const NativeTitleBarMetrics& titlebar,
                                    std::size_t tab_count) {
  ShellLayout result;
  result.tabs = TabLayoutOf(prefs.flags);
  result.native_titlebar = titlebar;
  result.centered_address = prefs.flags.Has(LayoutFlag::kCenteredAddress) &&
                            result.tabs != TabLayout::kCompact;
  const Rect client{window.client.x, window.client.y,
                    std::max(0.0f, window.client.width),
                    std::max(0.0f, window.client.height)};
  result.card = client;
  if (client.empty() ||
      window.flags.HasAny(WindowFlag::kFullscreen | WindowFlag::kTabFullscreen))
    return result;

  const bool rounded = !prefs.flags.Has(LayoutFlag::kSquareFrame);
  const float margin = rounded ? std::min({tokens.spacing.window_margin,
                                           client.width / 2, client.height / 2})
                               : 0;
  const float caption =
      titlebar.enabled ? std::clamp(titlebar.height, 0.0f, client.height) : 0;
  const float tab_height =
      caption > 0 ? caption
                  : tokens.spacing.tab_height + 2 * tokens.spacing.tab_gap;
  const float navigation =
      caption > 0
          ? caption
          : std::min(std::max({tokens.spacing.toolbar_height, caption,
                               result.tabs == TabLayout::kCompact ? tab_height
                                                                  : 0.0f}),
                     client.height);
  result.chrome_row_height = navigation;
  const bool separate = result.tabs == TabLayout::kClassic ||
                        (result.tabs == TabLayout::kDynamic && tab_count > 1);
  const float strip =
      separate ? std::min(tab_height, client.height - navigation) : 0;
  const float height = navigation + strip;
  const bool hidden = prefs.flags.Has(LayoutFlag::kCompactMode) &&
                      (prefs.flags.Has(LayoutFlag::kCompactHideToolbar) ||
                       (!prefs.flags.Has(LayoutFlag::kCompactTargetsExplicit) &&
                        !prefs.flags.Has(LayoutFlag::kCompactHideSidebar)));
  result.compact_mode = !prefs.flags.Has(LayoutFlag::kCompactMode)
                            ? CompactMode::kOff
                        : hidden ? CompactMode::kHideToolbar
                                 : CompactMode::kOff;
  result.toolbar_visible = !hidden || revealed;
  result.toolbar_overlays = hidden && revealed;
  const float left = titlebar.enabled
                         ? std::clamp(titlebar.left_inset, 0.0f, client.width)
                         : 0;
  const float right = titlebar.enabled ? std::clamp(titlebar.right_inset, 0.0f,
                                                    client.width - left)
                                       : 0;
  result.toolbar = {client.x, client.y, client.width,
                    result.toolbar_visible
                        ? height
                        : std::min(tokens.sidebar.reveal_width, client.height)};
  PlaceCompactSurfaces(result, client, !rounded, tokens);
  result.navigation = {
      result.toolbar.x,
      result.toolbar.y + (result.tabs == TabLayout::kClassic ? strip : 0),
      result.toolbar.width, navigation};
  result.tab_strip = {
      result.toolbar.x,
      result.toolbar.y + (result.tabs == TabLayout::kDynamic ? navigation : 0),
      result.toolbar.width, strip};
  if (result.tabs == TabLayout::kCompact)
    result.tab_strip = result.navigation;
  // Caption buttons share only the row at the top of the window. Reserving
  // their width on both rows leaves an empty band after Classic's app menu
  // and unnecessarily shortens Dynamic's lower tab strip.
  const auto reserve_caption = [&](Rect& row) {
    if (!titlebar.enabled || row.empty() || row.y >= client.y + caption)
      return;
    const float start = std::max(row.x, client.x + left);
    const float end = std::min(row.right(), client.right() - right);
    row.x = start;
    row.width = std::max(0.0f, end - start);
  };
  reserve_caption(result.navigation);
  reserve_caption(result.tab_strip);
  result.tab_strip_visible = result.toolbar_visible &&
                             (separate || result.tabs == TabLayout::kCompact);
  result.card = client.Inset(
      margin, std::max(caption, hidden ? margin : height), margin, margin);
  result.card_radius = rounded ? tokens.radius.card : 0;
  if (result.toolbar_visible && !result.toolbar.empty())
    result.drag_regions[result.drag_region_count++] = result.toolbar;
  return result;
}

Rect CenteredAddressBounds(Rect slot,
                           float toolbar_width,
                           float minimum_width,
                           const design::Tokens& tokens) noexcept {
  if (!std::isfinite(toolbar_width) || slot.empty())
    return slot;
  float reduction = 0;
  for (const auto& step : tokens.horizontal_tabs.centered_steps)
    if (toolbar_width >= step.minimum_width) {
      reduction = toolbar_width * step.reduction;
      break;
    }
  if (reduction <= 0)
    return slot;
  const float width = std::clamp(
      slot.width - reduction,
      std::min({minimum_width, slot.width,
                tokens.horizontal_tabs.centered_max_width}),
      std::min(tokens.horizontal_tabs.centered_max_width, slot.width));
  const float ideal = (toolbar_width - width) / 2;
  return {ideal >= slot.x && ideal + width <= slot.right()
              ? ideal
              : slot.x + (slot.width - width) / 2,
          slot.y, width, slot.height};
}

float CompactAddressWidth(float available,
                          float minimum_address,
                          float minimum_tabs,
                          const design::Tokens& tokens) noexcept {
  if (!std::isfinite(available) || available <= 0)
    return 0;
  const float minimum = std::clamp(minimum_address, 0.0f, available);
  return std::clamp(
      available - std::max(0.0f, minimum_tabs), minimum,
      std::max(
          minimum,
          std::min(available, tokens.horizontal_tabs.compact_address_width)));
}
}  // namespace kr
