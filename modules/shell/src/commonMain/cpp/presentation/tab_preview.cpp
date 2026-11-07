#include "krypton/ui/presentation/tab_preview.hpp"

#include <algorithm>
#include <cmath>

#include "krypton/ui/layout/popup_layout.hpp"
#include "krypton/ui/presentation/view_model.hpp"

namespace kr {
std::chrono::milliseconds TabPreviewShowDelay(
    float width,
    float minimum,
    float standard,
    const design::Tokens& tokens) noexcept {
  const auto& t = tokens.tab_hover;
  float delay = t.show_delay_minimum_ms;
  if (width > minimum && standard > minimum) {
    const float fraction =
        std::log(width - minimum + 1) / std::log(standard - minimum + 1);
    delay += fraction * (t.show_delay_maximum_ms - t.show_delay_minimum_ms);
    if (width >= standard)
      delay += t.show_delay_standard_extra_ms;
  }
  return std::chrono::milliseconds(std::lround(delay));
}

TabPreviewColors ResolveTabPreviewColors(const ThemeVm& theme) noexcept {
  const auto& palette = *theme.palette;
  const auto& dark = design::kTokens.tab_hover_dark;
  const Color background{theme.dark ? dark.background : palette.overlay};
  return {
      .background = theme.high_contrast
                        ? Color{palette.gradient_base}
                        : Mix(background, theme.gradient_start,
                              design::kTokens.tab_hover.theme_tint)
                              .WithAlpha(background.a()),
      .border = Color{theme.dark ? dark.border : palette.overlay_border},
      .thumbnail = Color{theme.dark ? dark.thumbnail : palette.page_background},
  };
}

Rect HorizontalTabPreviewViewport(Rect client,
                                  Rect chrome,
                                  const design::Tokens& tokens) noexcept {
  const auto viewport = PopupViewport(client, tokens);
  if (viewport.empty() || chrome.empty())
    return {};
  const float top = std::clamp(
      chrome.bottom() + std::max(0.0f, tokens.tab_hover.placement_gap),
      viewport.y, viewport.bottom());
  return {viewport.x, top, viewport.width, viewport.bottom() - top};
}
Rect PlaceHorizontalTabPreview(Rect client,
                               Rect chrome,
                               Rect tab,
                               float desired_height,
                               bool right_to_left,
                               const design::Tokens& tokens) noexcept {
  if (client.empty() || chrome.empty() || tab.empty())
    return {};
  const auto viewport = HorizontalTabPreviewViewport(client, chrome, tokens);
  if (viewport.empty() || !std::isfinite(desired_height))
    return {};
  const float width = MeasureTabPreview(viewport.width, tokens).width;
  const float left = right_to_left ? tab.right() - width : tab.x;
  return {std::clamp(left, viewport.x, viewport.right() - width), viewport.y,
          width, std::clamp(desired_height, 0.0f, viewport.height)};
}

std::string_view TabPreviewDomain(std::string_view url) noexcept {
  const auto scheme = url.find("://");
  if (scheme == std::string_view::npos)
    return url;
  auto authority = url.substr(scheme + 3);
  authority = authority.substr(0, authority.find_first_of("/?#"));
  const auto credentials = authority.rfind('@');
  if (credentials != std::string_view::npos)
    authority.remove_prefix(credentials + 1);
  return authority;
}

TabPreviewMetrics MeasureTabPreview(float available_width,
                                    const design::Tokens& tokens) noexcept {
  const auto& hover = tokens.tab_hover;
  const float width = std::max(0.0f, std::min(available_width, hover.width));
  const float image_width = std::max(0.0f, width - 2 * hover.border_width);
  return {width, hover.thumbnail_aspect_ratio > 0
                     ? image_width / hover.thumbnail_aspect_ratio
                     : 0};
}

TabPreviewMetrics MeasureTabPreviewForLayout(
    Rect client,
    const ShellLayout& layout,
    const design::Tokens& tokens) noexcept {
  if (layout.tabs != TabLayout::kVertical)
    return MeasureTabPreview(
        HorizontalTabPreviewViewport(client, layout.toolbar, tokens).width,
        tokens);
  const float gap = std::max(0.0f, tokens.tab_hover.placement_gap);
  const float available = layout.sidebar.empty() ? 0.0f
                          : layout.sidebar_on_right
                              ? layout.sidebar.x - gap - client.x
                              : client.right() - layout.sidebar.right() - gap;
  return MeasureTabPreview(available, tokens);
}

Rect PlaceTabPreview(Rect client,
                     Rect sidebar,
                     Rect tab,
                     float desired_height,
                     bool on_right,
                     const design::Tokens& tokens) noexcept {
  if (client.empty() || sidebar.empty() || tab.empty())
    return {};
  const float gap = std::max(0.0f, tokens.tab_hover.placement_gap);
  const float edge = on_right ? sidebar.x - gap : sidebar.right() + gap;
  const float available = on_right ? edge - client.x : client.right() - edge;
  const float width = MeasureTabPreview(available, tokens).width;
  const float height = std::clamp(desired_height, 0.0f, client.height);
  return {on_right ? edge - width : edge,
          std::clamp(tab.y, client.y, client.bottom() - height), width, height};
}

}  // namespace kr
