#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr::fixtures {
KR_TEST(MockFoldersMoveRealTabsAndUnpackWithoutChangingTheirIdentity) {
  headless::Reset();
  MockBrowser browser({.extra_tabs = 2000}, {});
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser.table()};
  KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  browser.Attach(ui);
  auto& shell = *static_cast<Shell*>(ui.self);
  const auto tab = shell.store().active_tab();
  const auto command = [&](KrFolderCommand value) {
    value.struct_size = sizeof(value);
    const auto result =
        browser.table().folder_command(browser.table().self, &value);
    headless::RunPendingTasks();
    return result;
  };
  KR_EXPECT_EQ(command({.kind = KR_FOLDER_CREATE, .space = 1}), KR_OK);
  KR_ASSERT(shell.store().folders().size() == 1);
  const auto id = shell.store().folders()[0].id;
  KR_EXPECT_EQ(
      command(
          {.kind = KR_FOLDER_MOVE_TAB, .folder = id.value, .tab = tab.value}),
      KR_OK);
  KR_EXPECT_EQ(shell.store().FindTab(tab)->folder, id);
  KR_EXPECT(shell.store().FindTab(tab)->flags.Has(TabFlag::kPinned));
  const auto count = shell.store().tabs().size();
  KR_EXPECT_EQ(command({.kind = KR_FOLDER_CREATE, .parent = id.value}), KR_OK);
  KR_EXPECT_EQ(
      command(
          {.kind = KR_FOLDER_COLLAPSE, .folder = id.value, .collapsed = true}),
      KR_OK);
  KR_EXPECT_EQ(headless::LastRender().folder_rows.size(), 2u);
  KR_EXPECT_EQ(
      command({.kind = KR_FOLDER_MOVE, .folder = id.value, .parent = id.value}),
      KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT_EQ(command({.kind = KR_FOLDER_UNPACK, .folder = id.value}), KR_OK);
  KR_EXPECT(shell.store().folders().empty());
  KR_EXPECT_EQ(shell.store().tabs().size(), count);
  KR_EXPECT(!shell.store().FindTab(tab)->folder);
  browser.Detach();
  ui.destroy(ui.self);
  headless::Reset();
}
}  // namespace kr::fixtures
