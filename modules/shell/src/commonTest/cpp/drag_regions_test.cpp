#include "krypton/ui/layout/drag_regions.hpp"

#include <array>

#include "krypton/testing/test.hpp"

namespace kr {
namespace {

bool Overlaps(Rect a, Rect b) {
  return a.x < b.right() && b.x < a.right() && a.y < b.bottom() &&
         b.y < a.bottom();
}

float Area(std::span<const Rect> regions) {
  float area = 0;
  for (const auto& region : regions)
    area += region.width * region.height;
  return area;
}

KR_TEST(TitleBarDragRegionsPreserveBlankSpaceAndExcludeControls) {
  DragRegions layout;
  const std::array draggable{Rect{0, 0, 300, 40}};
  const std::array controls{Rect{30, 5, 60, 30}, Rect{160, 0, 80, 40}};
  const auto regions = layout.Update(draggable, controls);
  KR_EXPECT_EQ(Area(regions), 7000.0f);
  for (std::size_t i = 0; i < regions.size(); ++i) {
    KR_EXPECT(!regions[i].empty());
    for (const auto& control : controls)
      KR_EXPECT(!Overlaps(regions[i], control));
    for (std::size_t j = i + 1; j < regions.size(); ++j)
      KR_EXPECT(!Overlaps(regions[i], regions[j]));
  }
}

KR_TEST(TitleBarDragRegionsFollowMovedAndTranslatedControls) {
  DragRegions layout;
  for (const auto origin : {Point{0, 0}, Point{17.5f, 8.25f}}) {
    const std::array draggable{Rect{origin.x, origin.y, 200, 32}};
    for (const float width : {20.0f, 90.5f, 200.0f}) {
      // A longer translated label or a resized header changes measured bounds.
      const std::array controls{Rect{origin.x, origin.y, width, 32}};
      const auto regions = layout.Update(draggable, controls);
      KR_EXPECT_EQ(Area(regions), (200 - width) * 32);
      for (const auto& region : regions)
        KR_EXPECT(!Overlaps(region, controls.front()));
    }
  }
}

KR_TEST(TitleBarDragRegionsClearWhenChromeIsHidden) {
  DragRegions layout;
  const std::array draggable{Rect{0, 0, 200, 32}};
  KR_EXPECT(!layout.Update(draggable, {}).empty());
  KR_EXPECT(layout.Update({}, {}).empty());
  const std::array outside{Rect{200, 0, 40, 32}, Rect{0, 0, 0, 32}};
  KR_EXPECT_EQ(Area(layout.Update(draggable, outside)), 6400.0f);
}

}  // namespace
}  // namespace kr
