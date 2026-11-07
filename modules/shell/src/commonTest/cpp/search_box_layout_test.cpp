#include "krypton/ui/layout/search_box_layout.hpp"

#include "krypton/testing/test.hpp"
#include "krypton/ui/layout/popup_layout.hpp"

namespace kr {
namespace {

KR_TEST(SearchResultsKeepZenHeightLimitWhenInputScales) {
  const auto& tokens = design::kTokens;
  const auto& search = tokens.overlay;
  for (const float input_height :
       {search.input_height, 3 * search.input_height}) {
    KR_EXPECT_EQ(ComputeSearchResultsMaxHeight({0, 0, 1280, 820}, input_height),
                 244.0f);
  }
}

KR_TEST(SearchResultsReserveScaledInputAndScrollInsideSmallWindows) {
  const auto& tokens = design::kTokens;
  const auto& search = tokens.overlay;
  const Rect client{17, 23, 500, 300};
  const float input_height = 2 * search.input_height;
  const float results_height =
      ComputeSearchResultsMaxHeight(client, input_height);
  KR_EXPECT(results_height > 0);
  KR_EXPECT(results_height < 10 * search.row_height);
  KR_EXPECT_EQ(results_height + input_height + search.results_bottom_margin +
                   2 * search.border_width,
               PopupViewport(client).height);
  const float total_height = results_height + input_height +
                             search.results_bottom_margin +
                             2 * search.border_width;
  const auto bounds =
      ComputeSearchBoxBounds(client, total_height, 2 * tokens.type.size);
  KR_EXPECT(bounds.y >= client.y);
  KR_EXPECT(bounds.bottom() <= client.bottom());
  KR_EXPECT_EQ(ComputeSearchResultsMaxHeight({0, 0, 100, 60}, input_height),
               0.0f);
  KR_EXPECT_EQ(ComputeSearchResultsMaxHeight({0, 0, 0, 0}, input_height), 0.0f);
}

KR_TEST(SearchBoxUsesReferenceWidthAndVerticalCenteringRules) {
  const auto& tokens = design::kTokens;
  const Rect client{0, 0, 1280, 820};
  const auto bounds = ComputeSearchBoxBounds(client, 200, tokens.type.size);
  const float preferred = std::min(client.width / tokens.overlay.width_divisor,
                                   tokens.overlay.width);
  const float minimum =
      std::min(client.width * tokens.overlay.minimum_width_fraction,
               tokens.type.size * tokens.overlay.minimum_width_rem);
  KR_EXPECT_EQ(bounds.width,
               std::min(tokens.overlay.width, std::max(preferred, minimum)));
  KR_EXPECT_EQ(bounds.x, (client.width - bounds.width) / 2);
  KR_EXPECT_EQ(bounds.y,
               (client.height - tokens.overlay.center_minimum_height) / 2);
}

KR_TEST(SearchBoxStaysInsideSmallAndOffsetClientsWithScaledText) {
  for (const Rect client :
       {Rect{0, 0, 270, 180}, Rect{17.5f, 8.25f, 800, 600}, Rect{0, 0, 8, 6}}) {
    for (const float root_font : {14.0f, 28.0f}) {
      for (const float content_height : {62.0f, 333.0f, 1000.0f}) {
        const auto bounds =
            ComputeSearchBoxBounds(client, content_height, root_font);
        KR_EXPECT(bounds.x >= client.x);
        KR_EXPECT(bounds.y >= client.y);
        KR_EXPECT(bounds.right() <= client.right());
        KR_EXPECT(bounds.bottom() <= client.bottom());
        KR_EXPECT(bounds.width >= 0);
        KR_EXPECT(bounds.height >= 0);
      }
    }
  }
}

KR_TEST(SearchBoxKeepsZenWidthCapAcrossTextScalingAndResultHeights) {
  for (const float window_width : {1280.0f, 1600.0f, 3840.0f}) {
    const Rect client{17, 23, window_width, 900};
    for (const float font_size : {14.0f, 28.0f, 42.0f}) {
      for (const float results_height : {62.0f, 333.0f, 1000.0f}) {
        const auto bounds =
            ComputeSearchBoxBounds(client, results_height, font_size);
        KR_EXPECT_EQ(bounds.width, 750.0f);
        KR_EXPECT_EQ(bounds.x, client.x + (client.width - 750.0f) / 2);
      }
    }
  }
}

KR_TEST(SearchBoxUsesNinetyPercentWidthInNarrowWindows) {
  const auto bounds = ComputeSearchBoxBounds({0, 0, 600, 900}, 333, 14);
  KR_EXPECT_EQ(bounds.width, 540.0f);
  KR_EXPECT_EQ(bounds.x, 30.0f);
}

KR_TEST(SearchBoxAllowsRelativeSizingWithinTheCapAndClampsEmptyClients) {
  const Rect client{0, 0, 1000, 900};
  const auto normal = ComputeSearchBoxBounds(client, 200, 10);
  const auto larger = ComputeSearchBoxBounds(client, 200, 20);
  KR_EXPECT(larger.width > normal.width);
  KR_EXPECT_EQ(larger.width, 750.0f);
  KR_EXPECT_EQ(ComputeSearchBoxBounds({10, 20, 0, 0}, 200, 14),
               (Rect{10, 20, 0, 0}));
}

}  // namespace
}  // namespace kr
