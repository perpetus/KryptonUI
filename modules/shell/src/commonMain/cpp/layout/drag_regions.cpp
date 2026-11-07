#include "krypton/ui/layout/drag_regions.hpp"

#include <algorithm>

namespace kr {

std::span<const Rect> DragRegions::Update(std::span<const Rect> draggable,
                                          std::span<const Rect> controls) {
  regions_.clear();
  for (const auto& region : draggable)
    if (!region.empty())
      regions_.push_back(region);

  for (const auto& control : controls) {
    if (control.empty())
      continue;
    scratch_.clear();
    for (const auto& region : regions_) {
      const float left = std::max(region.x, control.x);
      const float top = std::max(region.y, control.y);
      const float right = std::min(region.right(), control.right());
      const float bottom = std::min(region.bottom(), control.bottom());
      if (left >= right || top >= bottom) {
        scratch_.push_back(region);
        continue;
      }
      if (region.y < top)
        scratch_.push_back({region.x, region.y, region.width, top - region.y});
      if (bottom < region.bottom())
        scratch_.push_back(
            {region.x, bottom, region.width, region.bottom() - bottom});
      if (region.x < left)
        scratch_.push_back({region.x, top, left - region.x, bottom - top});
      if (right < region.right())
        scratch_.push_back({right, top, region.right() - right, bottom - top});
    }
    regions_.swap(scratch_);
  }
  return regions_;
}

}  // namespace kr
