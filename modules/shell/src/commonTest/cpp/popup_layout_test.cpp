#include "krypton/ui/layout/popup_layout.hpp"

#include <array>
#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/layout/search_box_layout.hpp"

namespace kr {
namespace {
KR_TEST(AnchoredPopupFitsTheClientAndFlipsAboveWhenNeeded) {
  const Rect client{0, 0, 1280, 800};
  const Rect top_anchor{18, 12, 30, 28};
  const auto below = PlaceAnchoredPopup(client, top_anchor, {320, 600}, false);
  KR_EXPECT_EQ(below.x, top_anchor.x);
  KR_EXPECT_EQ(below.y, top_anchor.bottom());
  KR_EXPECT_EQ(below.height, 600.0f);
  const Rect bottom_anchor{18, 740, 30, 28};
  const auto above =
      PlaceAnchoredPopup(client, bottom_anchor, {320, 400}, false);
  KR_EXPECT_EQ(above.bottom(), bottom_anchor.y);
  KR_EXPECT_EQ(above.height, 400.0f);
}

KR_TEST(AnchoredPopupClampsLargeTranslatedMenusToSmallAndOffsetWindows) {
  for (const Rect client : {Rect{0, 0, 320, 200}, Rect{17.5f, 8.25f, 700, 400},
                            Rect{0, 0, 8, 6}, Rect{0, 0, 0, 0}}) {
    for (const Rect anchor :
         {Rect{client.x, client.y, 28, 28},
          Rect{client.right() - 28, client.y + 20, 28, 28},
          Rect{client.right() + 500, client.bottom() + 500, 28, 28}}) {
      for (const PopupExtent desired :
           {PopupExtent{300, 600}, PopupExtent{1600, 2400},
            PopupExtent{0, 0}}) {
        for (const bool rtl : {false, true}) {
          const auto frame = PlaceAnchoredPopup(client, anchor, desired, rtl);
          KR_EXPECT(frame.x >= client.x && frame.y >= client.y);
          KR_EXPECT(frame.right() <= client.right() &&
                    frame.bottom() <= client.bottom());
          KR_EXPECT(frame.width >= 0 && frame.height >= 0);
        }
      }
    }
  }
  KR_EXPECT(PlaceAnchoredPopup({0, 0, 800, 600}, {},
                               {std::numeric_limits<float>::infinity(), 600},
                               false)
                .empty());
}

KR_TEST(AnchoredPopupUsesPhysicalClientCoordinatesWithIndependentRtlAlignment) {
  const Rect client{0, 0, 1280, 800}, anchor{800, 20, 30, 28};
  const auto left = PlaceAnchoredPopup(client, anchor, {320, 400}, false);
  const auto right = PlaceAnchoredPopup(client, anchor, {320, 400}, true);
  KR_EXPECT_EQ(left.x, anchor.x);
  KR_EXPECT_EQ(right.right(), anchor.right());
  const auto shifted =
      PlaceAnchoredPopup(client.Offset(17.5f, 8.25f),
                         anchor.Offset(17.5f, 8.25f), {320, 400}, true);
  KR_EXPECT_EQ(shifted, right.Offset(17.5f, 8.25f));
}

KR_TEST(SearchBoxReclampsStaleMeasuredHeightDuringShrinkAndTextScaling) {
  for (const Rect client : {Rect{0, 0, 1600, 900}, Rect{0, 0, 900, 500},
                            Rect{0, 0, 527, 210}, Rect{0, 0, 800, 360}}) {
    for (const float font : {14.0f, 28.0f, 42.0f}) {
      const auto bounds = ComputeSearchBoxBounds(client, 1200, font);
      const auto available = PopupViewport(client);
      KR_EXPECT(bounds.x >= available.x && bounds.y >= available.y);
      KR_EXPECT(bounds.right() <= available.right() &&
                bounds.bottom() <= available.bottom());
      KR_EXPECT_EQ(bounds.height, available.height);
    }
  }
}
}  // namespace
}  // namespace kr
