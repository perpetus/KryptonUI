#include "krypton/ui/state/store.hpp"

#include "krypton/testing/test.hpp"

namespace kr {
namespace {

Tab MakeTab(std::uint64_t id, std::string title = "tab") {
  return {.id = TabId{id}, .space = SpaceId{1}, .title = std::move(title)};
}

std::vector<std::uint64_t> Order(const Store& store) {
  std::vector<std::uint64_t> ids;
  for (const Tab& tab : store.tabs())
    ids.push_back(tab.id.value);
  return ids;
}

KR_TEST(StoreInsertsInOrderAndRejectsBadInput) {
  Store store;
  KR_EXPECT(store.InsertTab(0, MakeTab(1)).has_value());
  KR_EXPECT(store.InsertTab(1, MakeTab(2)).has_value());
  KR_EXPECT(store.InsertTab(0, MakeTab(3)).has_value());
  KR_EXPECT(Order(store) == (std::vector<std::uint64_t>{3, 1, 2}));

  KR_EXPECT(!store.InsertTab(0, MakeTab(1)).has_value());  // duplicate
  KR_EXPECT(!store.InsertTab(9, MakeTab(4)).has_value());  // past the end
  KR_EXPECT(!store.InsertTab(0, MakeTab(0)).has_value());  // id 0 reserved
  KR_EXPECT_EQ(store.tabs().size(), 3u);
}

KR_TEST(StoreMovesBothDirectionsAndKeepsIndexConsistent) {
  Store store;
  for (std::uint64_t id = 1; id <= 5; ++id) {
    KR_ASSERT(store.InsertTab(id - 1, MakeTab(id)).has_value());
  }
  KR_ASSERT(store.MoveTab(TabId{1}, 3).has_value());
  KR_EXPECT(Order(store) == (std::vector<std::uint64_t>{2, 3, 4, 1, 5}));
  KR_ASSERT(store.MoveTab(TabId{5}, 0).has_value());
  KR_EXPECT(Order(store) == (std::vector<std::uint64_t>{5, 2, 3, 4, 1}));

  for (std::size_t i = 0; i < store.tabs().size(); ++i) {
    KR_EXPECT_EQ(store.IndexOf(store.tabs()[i].id).value_or(99), i);
  }
  KR_EXPECT(!store.MoveTab(TabId{42}, 0).has_value());
  KR_EXPECT(!store.MoveTab(TabId{1}, 5).has_value());
}

KR_TEST(StoreRemovingActiveTabClearsActive) {
  Store store;
  KR_ASSERT(store.InsertTab(0, MakeTab(1)).has_value());
  KR_ASSERT(store.InsertTab(1, MakeTab(2)).has_value());
  KR_ASSERT(store.SetActiveTab(TabId{2}).has_value());
  KR_ASSERT(store.RemoveTab(TabId{2}).has_value());
  KR_EXPECT(!store.active_tab());
  KR_EXPECT_EQ(store.FindTab(TabId{1})->title, std::string("tab"));
  KR_EXPECT(store.FindTab(TabId{2}) == nullptr);
  KR_EXPECT(!store.SetActiveTab(TabId{2}).has_value());
}

KR_TEST(StoreTracksAndResetsDirtyState) {
  Store store;
  (void)store.TakeDirty();  // starts all-dirty
  KR_EXPECT(store.dirty().none());
  store.SetNav({.display_text = "example.com"});
  KR_EXPECT(store.dirty().Has(Dirty::kNav));
  KR_EXPECT(!store.dirty().Has(Dirty::kTabs));
  const Flags<Dirty> taken = store.TakeDirty();
  KR_EXPECT(taken.Has(Dirty::kNav));
  KR_EXPECT(store.dirty().none());
}

KR_TEST(StoreTracksSidebarAndToolbarRevealsWithoutDirtyingUnchangedState) {
  Store store;
  (void)store.TakeDirty();
  store.SetToolbarRevealed(true);
  KR_EXPECT(store.ui().toolbar_revealed);
  KR_EXPECT(!store.ui().sidebar_revealed);
  KR_EXPECT(store.TakeDirty() == Flags<Dirty>{Dirty::kLayout});
  store.SetToolbarRevealed(true);
  KR_EXPECT(store.dirty().none());
  store.SetSidebarRevealed(true);
  KR_EXPECT(store.ui().sidebar_revealed);
  KR_EXPECT(store.TakeDirty() == Flags<Dirty>{Dirty::kLayout});

  store.SetLayoutPrefs({.flags = LayoutFlag::kToolbarCollapsed});
  KR_EXPECT(!store.ui().sidebar_revealed);
  KR_EXPECT(!store.ui().toolbar_revealed);
  KR_EXPECT(store.TakeDirty() == Flags<Dirty>{Dirty::kLayout});
}

KR_TEST(StoreIgnoresStaleSuggestionsAndPrefersDefault) {
  Store store;
  store.OpenCommandBar("", false);
  const std::uint32_t first = store.SetCommandText("ex");
  const std::uint32_t second = store.SetCommandText("exa");
  KR_EXPECT(first != second);

  store.SetSuggestions(first, {{.contents = "stale"}});
  KR_EXPECT(store.ui().command_bar.suggestions.empty());

  store.SetSuggestions(
      second, {{.contents = "a"}, {.is_default = true, .contents = "b"}});
  KR_EXPECT_EQ(store.ui().command_bar.suggestions.size(), 2u);
  KR_EXPECT_EQ(store.ui().command_bar.selected, 1);

  store.MoveCommandSelection(1);
  KR_EXPECT_EQ(store.ui().command_bar.selected, 0);  // wraps
  store.MoveCommandSelection(-1);
  KR_EXPECT_EQ(store.ui().command_bar.selected, 1);
}

KR_TEST(StoreClosedCommandBarIgnoresSuggestions) {
  Store store;
  store.OpenCommandBar("", false);
  const std::uint32_t id = store.SetCommandText("x");
  store.CloseCommandBar();
  store.SetSuggestions(id, {{.contents = "late"}});
  KR_EXPECT(store.ui().command_bar.suggestions.empty());
}

KR_TEST(StoreNewCommandTextInvalidatesPreviousSuggestions) {
  Store store;
  store.OpenCommandBar("", false);
  const std::uint32_t first = store.SetCommandText("first");
  store.SetSuggestions(first, {{.contents = "old result"},
                               {.is_default = true, .contents = "selected"}});
  KR_ASSERT(store.ui().command_bar.selected == 1);

  const std::uint32_t second = store.SetCommandText("second");
  KR_EXPECT(second != first);
  KR_EXPECT(store.ui().command_bar.suggestions.empty());
  KR_EXPECT_EQ(store.ui().command_bar.selected, 0);
  store.SetSuggestions(first, {{.contents = "late old result"}});
  KR_EXPECT(store.ui().command_bar.suggestions.empty());
}

KR_TEST(StoreCommandBarIgnoresSuggestionsWithoutAQuery) {
  Store store;
  store.OpenCommandBar("example.com", true);
  store.SetSuggestions(0, {{.contents = "unsolicited result"}});
  KR_EXPECT(store.ui().command_bar.suggestions.empty());
}

KR_TEST(ImageStoreCopiesRowsAndReportsDrops) {
  Store store;
  // 2x2 image with 4 bytes of padding per row.
  const std::uint8_t pixels[] = {1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0,
                                 3, 3, 3, 3, 4, 4, 4, 4, 0, 0, 0, 0};
  KR_ASSERT(store.PutImage(ImageId{7}, 2, 2, 12, pixels));
  const Image* image = store.images().Find(ImageId{7});
  KR_ASSERT(image != nullptr);
  KR_EXPECT_EQ(image->pixels.size(), 16u);
  KR_EXPECT_EQ(image->pixels[8], std::uint8_t{3});
  KR_EXPECT_EQ(image->generation, 1u);

  KR_EXPECT(!store.PutImage(ImageId{8}, 2, 2, 4, pixels));  // stride too small
  store.DropImage(ImageId{7});
  KR_EXPECT(store.images().Find(ImageId{7}) == nullptr);
  KR_EXPECT_EQ(store.images().dropped().size(), 1u);
}

KR_TEST(ImageStoreInvalidatesCachedPixelsWhenADroppedIdIsReused) {
  Store store;
  const std::uint8_t first[] = {1, 2, 3, 4};
  const std::uint8_t second[] = {5, 6, 7, 8};
  KR_ASSERT(store.PutImage(ImageId{7}, 1, 1, 4, first));
  const auto generation = store.images().Find(ImageId{7})->generation;
  store.DropImage(ImageId{7});
  KR_ASSERT(store.PutImage(ImageId{7}, 1, 1, 4, second));
  const Image* image = store.images().Find(ImageId{7});
  KR_ASSERT(image != nullptr);
  KR_EXPECT(image->generation != generation);
  KR_EXPECT_EQ(image->pixels[0], std::uint8_t{5});
  KR_EXPECT(store.images().dropped().empty());
}

}  // namespace
}  // namespace kr
