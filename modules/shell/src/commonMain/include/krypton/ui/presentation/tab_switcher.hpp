#pragma once

#include <cstddef>

#include "krypton/ui/core/geometry.hpp"

namespace kr {
struct TabSwitcherMetrics {
  Rect bounds;
  int columns = 1;
  float image_height = 1;
};
[[nodiscard]] TabSwitcherMetrics MeasureTabSwitcher(Rect client,
                                                    std::size_t count) noexcept;
}  // namespace kr
