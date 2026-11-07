#include "krypton/ui/layout/popup_layout.hpp"

#include <algorithm>
#include <cmath>

namespace kr {
Rect PopupViewport(Rect client, const design::Tokens& tokens) noexcept {
  if (client.empty())
    return {client.x, client.y, 0, 0};
  const float margin = std::min(
      {tokens.spacing.window_margin, client.width / 2, client.height / 2});
  return client.Inset(margin);
}

Rect PlaceNotification(Rect client,
                       PopupExtent desired,
                       bool sidebar_on_right,
                       const design::Tokens& tokens) noexcept {
  const auto margin =
      std::min(tokens.notification.margin,
               std::max(0.f, std::min(client.width, client.height) / 2));
  const auto width =
      std::clamp(desired.width, 0.f, std::max(0.f, client.width - 2 * margin));
  const auto height = std::clamp(desired.height, 0.f,
                                 std::max(0.f, client.height - 2 * margin));
  return {
      sidebar_on_right ? client.x + margin : client.right() - width - margin,
      client.y + margin, width, height};
}

Rect PlaceAnchoredPopup(Rect client,
                        Rect anchor,
                        PopupExtent desired,
                        bool right_aligned,
                        const design::Tokens& tokens) noexcept {
  const auto available = PopupViewport(client, tokens);
  if (available.empty() || !std::isfinite(desired.width) ||
      !std::isfinite(desired.height))
    return {available.x, available.y, 0, 0};
  const float width = std::clamp(desired.width, 0.0f, available.width);
  const float below_y =
      std::clamp(anchor.bottom(), available.y, available.bottom());
  const float above_y = std::clamp(anchor.y, available.y, available.bottom());
  const float below = available.bottom() - below_y,
              above = above_y - available.y;
  const bool use_below = desired.height <= below || below >= above;
  const float room = std::max(below, above) > 0 ? (use_below ? below : above)
                                                : available.height;
  const float height = std::clamp(desired.height, 0.0f, room);
  const float left = right_aligned ? anchor.right() - width : anchor.x;
  const float top = use_below ? below_y : above_y - height;
  return {std::clamp(left, available.x, available.right() - width),
          std::clamp(top, available.y, available.bottom() - height), width,
          height};
}

Rect PlaceSiteToolsPopup(Rect client,
                         Rect anchor,
                         PopupExtent desired,
                         bool right_to_left,
                         const design::Tokens& tokens) noexcept {
  anchor.x +=
      right_to_left ? -tokens.site_data_menu.gap : tokens.site_data_menu.gap;
  return PlaceAnchoredPopup(client, anchor, desired, right_to_left, tokens);
}
}  // namespace kr
