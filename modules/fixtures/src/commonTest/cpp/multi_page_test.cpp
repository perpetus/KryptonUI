#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr::fixtures {
namespace {
struct MultiPageFixture {
  MockBrowser browser;
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  explicit MultiPageFixture(std::size_t panes = 0, bool glance = false)
      : browser({.extra_tabs = 2000,
                 .multi_page = true,
                 .split_panes = panes,
                 .glance = glance},
                {}) {
    headless::Reset();
    const KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                nullptr, &browser.table()};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
    browser.Attach(ui);
    headless::RunPendingTasks();
  }
  ~MultiPageFixture() {
    browser.Detach();
    ui.destroy(ui.self);
    headless::Reset();
  }
  const Store& store() const { return static_cast<Shell*>(ui.self)->store(); }
};
KR_TEST(MockMultiPageFixturesCoverEveryPaneCountWithoutChangingTabIdentity) {
  for (std::size_t count = 2; count <= 4; ++count) {
    MultiPageFixture fixture(count);
    const auto active = fixture.store().active_tab();
    const auto* group = fixture.store().SplitForTab(active);
    KR_ASSERT(group != nullptr);
    KR_EXPECT(group->count == count);
    KR_EXPECT(group->layout == SplitLayout::kGrid);
    const auto original = group->members;
    fixture.browser.table().activate_tab(fixture.browser.table().self,
                                         group->members[count - 1].value);
    headless::RunPendingTasks();
    KR_EXPECT(fixture.store().SplitForTab(active)->members == original);
    KR_EXPECT(fixture.store().tabs().size() >= 2000);
    KR_EXPECT(headless::LastRender().today.size() + count - 1 <
              fixture.store().tabs().size());
  }
}
KR_TEST(MockSnapLayoutsApplyAndPreviewWithTwoThousandTabs) {
  MultiPageFixture fixture(4);
  fixture.browser.SetWindowState(1280, 800, 1, true, false);
  headless::RunPendingTasks();
  const auto* initial =
      fixture.store().SplitForTab(fixture.store().active_tab());
  KR_ASSERT(initial != nullptr);
  const auto members = initial->members;
  const auto id = initial->id;
  const auto tab_count = fixture.store().tabs().size();
  for (int layout = KR_SPLIT_COLUMNS; layout <= KR_SPLIT_MAIN_BOTTOM;
       ++layout) {
    const auto* group = fixture.store().SplitForTab(members[0]);
    const KrSplitEdit edit{sizeof(KrSplitEdit),
                           KR_SPLIT_SET_LAYOUT,
                           group->revision,
                           members[0].value,
                           group->space.value,
                           0,
                           static_cast<KrSplitLayout>(layout),
                           0.5f};
    const KrMultiPageCommand command{.struct_size = sizeof(KrMultiPageCommand),
                                     .kind = KR_MULTI_PAGE_EDIT_SPLIT,
                                     .group = id.value,
                                     .edit = &edit};
    KR_EXPECT(fixture.browser.table().multi_page_command(
                  fixture.browser.table().self, &command) == KR_OK);
    const auto pages = fixture.browser.page_layout();
    KR_EXPECT(pages.count == 4 && pages.divider_count == 3);
    for (std::size_t i = 0; i < 4; ++i)
      KR_EXPECT(pages.surfaces[i].tab == members[i].value);
    KR_EXPECT(fixture.store().tabs().size() == tab_count);
  }
  const KrMultiPageCommand choose{.struct_size = sizeof(KrMultiPageCommand),
                                  .kind = KR_MULTI_PAGE_CHOOSE_SPLIT_LAYOUT,
                                  .layout = KR_SPLIT_MAIN_RIGHT};
  KR_EXPECT(fixture.browser.table().multi_page_command(
                fixture.browser.table().self, &choose) == KR_OK);
  KR_EXPECT(fixture.store().ui().split_chooser.open);
  KR_EXPECT(fixture.store().ui().split_chooser.layout ==
            SplitLayout::kMainRight);
}

KR_TEST(MockVisualSplitOptionsApplyLayoutThroughTheBrowserContract) {
  MultiPageFixture fixture(3);
  const auto* group = fixture.store().SplitForTab(fixture.store().active_tab());
  KR_ASSERT(group);
  const auto id = group->id;
  const auto target = group->members[1];
  KR_ASSERT(fixture.browser.ShowSplitOptions(target.value, {100, 80, 20, 20}));
  headless::RunPendingTasks();
  KR_EXPECT(fixture.store().ui().split_chooser.layout_only);
  headless::Delegate()->OnSplitLayoutSelected(SplitLayout::kMainRight);
  headless::RunPendingTasks();
  const auto* updated = fixture.store().SplitForTab(target);
  KR_ASSERT(updated);
  KR_EXPECT(updated->id == id);
  KR_EXPECT(updated->layout == SplitLayout::kMainRight);
  KR_EXPECT(!fixture.store().ui().split_chooser.open);
  KR_EXPECT(!fixture.browser.ShowSplitOptions(999999, {100, 80, 20, 20}));
}
KR_TEST(MockGlanceReusesPreviewPromotesTheSameTabAndClosesOnDeparture) {
  MultiPageFixture fixture(3, true);
  const auto original = fixture.browser.glance_state();
  KR_EXPECT(fixture.store().IsTransientTab(TabId{original.child}));
  KR_EXPECT(fixture.store().presented_tab() == TabId{original.parent});
  KR_ASSERT(
      fixture.browser.OpenGlance(original.child, "https://example.com/reused"));
  KR_EXPECT(fixture.browser.glance_state().child == original.child);
  KR_ASSERT(fixture.browser.PromoteGlance(true));
  KR_EXPECT(fixture.store().FindTab(TabId{original.child}) != nullptr);
  KR_EXPECT(!fixture.store().IsTransientTab(TabId{original.child}));
  KR_EXPECT(fixture.store().SplitForTab(TabId{original.child})->count == 4);
  KR_ASSERT(fixture.browser.OpenGlance(original.parent,
                                       "https://example.com/second"));
  const auto preview = fixture.browser.glance_state().child;
  KR_EXPECT(!fixture.browser.PromoteGlance(true));
  fixture.browser.CloseGlance();
  KR_EXPECT(fixture.store().FindTab(TabId{preview}) == nullptr);
  KR_EXPECT(fixture.store().FindTab(TabId{original.child}) != nullptr);
}
KR_TEST(MockGlanceExpandKeepsItsIdentityAndBecomesAnOrdinaryTab) {
  MultiPageFixture fixture(0, true);
  const auto preview = fixture.browser.glance_state().child;
  KR_ASSERT(fixture.browser.PromoteGlance(false));
  KR_EXPECT(fixture.store().active_tab() == TabId{preview});
  KR_EXPECT(fixture.store().FindTab(TabId{preview}) != nullptr);
  KR_EXPECT(!fixture.store().IsTransientTab(TabId{preview}));
  KR_EXPECT(fixture.browser.glance_state().phase == KR_GLANCE_CLOSED);
}

KR_TEST(MockPageLayoutUsesSharedGeometryForEveryPaneAndMirrorsRtl) {
  for (std::size_t count = 2; count <= 4; ++count) {
    MultiPageFixture fixture(count);
    fixture.browser.SetWindowState(1280, 800, 1, true, false);
    headless::RunPendingTasks();
    const auto pages = fixture.browser.page_layout();
    const auto rtl = fixture.browser.page_layout(true);
    KR_ASSERT(pages.count == count);
    KR_EXPECT(pages.divider_count == count - 1);
    const auto* group =
        fixture.store().SplitForTab(fixture.store().active_tab());
    KR_ASSERT(group != nullptr);
    const auto card = fixture.browser.content_bounds();
    for (std::size_t i = 0; i < count; ++i) {
      KR_EXPECT(pages.surfaces[i].tab == group->members[i].value);
      KR_EXPECT(pages.surfaces[i].bounds.width > 0);
      KR_EXPECT(pages.surfaces[i].bounds.height > 0);
      KR_EXPECT(pages.surfaces[i].bounds.x >= card.x);
      KR_EXPECT(pages.surfaces[i].bounds.y >= card.y);
      KR_EXPECT(rtl.surfaces[i].tab == pages.surfaces[i].tab);
      KR_EXPECT(rtl.surfaces[i].bounds.width == pages.surfaces[i].bounds.width);
    }
    KR_EXPECT(rtl.surfaces[0].bounds.x > pages.surfaces[0].bounds.x);
  }
}

KR_TEST(MockGlancePageLayoutOverlaysOnlyItsParentPane) {
  MultiPageFixture fixture(3, true);
  fixture.browser.SetWindowState(1280, 800, 1, true, false);
  headless::RunPendingTasks();
  const auto state = fixture.browser.glance_state();
  const auto pages = fixture.browser.page_layout();
  KR_ASSERT(pages.count == 4);
  KR_EXPECT(pages.surfaces[3].tab == state.child);
  KR_EXPECT(pages.surfaces[3].glance);
  bool found_parent = false;
  for (std::size_t i = 0; i < 3; ++i) {
    if (pages.surfaces[i].tab != state.parent)
      continue;
    found_parent = true;
    const auto parent = pages.surfaces[i].bounds;
    KR_EXPECT(pages.glance.frame.x >= parent.x);
    KR_EXPECT(pages.glance.frame.y >= parent.y);
    KR_EXPECT(pages.glance.frame.x + pages.glance.frame.width <=
              parent.x + parent.width);
    KR_EXPECT(pages.glance.frame.y + pages.glance.frame.height <=
              parent.y + parent.height);
  }
  KR_EXPECT(found_parent);
  KR_ASSERT(fixture.browser.PromoteGlance(true));
  const auto promoted = fixture.browser.page_layout();
  KR_ASSERT(promoted.count == 4);
  KR_EXPECT(!promoted.surfaces[3].glance);
  KR_EXPECT(promoted.surfaces[3].tab == state.child);
}
}  // namespace
}  // namespace kr::fixtures
