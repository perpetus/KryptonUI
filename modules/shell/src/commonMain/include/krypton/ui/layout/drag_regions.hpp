#pragma once

#include <span>
#include <vector>

#include "krypton/ui/core/geometry.hpp"

namespace kr {

// Reuses its buffers when native controls move or change size. All bounds are
// measured in client DIPs; the platform converts the result at the API edge.
class DragRegions {
 public:
  [[nodiscard]] std::span<const Rect> Update(std::span<const Rect> draggable,
                                             std::span<const Rect> controls);

 private:
  std::vector<Rect> regions_;
  std::vector<Rect> scratch_;
};

}  // namespace kr
