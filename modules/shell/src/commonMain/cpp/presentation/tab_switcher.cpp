#include "krypton/ui/presentation/tab_switcher.hpp"

#include <algorithm>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/model/tab_switcher.hpp"

namespace kr {
TabSwitcherMetrics MeasureTabSwitcher(Rect client, std::size_t count) noexcept {
  constexpr auto& t = design::kTokens.tab_switcher;
  const auto available = client.Inset(t.margin);
  // Keep one row even in narrow windows: shrink previews instead of wrapping.
  const int columns =
      static_cast<int>(std::clamp(count, std::size_t{1}, kTabSwitcherPageSize));
  const float desired_width =
      columns * t.card_width + (columns - 1) * t.gap + 2 * t.padding;
  const float width =
      std::max(1.0f, std::min({t.max_width, available.width, desired_width}));
  const float tile =
      std::max(1.0f, (width - 2 * t.padding - (columns - 1) * t.gap) / columns);
  const float image_height = std::max(
      1.0f, std::min((tile - 2 * (t.card_padding + t.border_width)) /
                         t.thumbnail_aspect_ratio,
                     available.height - 2 * t.padding - 2 * t.header_gap -
                         2 * t.title_height - t.title_height -
                         2 * (t.card_padding + t.border_width)));
  const float height = 2 * t.padding + 2 * t.header_gap + 2 * t.title_height +
                       image_height + t.title_height +
                       2 * (t.card_padding + t.border_width);
  return {
      {client.x + (client.width - width) / 2,
       client.y + std::max(0.0f, (client.height - height) / 2), width, height},
      columns,
      image_height};
}
}  // namespace kr
