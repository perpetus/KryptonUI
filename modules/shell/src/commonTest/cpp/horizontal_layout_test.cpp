#include "krypton/ui/layout/horizontal_layout.hpp"

#include "krypton/testing/test.hpp"

namespace kr {
namespace {
ShellLayout Horizontal(TabLayout mode,
                       std::size_t count,
                       Flags<LayoutFlag> modifiers = {},
                       bool reveal = false,
                       Rect client = {17, 23, 1200, 800}) {
  return ComputeLayout({.client = client},
                       {.flags = WithTabLayout(modifiers, mode)}, false, reveal,
                       design::kTokens, {}, count);
}
bool Within(Rect outer, Rect inner) {
  return inner.empty() ||
         (inner.x >= outer.x && inner.y >= outer.y &&
          inner.right() <= outer.right() && inner.bottom() <= outer.bottom());
}
}  // namespace

KR_TEST(HorizontalModesOrderChromeAndDynamicCountsOnlyDisplayedTabs) {
  const auto classic = Horizontal(TabLayout::kClassic, 1);
  KR_EXPECT(classic.tab_strip_visible);
  KR_EXPECT_EQ(classic.tab_strip.bottom(), classic.navigation.y);
  const auto compact = Horizontal(TabLayout::kCompact, 5);
  KR_EXPECT_EQ(compact.tab_strip, compact.navigation);
  KR_EXPECT(compact.navigation.height >=
            design::kTokens.spacing.tab_height +
                2 * design::kTokens.spacing.tab_gap);
  KR_EXPECT(compact.toolbar.height < classic.toolbar.height);
  const auto single = Horizontal(TabLayout::kDynamic, 1);
  KR_EXPECT(!single.tab_strip_visible);
  KR_EXPECT_EQ(single.tab_strip.height, 0.0f);
  const auto multiple = Horizontal(TabLayout::kDynamic, 2);
  KR_EXPECT(multiple.tab_strip_visible);
  KR_EXPECT_EQ(multiple.navigation.bottom(), multiple.tab_strip.y);
  KR_EXPECT(multiple.card.y > single.card.y);
  KR_EXPECT(!classic.sidebar_visible && !compact.sidebar_visible);
}

KR_TEST(HorizontalOrientationPreservesVerticalAndUnrelatedPreferences) {
  const Flags<LayoutFlag> saved =
      LayoutFlag::kToolbarSingle | LayoutFlag::kSidebarRight |
      LayoutFlag::kCompactMode | LayoutFlag::kCenteredAddress;
  for (const auto mode :
       {TabLayout::kClassic, TabLayout::kCompact, TabLayout::kDynamic}) {
    const auto flags = WithTabLayout(saved, mode);
    KR_EXPECT(TabLayoutOf(flags) == mode);
    KR_EXPECT(WithTabLayout(flags, TabLayout::kVertical) == saved);
    KR_EXPECT(BrowserLayoutOf(flags) == ToolbarLayout::kSingle);
  }
}

KR_TEST(HorizontalCompactRevealsAndIndependentPinsLeavePageStationary) {
  const auto flags = LayoutFlag::kCompactMode |
                     LayoutFlag::kCompactTargetsExplicit |
                     LayoutFlag::kCompactHideToolbar;
  const auto hidden = Horizontal(TabLayout::kClassic, 3, flags);
  const auto revealed = Horizontal(TabLayout::kClassic, 3, flags, true);
  KR_EXPECT(!hidden.toolbar_visible && revealed.toolbar_overlays);
  KR_EXPECT_EQ(hidden.card, revealed.card);
  const auto pinned = Horizontal(
      TabLayout::kClassic, 3,
      LayoutFlag::kCompactMode | LayoutFlag::kCompactTargetsExplicit);
  KR_EXPECT(pinned.toolbar_visible && !pinned.toolbar_overlays);
  const auto vertical = ComputeLayout(
      {.client = {0, 0, 1200, 800}},
      {.flags = LayoutFlag::kCompactMode | LayoutFlag::kCompactTargetsExplicit},
      false);
  KR_EXPECT(vertical.toolbar_visible && vertical.sidebar_visible);
}

KR_TEST(HorizontalChromeClampsTinyClientsAndNativeCaptionInsets) {
  for (const auto mode :
       {TabLayout::kClassic, TabLayout::kCompact, TabLayout::kDynamic})
    for (const Rect client :
         {Rect{11, 19, 1, 1}, Rect{11, 19, 180, 30}, Rect{11, 19, 600, 400}}) {
      const auto layout = Horizontal(mode, 2, {}, false, client);
      KR_EXPECT(Within(client, layout.toolbar));
      KR_EXPECT(Within(client, layout.navigation));
      KR_EXPECT(Within(client, layout.tab_strip));
      KR_EXPECT(Within(client, layout.card));
    }
  const NativeTitleBarMetrics caption{
      .enabled = true, .height = 32, .left_inset = 20, .right_inset = 138};
  const auto layout = ComputeLayout({.client = {11, 19, 900, 600}},
                                    {.flags = LayoutFlag::kTabsClassic}, false,
                                    false, design::kTokens, caption, 2);
  KR_EXPECT_EQ(layout.toolbar.x, 31.0f);
  KR_EXPECT_EQ(layout.toolbar.right(), 773.0f);
  KR_EXPECT(layout.card.y >= 19 + caption.height);
}

KR_TEST(HorizontalTabsShareTheStripUpToTheStandardWidth) {
  const auto& h = design::kTokens.horizontal_tabs;
  const float margins = 2 * design::kTokens.spacing.tab_inset;
  const auto few = HorizontalTabWidthsFor(1200, 2, true);
  KR_EXPECT_EQ(few.active, h.tab_standard_width);
  KR_EXPECT_EQ(few.inactive, h.tab_standard_width);
  const auto many = HorizontalTabWidthsFor(1200, 10, true);
  KR_EXPECT_EQ(many.inactive, 1200.0f / 10 - margins);
  KR_EXPECT_EQ(many.active, many.inactive);
  KR_EXPECT_EQ(HorizontalTabWidthsFor(1200, 0, true).active, 0.0f);
  KR_EXPECT_EQ(HorizontalTabWidthsFor(0, 3, true).inactive, 0.0f);
}

KR_TEST(HorizontalActiveTabKeepsItsMinimumWhileOthersShrink) {
  const auto& h = design::kTokens.horizontal_tabs;
  const float margins = 2 * design::kTokens.spacing.tab_inset;
  const auto crowded = HorizontalTabWidthsFor(900, 30, true);
  KR_EXPECT_EQ(crowded.active, h.tab_minimum_active_width);
  KR_EXPECT_EQ(crowded.inactive,
               (900 - h.tab_minimum_active_width - margins) / 29 - margins);
  KR_EXPECT(crowded.inactive < crowded.active);
  const auto scrolled = HorizontalTabWidthsFor(400, 2000, true);
  KR_EXPECT_EQ(scrolled.active, h.tab_minimum_active_width);
  KR_EXPECT_EQ(scrolled.inactive, h.tab_minimum_inactive_width);
  KR_EXPECT_EQ(HorizontalTabWidthsFor(400, 2000, false).inactive,
               h.tab_minimum_inactive_width);
  const auto alone = HorizontalTabWidthsFor(20, 1, true);
  KR_EXPECT_EQ(alone.active, h.tab_minimum_active_width);
}

KR_TEST(NarrowHorizontalTabsDropContentInPriorityOrder) {
  const auto& h = design::kTokens.horizontal_tabs;
  const auto wide = HorizontalTabContent(h.tab_standard_width, false, true);
  KR_EXPECT(wide.icon && wide.audio && wide.close && !wide.centered);
  const auto active =
      HorizontalTabContent(h.tab_minimum_active_width, true, true);
  KR_EXPECT(active.close && !active.icon && !active.audio);
  const auto medium = HorizontalTabContent(60, false, false);
  KR_EXPECT(medium.icon && !medium.close && !medium.centered);
  const auto narrow =
      HorizontalTabContent(h.tab_minimum_inactive_width, false, true);
  KR_EXPECT(narrow.icon && narrow.centered && !narrow.audio && !narrow.close);
  KR_EXPECT(HorizontalTabContent(h.tab_minimum_inactive_width - 1, false,
                                 false) == TabRowContent{});
}

KR_TEST(HorizontalAddressAllocationHandlesLargeTranslatedContentAndSmallSlots) {
  const Rect right_slot{300, 0, 700, 40};
  const auto centered = CenteredAddressBounds(right_slot, 1200, 100);
  KR_EXPECT(Within(right_slot, centered));
  KR_EXPECT(centered.width <=
            design::kTokens.horizontal_tabs.centered_max_width);
  const Rect tiny{7, 0, 30, 40};
  KR_EXPECT(Within(tiny, CenteredAddressBounds(tiny, 1200, 200)));
  KR_EXPECT(CompactAddressWidth(80, 150, 200) <= 80);
  KR_EXPECT_EQ(CompactAddressWidth(1000, 100, 200),
               design::kTokens.horizontal_tabs.compact_address_width);
  const auto square =
      Horizontal(TabLayout::kDynamic, 1, LayoutFlag::kSquareFrame);
  KR_EXPECT_EQ(square.card_radius, 0.0f);
  KR_EXPECT_EQ(square.card.x, 17.0f);
}
}  // namespace kr
