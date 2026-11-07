#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr {
namespace {
void Populate(Store& store) {
  for (std::uint64_t id = 1; id <= 5; ++id)
    KR_ASSERT(store
                  .InsertTab(store.tabs().size(),
                             {.id = TabId{id}, .space = SpaceId{3}})
                  .has_value());
}
SplitGroup StateGroup() {
  const std::array ids{TabId{1}, TabId{2}, TabId{3}};
  return *MakeSplit(SplitGroupId{7}, SpaceId{3}, ids, ids[0],
                    SplitLayout::kGrid);
}
KR_TEST(MultiPageStoreStaysLazyUntilFirstFeatureUpdate) {
  Store store;
  Populate(store);
  KR_EXPECT(store.multi_page() == nullptr);
  KR_EXPECT(store.SetGlance({}).has_value());
  KR_EXPECT(store.multi_page() == nullptr);
  KR_EXPECT(store.SetActiveTab(TabId{1}).has_value());
  KR_EXPECT(store.presented_tab() == TabId{1});
}
KR_TEST(MultiPageChooserAcceptsEverySnapPresetAndRejectsInvalidLayouts) {
  Store store;
  Populate(store);
  KR_ASSERT(store.SetActiveTab(TabId{1}).has_value());
  KR_ASSERT(store.OpenSplitChooser().has_value());
  for (std::uint32_t layout = 0;
       layout <= static_cast<std::uint32_t>(SplitLayout::kMainBottom);
       ++layout) {
    const auto preset = static_cast<SplitLayout>(layout);
    store.SetSplitChooserLayout(preset);
    KR_EXPECT(store.ui().split_chooser.layout == preset);
  }
  store.SetSplitChooserLayout(static_cast<SplitLayout>(100));
  KR_EXPECT(store.ui().split_chooser.layout == SplitLayout::kMainBottom);
  KR_EXPECT(store.multi_page() == nullptr);
}

KR_TEST(SplitOptionsUseCurrentGroupMembersAndPreserveNormalChooserSemantics) {
  Store store;
  Populate(store);
  KR_ASSERT(store.SetActiveTab(TabId{1}).has_value());
  KR_ASSERT(store.UpsertSplitGroup(StateGroup()).has_value());
  const Rect anchor{100, 80, 20, 20};
  KR_ASSERT(store.OpenSplitOptions(TabId{2}, anchor).has_value());
  const auto& options = store.ui().split_chooser;
  KR_EXPECT(options.open && options.layout_only);
  KR_EXPECT(options.target == TabId{2});
  KR_EXPECT(options.anchor == anchor);
  KR_EXPECT_EQ(options.count, 3u);
  KR_EXPECT(options.selected == StateGroup().members);
  KR_EXPECT(!store.OpenSplitOptions(TabId{5}, anchor));
  KR_EXPECT(!store.OpenSplitOptions(TabId{2}, {0, 0, -1, 20}));
  KR_EXPECT(!store.OpenSplitOptions(
      TabId{2}, {std::numeric_limits<float>::quiet_NaN(), 0, 20, 20}));
  KR_EXPECT(store.ui().split_chooser.target == TabId{2});
  KR_ASSERT(store.OpenSplitChooser().has_value());
  KR_EXPECT(!store.ui().split_chooser.layout_only);
  KR_EXPECT(store.ui().split_chooser.target == TabId{});
}
KR_TEST(MultiPageStoreUpdatesOnlyMemberMetadataOnFocusAndResize) {
  Store store;
  Populate(store);
  auto group = StateGroup();
  KR_ASSERT(store.UpsertSplitGroup(group).has_value());
  (void)store.TakeDirty();
  group.active = TabId{2};
  ++group.revision;
  KR_ASSERT(store.UpsertSplitGroup(group).has_value());
  KR_EXPECT(!store.dirty().Has(Dirty::kTabs));
  KR_EXPECT(!store.dirty().Has(Dirty::kLibrary));
  KR_EXPECT(store.SplitForTab(TabId{3})->active == TabId{2});
  (void)store.TakeDirty();
  group.weights[0] = 0.7f;
  ++group.revision;
  KR_ASSERT(store.UpsertSplitGroup(group).has_value());
  KR_EXPECT(!store.dirty().Has(Dirty::kTabs));
  KR_EXPECT(store.dirty().Has(Dirty::kMultiPage));
  KR_EXPECT(!store.dirty().Has(Dirty::kActiveTab));
  KR_EXPECT(!store.UpsertSplitGroup(StateGroup()));
}
KR_TEST(MultiPageStoreRejectsOverlappingGroupsAndRecoversRemovedTabs) {
  Store store;
  Populate(store);
  auto group = StateGroup();
  KR_ASSERT(store.UpsertSplitGroup(group).has_value());
  group.id = SplitGroupId{8};
  KR_EXPECT(!store.UpsertSplitGroup(group));
  KR_EXPECT(!store.SplitForTab(TabId{4}));
  KR_ASSERT(store.RemoveTab(TabId{2}).has_value());
  KR_EXPECT(store.SplitForTab(TabId{1})->count == 2);
  KR_EXPECT(!store.SplitForTab(TabId{2}));
  KR_ASSERT(store.RemoveTab(TabId{1}).has_value());
  KR_EXPECT(!store.SplitForTab(TabId{3}));
  KR_EXPECT(store.FindTab(TabId{3}) != nullptr);
}
KR_TEST(MultiPageStoreGlanceMapsSelectionAndRejectsStaleGeneration) {
  Store store;
  Populate(store);
  KR_ASSERT(store.SetGlance({{TabId{1}, TabId{5}, 3}, GlancePhase::kOpen, {}})
                .has_value());
  KR_ASSERT(store.SetActiveTab(TabId{5}).has_value());
  KR_EXPECT(store.presented_tab() == TabId{1});
  KR_EXPECT(store.IsTransientTab(TabId{5}));
  KR_EXPECT(
      !store.SetGlance({{TabId{2}, TabId{4}, 4}, GlancePhase::kOpen, {}}));
  KR_ASSERT(
      store.SetGlance({{{}, {}, 3}, GlancePhase::kClosed, {}}).has_value());
  KR_EXPECT(!store.IsTransientTab(TabId{5}));
  KR_EXPECT(
      !store.SetGlance({{TabId{1}, TabId{5}, 3}, GlancePhase::kOpen, {}}));
}
}  // namespace
}  // namespace kr
