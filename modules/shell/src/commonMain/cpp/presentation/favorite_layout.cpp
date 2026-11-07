#include "krypton/ui/presentation/favorite_layout.hpp"

#include <algorithm>
#include <cmath>

namespace kr {

std::size_t FavoriteColumns(float width,
                            std::size_t count,
                            const design::Tokens& tokens) noexcept {
  if (count == 0)
    return 0;
  if (!std::isfinite(width) || width <= 0)
    return 1;
  const auto& spacing = tokens.spacing;
  const float minimum =
      std::max(width * spacing.favorite_min_fraction,
               spacing.favorite_height + spacing.favorite_gap);
  const float columns = std::floor((width + spacing.favorite_gap) /
                                   (minimum + spacing.favorite_gap));
  return std::min(count, static_cast<std::size_t>(std::max(1.0f, columns)));
}

}  // namespace kr
