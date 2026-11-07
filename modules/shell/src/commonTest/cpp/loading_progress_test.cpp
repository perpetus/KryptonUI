#include "krypton/ui/model/loading_progress.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <span>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
KR_TEST(LoadingProgressAcceptsOnlyHttpPagesAndMovesForwardWithinANavigation) {
  KR_EXPECT(IsProgressPage("https://example.test/path"));
  KR_EXPECT(IsProgressPage("HTTP://example.test"));
  for (auto url : {"krypton://settings", "about:blank", "file:///test",
                   "httpsx://test", ""})
    KR_EXPECT(!IsProgressPage(url));
  LoadingProgress progress;
  progress.Reset(TabId{1});
  progress.SyncNavigation(TabId{1}, true, true, true);
  KR_EXPECT(progress.state().phase == LoadPhase::kLoading);
  KR_EXPECT_EQ(progress.state().progress,
               design::kTokens.loading_progress.minimum_progress);
  const auto session = progress.state().session;
  KR_ASSERT(progress.Update(
      {.tab = TabId{1}, .navigation_id = 10, .progress = 0.6f, .loading = true},
      true));
  KR_EXPECT_EQ(progress.state().session, session);
  KR_EXPECT(!progress.Update(
      {.tab = TabId{1}, .navigation_id = 10, .progress = 0.2f, .loading = true},
      true));
  KR_EXPECT(!progress.Update(
      {.tab = TabId{1}, .navigation_id = 9, .progress = 0.8f, .loading = true},
      true));
  KR_EXPECT_EQ(progress.state().progress, 0.6f);
  progress.SyncNavigation(TabId{1}, true, true,
                          true);  // redirect inside the known navigation
  KR_EXPECT_EQ(progress.state().session, session);
  progress.SyncNavigation(TabId{1}, false, false, true);
  KR_EXPECT(progress.state().phase == LoadPhase::kComplete);
  KR_EXPECT_EQ(progress.state().progress, 1.0f);
  KR_EXPECT(!progress.Update(
      {.tab = TabId{1}, .navigation_id = 10, .progress = 0.7f, .loading = true},
      true));
  KR_ASSERT(progress.Update(
      {.tab = TabId{1}, .navigation_id = 11, .progress = 0.1f, .loading = true},
      true));
  KR_EXPECT(progress.state().session != session);
  KR_EXPECT_EQ(progress.state().progress, 0.1f);
  KR_EXPECT(!progress.Update(
      {.tab = TabId{2}, .navigation_id = 12, .progress = 0.5f, .loading = true},
      true));
  KR_ASSERT(progress.Update({.tab = TabId{1},
                             .navigation_id = 12,
                             .progress = 0.2f,
                             .loading = true,
                             .same_document = true},
                            true));
  KR_EXPECT(progress.state().phase == LoadPhase::kIdle);
}

KR_TEST(LoadingProgressHidesForAddressFocusSearchAndHiddenChrome) {
  Store store;
  KR_ASSERT(
      store.InsertTab(0, {.id = TabId{1}, .url = "https://example.test"}));
  KR_ASSERT(store.SetActiveTab(TabId{1}));
  store.SetNav({.is_loading = true, .full_url = "https://example.test"});
  Presenter presenter;
  ShellLayout layout;
  layout.toolbar_layout = ToolbarLayout::kSingle;
  layout.sidebar_visible = true;
  KR_EXPECT(
      presenter.Present(store, layout, Dirty::kAll).loading_progress.visible);
  store.SetAddressFocused(true);
  KR_EXPECT(
      !presenter.Present(store, layout, Dirty::kAll).loading_progress.visible);
  store.SetAddressFocused(false);
  store.OpenCommandBar("", true);
  KR_EXPECT(
      !presenter.Present(store, layout, Dirty::kAll).loading_progress.visible);
  store.CloseCommandBar();
  layout.sidebar_visible = false;
  KR_EXPECT(
      !presenter.Present(store, layout, Dirty::kAll).loading_progress.visible);
  layout.sidebar_visible = true;
  KR_ASSERT(store.SetLoadProgress({.tab = TabId{1},
                                   .navigation_id = 1,
                                   .progress = 0.3f,
                                   .loading = true,
                                   .enabled = false}));
  KR_EXPECT(
      !presenter.Present(store, layout, Dirty::kAll).loading_progress.visible);
  KR_EXPECT(!store.SetLoadProgress(
      {.tab = TabId{1},
       .navigation_id = 1,
       .progress = std::numeric_limits<float>::quiet_NaN()}));
  store.SetNav({.is_loading = true, .full_url = "krypton://settings"});
  KR_EXPECT(store.loading_progress().phase == LoadPhase::kIdle);
}

struct Host {
  KrShellUI ui{};
  Host() {
    headless::Reset();
    KrBrowser browser{};
    browser.struct_size = sizeof(browser);
    ui.struct_size = sizeof(ui);
    const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                                &browser};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
    const KrTab tab{.struct_size = sizeof(KrTab),
                    .id = 1,
                    .url = {"https://example.test", 20}};
    ui.tab_inserted(ui.self, 0, &tab);
    ui.set_active_tab(ui.self, 1);
    const KrWindowState window{.struct_size = sizeof(KrWindowState),
                               .flags = KR_WINDOW_ACTIVE,
                               .scale_factor = 1,
                               .client = {0, 0, 1200, 800}};
    ui.set_window_state(ui.self, &window);
    const KrNavState nav{.struct_size = sizeof(KrNavState),
                         .is_loading = true,
                         .full_url = {"https://example.test", 20}};
    ui.set_nav_state(ui.self, &nav);
  }
  ~Host() {
    if (ui.self)
      ui.destroy(ui.self);
    headless::RunPendingTasks();
  }
};

KR_TEST(LoadingProgressAbiValidatesSnapshotsAndCoalescesUpdates) {
  Host host;
  KrLoadProgress update{sizeof(update), 1, 1, 0.2f,
                        KR_LOAD_PROGRESS_ENABLED | KR_LOAD_PROGRESS_LOADING};
  KR_ASSERT(host.ui.set_load_progress(host.ui.self, &update) == KR_OK);
  update.progress = 0.7f;
  KR_ASSERT(host.ui.set_load_progress(host.ui.self, &update) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(headless::RenderCount(), 1u);
  KR_EXPECT_EQ(headless::LastRender().loading_progress.progress, 0.7f);
  update.progress = std::numeric_limits<float>::infinity();
  KR_EXPECT(host.ui.set_load_progress(host.ui.self, &update) ==
            KR_ERR_INVALID_ARGUMENT);
  update.progress = 0.8f;
  update.struct_size = offsetof(KrLoadProgress, flags);
  KR_EXPECT(host.ui.set_load_progress(host.ui.self, &update) ==
            KR_ERR_INVALID_ARGUMENT);
  update.struct_size = sizeof(update);
  update.flags =
      KR_LOAD_PROGRESS_ENABLED | KR_LOAD_PROGRESS_LOADING | (1u << 9);
  KR_EXPECT(host.ui.set_load_progress(host.ui.self, &update) ==
            KR_OK);  // future flag bits are ignored
  KR_EXPECT(host.ui.set_load_progress(host.ui.self, nullptr) ==
            KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(LoadingProgressAbiDoesNotOverwriteOlderUiOutputBuffers) {
  headless::Reset();
  constexpr auto capacity = offsetof(KrShellUI, set_load_progress);
  KrShellUI ui{};
  const auto bytes = std::as_writable_bytes(std::span{&ui, std::size_t{1}});
  std::fill(bytes.begin() + capacity, bytes.end(), std::byte{0x5a});
  ui.struct_size = capacity;
  KrBrowser browser{};
  browser.struct_size = sizeof(browser);
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser};
  KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  KR_EXPECT(!KR_HAS_SLOT(&ui, KrShellUI, set_load_progress));
  for (std::size_t i = capacity; i < bytes.size(); ++i)
    KR_EXPECT(bytes[i] == std::byte{0x5a});
  ui.destroy(ui.self);
  headless::RunPendingTasks();
}
}  // namespace
}  // namespace kr
