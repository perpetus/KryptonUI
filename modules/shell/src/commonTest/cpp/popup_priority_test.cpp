#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
struct PopupHost {
  std::uint32_t requested = 0;
  int closed = 0;
  KrShellUI ui{};
  PopupHost() {
    headless::Reset();
    KrBrowser browser{};
    browser.struct_size = sizeof(browser);
    browser.self = this;
    browser.request_menu = [](void* self, std::uint32_t request, std::uint32_t,
                              std::uint64_t, KrRect) {
      static_cast<PopupHost*>(self)->requested = request;
    };
    browser.menu_closed = [](void* self, std::uint32_t) {
      ++static_cast<PopupHost*>(self)->closed;
    };
    const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                                &browser};
    ui.struct_size = sizeof(ui);
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  }
  ~PopupHost() {
    ui.destroy(ui.self);
    headless::RunPendingTasks();
  }
  PopupHost(const PopupHost&) = delete;
  PopupHost& operator=(const PopupHost&) = delete;
  void Reply() {
    const KrMenuItem item{.struct_size = sizeof(KrMenuItem),
                          .command_id = 1,
                          .type = KR_MENU_ITEM_COMMAND,
                          .flags = KR_MENU_ITEM_ENABLED,
                          .label = {"Menu", 4},
                          .submenu = -1};
    const KrMenu menu{sizeof(KrMenu), &item, 1, sizeof(KrMenuItem)};
    ui.show_menu_panel(ui.self, requested, &menu, 1, sizeof(KrMenu));
  }
};

KR_TEST(SearchRejectsDelayedMenuRepliesAndNewMenusDismissSearch) {
  PopupHost host;
  auto& input = *headless::Delegate();
  input.OnMenuRequested(MenuKind::kApp, 0, {10, 20, 28, 28});
  input.OnCommandBarRequested();
  host.Reply();
  KR_EXPECT(!headless::LastMenu());
  KR_EXPECT_EQ(host.closed, 1);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().command_bar_open);
  input.OnMenuRequested(MenuKind::kApp, 0, {10, 20, 28, 28});
  host.Reply();
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().command_bar_open);
  KR_ASSERT(headless::LastMenu());
  KR_EXPECT_EQ(headless::LastMenu()->request_id, host.requested);
}

KR_TEST(SearchPreventsLateHoverPreviewRequestsFromCoveringIt) {
  PopupHost host;
  const KrTab tab{.struct_size = sizeof(KrTab), .id = 1};
  host.ui.tab_inserted(host.ui.self, 0, &tab);
  host.ui.set_active_tab(host.ui.self, 1);
  auto& input = *headless::Delegate();
  input.OnCommandBarRequested();
  KR_EXPECT_EQ(input.OnTabPreviewRequested(TabId{1}), 0u);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().preview_tab);
}
}  // namespace
}  // namespace kr
