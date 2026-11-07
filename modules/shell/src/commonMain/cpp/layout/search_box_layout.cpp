#include "krypton/ui/layout/search_box_layout.hpp"

#include <algorithm>

#include "krypton/ui/layout/popup_layout.hpp"

namespace kr {

float ComputeSearchResultsMaxHeight(Rect client,
                                    float measured_input_height,
                                    const design::Tokens& tokens) noexcept {
  const auto& search = tokens.overlay;
  const float input_height =
      std::max(search.input_height, measured_input_height);
  const float available = PopupViewport(client, tokens).height -
                          2 * search.border_width - input_height -
                          search.results_bottom_margin;
  return std::clamp(available, 0.0f, search.results_max_height);
}

Rect ComputeSearchBoxBounds(Rect client,
                            float measured_height,
                            float root_font_size,
                            const design::Tokens& tokens) noexcept {
  if (client.empty())
    return {client.x, client.y, 0, 0};
  const auto& search = tokens.overlay;
  const Rect available = PopupViewport(client, tokens);
  const float preferred =
      std::min(client.width / search.width_divisor, search.width);
  const float minimum =
      std::min(client.width * search.minimum_width_fraction,
               std::max(0.0f, root_font_size) * search.minimum_width_rem);
  // Keep Zen's width limit even when the native root font/text scale makes
  // the relative minimum larger. Content wraps or scrolls within the popup.
  const float width =
      std::min({available.width, search.width, std::max(preferred, minimum)});
  const float height = std::clamp(measured_height, 0.0f, available.height);
  const float centered_height = std::max(search.center_minimum_height, height);
  const float top =
      std::clamp(client.y + (client.height - centered_height) / 2, available.y,
                 std::max(available.y, available.bottom() - height));
  return {client.x + (client.width - width) / 2, top, width, height};
}

Rect ComputeAttachedSearchBoxBounds(Rect client,
                                    Rect address,
                                    float measured_height,
                                    const design::Tokens&) noexcept {
  // The URL field already includes chrome spacing; do not inset it again.
  const auto available = client;
  if (available.empty() || address.empty())
    return {};
  const float width = std::min(address.width, available.width);
  const float left =
      std::clamp(address.x, available.x, available.right() - width);
  // The editor stays over the URL field; the results scroll near the bottom.
  const float top = std::clamp(address.y, available.y, available.bottom());
  const float height =
      std::clamp(measured_height, 0.0f, available.bottom() - top);
  return {left, top, width, height};
}
}  // namespace kr
