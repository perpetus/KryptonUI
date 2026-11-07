#include "krypton/ui/model/multi_page.hpp"

#include <cmath>
#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/presentation/multi_page.hpp"

namespace kr {
namespace {
SplitGroup Group(std::size_t count = 4,
                 SplitLayout layout = SplitLayout::kGrid) {
  const std::array ids{TabId{11}, TabId{12}, TabId{13}, TabId{14}};
  return *MakeSplit(SplitGroupId{7}, SpaceId{3}, std::span(ids).first(count),
                    ids[0], layout);
}

bool Near(float a, float b) {
  return std::abs(a - b) < 0.01f;
}

KR_TEST(SplitLayoutsPartitionEverySupportedMemberCount) {
  for (const auto& choice : kSplitLayoutChoices) {
    const auto layout = choice.layout;
    for (std::size_t count = 2; count <= 4; ++count) {
      const auto group = Group(count, layout);
      for (const bool rtl : {false, true}) {
        const auto geometry = LayoutSplit(group, {17, 23, 1200, 800}, 4, rtl);
        KR_ASSERT(geometry.has_value());
        KR_EXPECT(geometry->pane_count == count);
        KR_EXPECT(geometry->divider_count == count - 1);
        float area = 0;
        for (std::size_t i = 0; i < count; ++i) {
          const auto pane = geometry->panes[i];
          KR_EXPECT(pane.x >= 17 && pane.y >= 23);
          KR_EXPECT(pane.right() <= 1217.01f && pane.bottom() <= 823.01f);
          KR_EXPECT(pane.width > 0 && pane.height > 0);
          area += pane.width * pane.height;
          for (std::size_t j = 0; j < i; ++j) {
            const auto other = geometry->panes[j];
            KR_EXPECT(pane.right() <= other.x + 0.01f ||
                      other.right() <= pane.x + 0.01f ||
                      pane.bottom() <= other.y + 0.01f ||
                      other.bottom() <= pane.y + 0.01f);
          }
        }
        for (std::size_t i = 0; i < count - 1; ++i) {
          area += geometry->dividers[i].bounds.width *
                  geometry->dividers[i].bounds.height;
          KR_EXPECT(geometry->dividers[i].index == i);
        }
        KR_EXPECT(std::abs(area - 1200 * 800) < 1);
      }
    }
  }
}

KR_TEST(SplitGridUsesSpecifiedTwoThreeAndFourPaneTopologies) {
  const auto pair = *LayoutSplit(Group(2), {0, 0, 1000, 800}, 0);
  KR_EXPECT(pair.panes[0] == (Rect{0, 0, 500, 800}));
  KR_EXPECT(pair.panes[1] == (Rect{500, 0, 500, 800}));
  const auto three = *LayoutSplit(Group(3), {0, 0, 1000, 800}, 0);
  KR_EXPECT(three.panes[0] == (Rect{0, 0, 500, 400}));
  KR_EXPECT(three.panes[1] == (Rect{0, 400, 500, 400}));
  KR_EXPECT(three.panes[2] == (Rect{500, 0, 500, 800}));
  const auto four = *LayoutSplit(Group(4), {0, 0, 1000, 800}, 0);
  KR_EXPECT(four.panes[3] == (Rect{500, 400, 500, 400}));
  const auto mirrored = *LayoutSplit(Group(3), {17, 0, 1000, 800}, 0, true);
  KR_EXPECT(mirrored.panes[2] == (Rect{17, 0, 500, 800}));
}
KR_TEST(SnapLayoutsPlaceMainPaneOnEachRequestedSide) {
  for (std::size_t count = 2; count <= 4; ++count) {
    for (auto layout : {SplitLayout::kMainLeft, SplitLayout::kMainRight,
                        SplitLayout::kMainTop, SplitLayout::kMainBottom}) {
      auto group = Group(count, layout);
      const auto result = *LayoutSplit(group, {0, 0, 1200, 800}, 0);
      const bool horizontal =
          layout == SplitLayout::kMainTop || layout == SplitLayout::kMainBottom;
      const bool trailing = layout == SplitLayout::kMainRight ||
                            layout == SplitLayout::kMainBottom;
      const Rect main = result.panes[0];
      KR_EXPECT(main.width == (horizontal ? 1200 : 600));
      KR_EXPECT(main.height == (horizontal ? 400 : 800));
      KR_EXPECT(main.x == (!horizontal && trailing ? 600 : 0));
      KR_EXPECT(main.y == (horizontal && trailing ? 400 : 0));
      for (std::size_t i = 1; i < count; ++i) {
        const Rect pane = result.panes[i];
        KR_EXPECT(Near(horizontal ? pane.width : pane.height,
                       (horizontal ? 1200.0f : 800.0f) / (count - 1)));
      }
      const auto resized = ReduceSplit(group, {.kind = SplitEditKind::kResize,
                                               .revision = group.revision,
                                               .index = 0,
                                               .weight = 0.7f});
      KR_ASSERT(resized && *resized);
      KR_EXPECT((**resized).members == group.members);
      KR_EXPECT((**resized).layout == layout);
      KR_EXPECT(LayoutSplit(**resized, {0, 0, 1200, 800}, 4).has_value());
    }
  }
}
KR_TEST(SnapWeightedColumnsAndPreviewUseTheLiveTopology) {
  for (const auto& choice : kSplitLayoutChoices) {
    const auto group = Group(choice.preview_members, choice.layout);
    for (const bool rtl : {false, true}) {
      const auto preview =
          PresentSplitLayoutPreview(choice, {0, 0, 1200, 800}, 4, rtl);
      const auto live = *LayoutSplit(group, {0, 0, 1200, 800}, 4, rtl);
      KR_EXPECT(preview.pane_count == live.pane_count);
      KR_EXPECT(preview.panes == live.panes);
    }
  }
  const auto left =
      *LayoutSplit(Group(2, SplitLayout::kWideLeft), {0, 0, 900, 600}, 0);
  KR_EXPECT(Near(left.panes[0].width, 600));
  const auto right =
      *LayoutSplit(Group(2, SplitLayout::kWideRight), {0, 0, 900, 600}, 0);
  KR_EXPECT(Near(right.panes[1].width, 600));
  const auto center =
      *LayoutSplit(Group(3, SplitLayout::kWideCenter), {0, 0, 1200, 800}, 0);
  KR_EXPECT(Near(center.panes[0].width, 300));
  KR_EXPECT(Near(center.panes[1].width, 600));
  KR_EXPECT(Near(center.panes[2].width, 300));
}
KR_TEST(SnapLayoutsRemainValidAfterInsertionDetachmentAndReordering) {
  for (const auto& choice : kSplitLayoutChoices) {
    auto group = Group(3, choice.layout);
    const auto inserted = ReduceSplit(group, {.kind = SplitEditKind::kInsert,
                                              .revision = group.revision,
                                              .tab = TabId{99},
                                              .space = group.space,
                                              .index = 1});
    KR_ASSERT(inserted && *inserted);
    group = **inserted;
    const auto detached = ReduceSplit(group, {.kind = SplitEditKind::kDetach,
                                              .revision = group.revision,
                                              .tab = TabId{11}});
    KR_ASSERT(detached && *detached);
    group = **detached;
    KR_EXPECT(group.layout == choice.layout);
    KR_EXPECT(group.count == 3 && group.members[0] == TabId{99});
    KR_EXPECT(IsValidSplit(group));
    KR_EXPECT(LayoutSplit(group, {0, 0, 900, 600}, 4).has_value());
  }
}

KR_TEST(SplitLinearLayoutsStartWithEqualSizedPanes) {
  for (auto layout : {SplitLayout::kColumns, SplitLayout::kRows}) {
    const auto geometry = *LayoutSplit(Group(3, layout), {0, 0, 900, 900}, 0);
    for (const auto pane : std::span(geometry.panes).first(3))
      KR_EXPECT(Near(layout == SplitLayout::kColumns ? pane.width : pane.height,
                     300));
  }
}

KR_TEST(SplitWorkspaceMovesPreserveMembersFocusLayoutAndWeights) {
  auto group = Group();
  group.weights = {0.3f, 0.4f, 0.7f};
  const auto moved = ReduceSplit(group, {.kind = SplitEditKind::kWorkspace,
                                         .revision = group.revision,
                                         .space = SpaceId{9}});
  KR_ASSERT(moved && *moved);
  KR_EXPECT((**moved).space == SpaceId{9});
  KR_EXPECT((**moved).members == group.members);
  KR_EXPECT((**moved).active == group.active);
  KR_EXPECT((**moved).layout == group.layout);
  KR_EXPECT((**moved).weights == group.weights);
  KR_EXPECT((**moved).revision == group.revision + 1);
  KR_EXPECT(!ReduceSplit(
      group, {.kind = SplitEditKind::kWorkspace, .revision = group.revision}));
  KR_EXPECT(!ReduceSplit(**moved, {.kind = SplitEditKind::kWorkspace,
                                   .revision = group.revision,
                                   .space = SpaceId{10}}));
}

KR_TEST(SplitGeometryRejectsNonfiniteInputsAndHandlesTinyWindows) {
  const auto group = Group();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  KR_EXPECT(!LayoutSplit(group, {nan, 0, 100, 100}, 4));
  KR_EXPECT(!LayoutSplit(group, {0, 0, -1, 100}, 4));
  KR_EXPECT(!LayoutSplit(group, {0, 0, 100, 100}, nan));
  KR_EXPECT(!LayoutSplit(group, {0, 0, 100, 100}, -1));
  for (float size : {0.0f, 1.0f, 3.0f}) {
    const auto result = LayoutSplit(group, {0, 0, size, size}, 4);
    KR_ASSERT(result.has_value());
    for (const auto pane : result->panes)
      KR_EXPECT(pane.width >= 0 && pane.height >= 0);
  }
}

KR_TEST(SplitEditsPreserveIdentityOrderAndRejectStaleGestures) {
  auto group = Group(3);
  const auto moved = ReduceSplit(group, {.kind = SplitEditKind::kMove,
                                         .revision = group.revision,
                                         .tab = TabId{11},
                                         .index = 2});
  KR_ASSERT(moved && *moved);
  group = **moved;
  KR_EXPECT(group.members[0] == TabId{12});
  KR_EXPECT(group.members[2] == TabId{11});
  KR_EXPECT(group.id == SplitGroupId{7});
  KR_EXPECT(group.active == TabId{11});
  KR_EXPECT(!ReduceSplit(group, {.revision = 1, .tab = TabId{12}}));
  KR_EXPECT(
      !ReduceSplit(group, {.revision = group.revision, .tab = TabId{99}}));
  const auto focused =
      ReduceSplit(group, {.revision = group.revision, .tab = TabId{12}});
  KR_ASSERT(focused && *focused);
  KR_EXPECT((**focused).active == TabId{12});
  KR_EXPECT((**focused).members == group.members);
}

KR_TEST(SplitInsertDetachAndUnsplitNeverDiscardMemberTabs) {
  auto group = Group(3);
  KR_EXPECT(!ReduceSplit(group, {.kind = SplitEditKind::kInsert,
                                 .revision = 1,
                                 .tab = TabId{14},
                                 .space = SpaceId{4},
                                 .index = 1}));
  KR_EXPECT(!ReduceSplit(group, {.kind = SplitEditKind::kInsert,
                                 .revision = 1,
                                 .tab = TabId{11},
                                 .space = SpaceId{3},
                                 .index = 1}));
  auto result = ReduceSplit(group, {.kind = SplitEditKind::kInsert,
                                    .revision = 1,
                                    .tab = TabId{14},
                                    .space = SpaceId{3},
                                    .index = 1});
  KR_ASSERT(result && *result);
  group = **result;
  KR_EXPECT(group.members ==
            (std::array{TabId{11}, TabId{14}, TabId{12}, TabId{13}}));
  KR_EXPECT(!ReduceSplit(group, {.kind = SplitEditKind::kInsert,
                                 .revision = group.revision,
                                 .tab = TabId{15},
                                 .space = SpaceId{3}}));
  while (group.count > 2) {
    result = ReduceSplit(group, {.kind = SplitEditKind::kDetach,
                                 .revision = group.revision,
                                 .tab = group.active});
    KR_ASSERT(result && *result);
    group = **result;
    KR_EXPECT(IsValidSplit(group));
  }
  result = ReduceSplit(group, {.kind = SplitEditKind::kDetach,
                               .revision = group.revision,
                               .tab = group.active});
  KR_ASSERT(result.has_value());
  KR_EXPECT(!*result);
  result =
      ReduceSplit(Group(), {.kind = SplitEditKind::kUnsplit, .revision = 1});
  KR_ASSERT(result.has_value());
  KR_EXPECT(!*result);
}

KR_TEST(SplitResizeClampsWeightsAndRejectsNonfiniteAndUnusedDividers) {
  auto group = Group(2);
  const auto low = ReduceSplit(group, {.kind = SplitEditKind::kResize,
                                       .revision = 1,
                                       .index = 0,
                                       .weight = -10});
  KR_ASSERT(low && *low);
  KR_EXPECT(Near((**low).weights[0], 0.07f));
  const auto high = ReduceSplit(group, {.kind = SplitEditKind::kResize,
                                        .revision = 1,
                                        .index = 0,
                                        .weight = 10});
  KR_ASSERT(high && *high);
  KR_EXPECT(Near((**high).weights[0], 0.93f));
  KR_EXPECT(!ReduceSplit(group, {.kind = SplitEditKind::kResize,
                                 .revision = 1,
                                 .index = 1,
                                 .weight = 0.5f}));
  KR_EXPECT(
      !ReduceSplit(group, {.kind = SplitEditKind::kResize,
                           .revision = 1,
                           .weight = std::numeric_limits<float>::infinity()}));
}

KR_TEST(SplitSavedTabRulesRetainOriginalsAndUseHomogeneousPinning) {
  std::array tabs{Tab{.id = TabId{1}, .space = SpaceId{3}},
                  Tab{.id = TabId{2}, .space = SpaceId{3}}};
  auto plan = PlanSplitCopies(tabs, SpaceId{3});
  KR_ASSERT(plan.has_value());
  KR_EXPECT(!plan->pinned && !plan->copy[0] && !plan->copy[1]);
  tabs[0].flags.Set(TabFlag::kPinned);
  plan = PlanSplitCopies(tabs, SpaceId{3});
  KR_ASSERT(plan.has_value());
  KR_EXPECT(!plan->pinned && plan->copy[0] && !plan->copy[1]);
  tabs[1].flags.Set(TabFlag::kPinned);
  plan = PlanSplitCopies(tabs, SpaceId{3});
  KR_ASSERT(plan.has_value());
  KR_EXPECT(plan->pinned && !plan->copy[0] && !plan->copy[1]);
  tabs[0].flags.Set(TabFlag::kFavorite);
  plan = PlanSplitCopies(tabs, SpaceId{3});
  KR_ASSERT(plan.has_value());
  KR_EXPECT(!plan->pinned && plan->copy[0] && plan->copy[1]);
}

KR_TEST(SplitRestoreRecoversOnlyCompleteValidGroups) {
  auto group = Group(2);
  std::array tabs{Tab{.id = TabId{11}, .space = SpaceId{3}},
                  Tab{.id = TabId{12}, .space = SpaceId{3}}};
  KR_EXPECT(CanRestoreSplit(group, tabs));
  KR_EXPECT(!CanRestoreSplit(group, std::span(tabs).first(1)));
  tabs[1].space = SpaceId{4};
  KR_EXPECT(!CanRestoreSplit(group, tabs));
  tabs[1].space = SpaceId{3};
  tabs[1].flags.Set(TabFlag::kFavorite);
  KR_EXPECT(!CanRestoreSplit(group, tabs));
  group.members[1] = group.members[0];
  KR_EXPECT(!CanRestoreSplit(group, tabs));
}

KR_TEST(GlanceUnloadCancellationRetainsChildAndCancelsDeparture) {
  const GlanceIdentity identity{TabId{1}, TabId{2}, 9};
  auto open = ReduceGlance({}, {.identity = identity});
  KR_ASSERT(open.has_value());
  KR_EXPECT(PresentedTab(TabId{2}, *open) == TabId{1});
  auto closing = ReduceGlance(*open, {.kind = GlanceEditKind::kRequestClose,
                                      .identity = identity,
                                      .departure = TabId{3}});
  KR_ASSERT(closing.has_value());
  KR_EXPECT(closing->departure == TabId{3});
  const auto cancelled = ReduceGlance(
      *closing, {.kind = GlanceEditKind::kCancelClose, .identity = identity});
  KR_ASSERT(cancelled.has_value());
  KR_EXPECT(cancelled->identity == identity);
  KR_EXPECT(cancelled->phase == GlancePhase::kOpen && !cancelled->departure);
  const auto closed = ReduceGlance(
      *closing, {.kind = GlanceEditKind::kClosed, .identity = identity});
  KR_ASSERT(closed.has_value());
  KR_EXPECT(closed->phase == GlancePhase::kClosed);
  KR_EXPECT(!ReduceGlance(*closed, {.identity = identity}));
  KR_EXPECT(!ReduceGlance(
      *closed, {.kind = GlanceEditKind::kExpand, .identity = identity}));
}

KR_TEST(GlanceReusePromotionCapacityAndStaleLifetimes) {
  const GlanceIdentity identity{TabId{1}, TabId{2}, 9};
  const auto open = *ReduceGlance({}, {.identity = identity});
  KR_EXPECT(!ReduceGlance(open, {.identity = {TabId{2}, TabId{3}, 10}}));
  const auto reused = ReduceGlance(
      open, {.kind = GlanceEditKind::kReuse, .identity = identity});
  KR_ASSERT(reused.has_value());
  KR_EXPECT(*reused == open);
  KR_EXPECT(!ReduceGlance(open, {.kind = GlanceEditKind::kExpand,
                                 .identity = {TabId{1}, TabId{2}, 8}}));
  KR_EXPECT(!ReduceGlance(open, {.kind = GlanceEditKind::kSplit,
                                 .identity = identity,
                                 .parent_members = 4}));
  const auto split = ReduceGlance(open, {.kind = GlanceEditKind::kSplit,
                                         .identity = identity,
                                         .parent_members = 3});
  KR_ASSERT(split.has_value());
  KR_EXPECT(split->phase == GlancePhase::kClosed);
  const auto expanded = ReduceGlance(
      open, {.kind = GlanceEditKind::kExpand, .identity = identity});
  KR_ASSERT(expanded.has_value());
  KR_EXPECT(expanded->phase == GlancePhase::kClosed);
  KR_EXPECT(ReduceGlance(*expanded, {.identity = {TabId{1}, TabId{4}, 10}})
                .has_value());
}
KR_TEST(GlanceCardUsesTheParentPaneAndMirrorsControls) {
  const GlanceLayoutSpec spec{0.8f, 15, 12, 34, 12};
  const Rect parent{100, 50, 1000, 600};
  const auto right = LayoutGlance(parent, spec, false);
  const auto left = LayoutGlance(parent, spec, true);
  KR_ASSERT(right && left);
  KR_EXPECT(Near(right->page.x, 200) && Near(right->page.width, 800));
  KR_EXPECT(right->page.y == 50 && right->page.height == 600);
  KR_EXPECT(right->toolbar.x == right->page.right());
  KR_EXPECT(left->toolbar.right() == left->page.x);
  KR_EXPECT(right->toolbar.y == 65 && right->toolbar.height == 150);
  KR_EXPECT(left->page == right->page);
  const auto narrow = LayoutGlance({20, 30, 40, 50}, spec, false);
  KR_ASSERT(narrow);
  KR_EXPECT(narrow->toolbar.x == 20 && narrow->toolbar.width == 40);
  KR_EXPECT(narrow->toolbar.y == 30 && narrow->toolbar.height == 50);
  KR_EXPECT(LayoutGlance({}, spec, false).has_value());
  auto invalid = spec;
  invalid.width_ratio = std::numeric_limits<float>::quiet_NaN();
  KR_EXPECT(!LayoutGlance(parent, invalid, false));
  invalid = spec;
  invalid.button_gap = -1;
  KR_EXPECT(!LayoutGlance(parent, invalid, false));
  invalid = spec;
  invalid.width_ratio = 1.1f;
  KR_EXPECT(!LayoutGlance(parent, invalid, false));
}

KR_TEST(GlanceButtonsFollowThemeAndContrastPreferences) {
  ThemeVm theme{.accent = Color{0x803B82F6u}};
  const auto light = GlanceButtonColors(theme);
  KR_EXPECT(light.button_icon == Color{0xFF3B82F6u});
  KR_EXPECT(light.button_hover == Color{0xFF292929u});
  KR_EXPECT(light.shadow.a() == 61 && light.button_shadow.a() == 18);
  theme.dark = true;
  theme.palette = &design::kTokens.dark;
  const auto dark = GlanceButtonColors(theme);
  KR_EXPECT(dark.button_hover == Color{0xFFCCCCCCu});
  KR_EXPECT(dark.button.r() > light.button.r());
  theme.accent = {};
  KR_EXPECT(GlanceButtonColors(theme).button_icon ==
            Color{theme.palette->text});
  theme.high_contrast = true;
  const auto contrast = GlanceButtonColors(theme);
  KR_EXPECT(contrast.button_icon == Color{theme.palette->text});
  KR_EXPECT(contrast.shadow.a() == 0 && contrast.button_shadow.a() == 0);
}

KR_TEST(SplitOutlineFollowsTheAccentAndDarkensInLightSchemes) {
  ThemeVm theme{.accent = Color{0x803B82F6u}};
  theme.dark = true;
  KR_EXPECT(SplitOutlineColor(theme) == Color{0xFF3B82F6u});
  // #3B82F6 is hsl(217 91% 60%); the light outline is hsl(217 91% 40%).
  theme.dark = false;
  KR_EXPECT(SplitOutlineColor(theme) == Color{0xFF094FC2u});
  // Grey keeps zero saturation; lightness stops at black.
  theme.accent = Color{0xFF202020u};
  KR_EXPECT(SplitOutlineColor(theme) == Color{0xFF000000u});
  theme.accent = Color{};
  KR_EXPECT(SplitOutlineColor(theme).a() == 0);
}
}  // namespace
}  // namespace kr
