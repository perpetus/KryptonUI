#include "krypton/ui/presentation/presenter.hpp"

#include "krypton/testing/test.hpp"

namespace kr {
namespace {

Store MakeStore() {
  Store store;
  store.SetSpaces(
      {{.id = SpaceId{1}, .name = "Personal", .color = Color{0xFF7C5CFF}},
       {.id = SpaceId{2}, .name = "Work", .color = Color{0xFF3B82F6}}},
      SpaceId{1});
  auto add = [&](std::uint64_t id, std::uint64_t space, Flags<TabFlag> flags,
                 std::string title) {
    (void)store.InsertTab(store.tabs().size(), {.id = TabId{id},
                                                .space = SpaceId{space},
                                                .flags = flags,
                                                .title = std::move(title)});
  };
  add(1, 1, TabFlag::kFavorite, "Mail");
  add(2, 2, TabFlag::kFavorite, "Calendar");
  add(3, 1, TabFlag::kPinned, "Docs");
  add(4, 1, {}, "News");
  add(5, 2, {}, "Jira");
  add(6, 1, TabFlag::kLoading, "Search");
  (void)store.SetActiveTab(TabId{4});
  return store;
}

std::vector<std::string_view> Titles(std::span<const TabItemVm> items) {
  std::vector<std::string_view> titles;
  for (const TabItemVm& item : items)
    titles.push_back(item.title);
  return titles;
}

KR_TEST(PresenterSplitsSectionsByActiveSpace) {
  const Store store = MakeStore();
  Presenter presenter;
  const ShellViewModel& vm = presenter.Present(store, {}, Dirty::kAll);

  using V = std::vector<std::string_view>;
  KR_EXPECT(Titles(vm.favorites) == (V{"Mail", "Calendar"}));  // all spaces
  KR_EXPECT(Titles(vm.pinned) == (V{"Docs"}));
  KR_EXPECT(Titles(vm.today) == (V{"News", "Search"}));  // no "Jira"
  KR_EXPECT(vm.today[0].active);
  KR_EXPECT_EQ(vm.spaces.size(), 2u);
  KR_EXPECT(vm.spaces[0].active);
}

KR_TEST(PresenterTintsWithSpaceButNotInIncognito) {
  Store store = MakeStore();
  Presenter presenter;
  const Color base{design::kTokens.light.gradient_base};

  const Color tinted =
      presenter.Present(store, {}, Dirty::kAll).theme.gradient_start;
  KR_EXPECT(tinted != base);

  store.SetTheme({.incognito = true});
  const Color neutral =
      presenter.Present(store, {}, Dirty::kAll).theme.gradient_start;
  KR_EXPECT(neutral == base);
}

KR_TEST(PresenterRemovesWorkspaceTintAndGrainInBothGlassModesAndRestoresOff) {
  Store store = MakeStore();
  Presenter presenter;
  WorkspaceTheme workspace{.customized = true, .point_count = 1, .texture = 1};
  workspace.points[0].color = Color{0xff22aa88};
  KR_ASSERT(store.SetWorkspaceTheme(SpaceId{1}, workspace).has_value());
  for (const bool dark : {false, true}) {
    store.SetTheme({.dark = dark});
    for (const auto mode : {GlassMode::kPerformance, GlassMode::kQuality}) {
      auto prefs = store.appearance_prefs();
      prefs.glass_mode = mode;
      KR_ASSERT(store.SetAppearancePrefs(prefs).has_value());
      const auto& theme = presenter.Present(store, {}, Dirty::kAll).theme;
      const Color base{design::PaletteFor(dark).gradient_base};
      KR_EXPECT(theme.gradient_start == base);
      KR_EXPECT(theme.gradient_end == base);
      KR_EXPECT(theme.workspace_theme == nullptr);
      KR_EXPECT(theme.accent == workspace.points[0].color);
    }
    auto prefs = store.appearance_prefs();
    prefs.glass_mode = GlassMode::kOff;
    KR_ASSERT(store.SetAppearancePrefs(prefs).has_value());
    KR_EXPECT(presenter.Present(store, {}, Dirty::kAll).theme.workspace_theme !=
              nullptr);
    KR_ASSERT(store.WorkspaceThemeFor(SpaceId{1}) != nullptr);
    KR_EXPECT(*store.WorkspaceThemeFor(SpaceId{1}) == workspace);
  }
}

KR_TEST(PresenterSplitsActionsAndSkipsHidden) {
  Store store;
  const auto visible = ActionFlag::kVisible | ActionFlag::kEnabled;
  store.SetActions({
      {.id = ActionId{1}, .kind = ActionKind::kPage, .flags = visible},
      {.id = ActionId{2}, .kind = ActionKind::kSiteSetting, .flags = visible},
      {.id = ActionId{3}, .kind = ActionKind::kExtension, .flags = visible},
      {.id = ActionId{4},
       .kind = ActionKind::kToolbar,
       .flags = ActionFlag::kEnabled},
  });
  Presenter presenter;
  const ShellViewModel& vm = presenter.Present(store, {}, Dirty::kAll);
  KR_EXPECT_EQ(vm.page_actions.size(), 3u);
  KR_EXPECT_EQ(vm.toolbar_actions.size(), 0u);  // id 4 is not visible
}

KR_TEST(PresenterReusesBuffersAcrossRenders) {
  const Store store = MakeStore();
  Presenter presenter;
  const TabItemVm* first =
      presenter.Present(store, {}, Dirty::kAll).today.data();
  const TabItemVm* second =
      presenter.Present(store, {}, Dirty::kAll).today.data();
  KR_EXPECT(first == second);  // same storage: no reallocation in steady state
}

}  // namespace
}  // namespace kr

namespace kr {
KR_TEST(PresenterHorizontalTabsPreserveHostOrderAndWorkspaceFiltering) {
  auto store = MakeStore();
  store.SetLayoutPrefs({.flags = LayoutFlag::kTabsClassic});
  Presenter presenter;
  const auto& vm =
      presenter.Present(store, {.tabs = TabLayout::kClassic}, Dirty::kAll);
  KR_EXPECT(Titles(vm.horizontal_tabs) ==
            (std::vector<std::string_view>{"Mail", "Calendar", "Docs", "News",
                                           "Search"}));
  KR_EXPECT_EQ(vm.horizontal_tabs[3].id, TabId{4});
  KR_EXPECT(vm.horizontal_tabs[3].active);
}

KR_TEST(PresenterMinimalAddressRetainsFullDisplayDuringEditing) {
  auto store = MakeStore();
  NavState nav;
  nav.full_url = "https://example.test:8443/article?q=1#section";
  nav.display_text = "example.test:8443/article?q=1#section";
  store.SetNav(nav);
  store.SetLayoutPrefs(
      {.flags = LayoutFlag::kTabsDynamic | LayoutFlag::kMinimalAddress});
  Presenter presenter;
  KR_EXPECT_EQ(presenter.Present(store, {}, Dirty::kAll).header.address_text,
               "example.test:8443");
  store.SetAddressFocused(true);
  KR_EXPECT_EQ(presenter.Present(store, {}, Dirty::kAll).header.address_text,
               nav.display_text);
  store.SetAddressFocused(false);
  store.OpenCommandBar(nav.full_url, true);
  KR_EXPECT_EQ(presenter.Present(store, {}, Dirty::kAll).header.address_text,
               nav.display_text);
}

KR_TEST(PresenterRetainsCollectionsUntilTheirStoreDomainChanges) {
  auto store = MakeStore();
  Presenter presenter;
  (void)presenter.Present(store, {}, {});
  (void)store.TakeDirty();
  store.SetNav({.display_text = "changed.example"});
  const auto& navigation = presenter.Present(store, {}, store.TakeDirty());
  KR_EXPECT_EQ(navigation.header.address_text, "changed.example");
  KR_EXPECT_EQ(navigation.today.size(), 2u);
  KR_EXPECT(navigation.today[0].active);

  KR_ASSERT(store.SetActiveTab(TabId{3}));
  const auto& pinned = presenter.Present(store, {}, store.TakeDirty());
  KR_EXPECT(pinned.pinned[0].active);
  KR_EXPECT(!pinned.today[0].active);

  KR_ASSERT(store.UpdateTab(
      {.id = TabId{4},
       .space = SpaceId{1},
       .title = "Replaced long page title after a host update"}));
  const auto& updated = presenter.Present(store, {}, store.TakeDirty());
  KR_EXPECT_EQ(updated.today[0].title,
               "Replaced long page title after a host update");
  store.SetLayoutPrefs({.flags = LayoutFlag::kTabsClassic});
  const auto& horizontal = presenter.Present(store, {}, store.TakeDirty());
  KR_EXPECT_EQ(horizontal.horizontal_tabs.size(), 5u);
  KR_ASSERT(store.SetActiveTab(TabId{1}));
  const auto& favorite = presenter.Present(store, {}, store.TakeDirty());
  KR_EXPECT(favorite.favorites[0].active);
  KR_EXPECT(favorite.horizontal_tabs[0].active);
  KR_EXPECT(!favorite.pinned[0].active);
  KR_EXPECT(!favorite.horizontal_tabs[2].active);
}

KR_TEST(
    PresenterWorkspaceIconsReactToThemeOnlyChangesWithoutChangingSavedColor) {
  Store store;
  const Color blue{0xFF153F70};
  store.SetSpaces({{.id = SpaceId{1}, .name = "Blue", .color = blue}},
                  SpaceId{1});
  auto appearance = store.appearance_prefs();
  appearance.glass_mode = GlassMode::kOff;
  KR_ASSERT(store.SetAppearancePrefs(appearance));
  Presenter presenter;
  const auto& light = presenter.Present(store, {}, Dirty::kAll);
  KR_EXPECT_EQ(light.spaces[0].icon_color, blue);
  store.SetTheme({.dark = true});
  const auto& dark = presenter.Present(store, {}, Dirty::kTheme);
  KR_EXPECT_EQ(dark.spaces[0].icon_color,
               Color{design::kTokens.dark.icon}.WithAlpha(255));
  KR_EXPECT_EQ(dark.spaces[0].color, blue);
  KR_EXPECT_EQ(store.spaces()[0].color, blue);
}

KR_TEST(PresenterWorkspaceIconsContrastWithGlassTintAndHonorHighContrast) {
  Store store;
  store.SetSpaces(
      {{.id = SpaceId{1}, .name = "Blue", .color = Color{0xFF3B82F6}}},
      SpaceId{1});
  auto appearance = store.appearance_prefs();
  appearance.glass_mode = GlassMode::kQuality;
  KR_ASSERT(store.SetAppearancePrefs(appearance));
  store.SetTheme({.dark = true});
  Presenter presenter;
  const auto& glass = presenter.Present(store, {}, Dirty::kAll);
  KR_EXPECT_EQ(glass.theme.gradient_start,
               Color{design::kTokens.dark.gradient_base});
  KR_EXPECT_EQ(glass.spaces[0].icon_color,
               Color{design::kTokens.light.icon}.WithAlpha(255));
  store.SetTheme({.dark = true, .high_contrast = true});
  const auto& high_contrast = presenter.Present(store, {}, Dirty::kTheme);
  KR_EXPECT_EQ(high_contrast.spaces[0].icon_color, Color{});
}

KR_TEST(PresenterRefreshesBorrowedCollectionsForAnotherStore) {
  auto first = MakeStore();
  Store second;
  Presenter presenter;
  (void)presenter.Present(first, {}, Dirty::kAll);
  const auto& vm = presenter.Present(second, {}, {});
  KR_EXPECT(vm.today.empty());
  KR_EXPECT(vm.favorites.empty());
  KR_EXPECT(vm.spaces.empty());
}
}  // namespace kr
