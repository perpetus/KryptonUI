#include <array>
#include <cstddef>
#include <span>
#include <string>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton/ui/presentation/chrome_layout.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
KR_TEST(
    ChromeActionsPlaceExtensionsInsideTheAddressAndKeepToolbarActionsSeparate) {
  Store store;
  store.SetActions({{.id = ActionId{1},
                     .kind = ActionKind::kExtension,
                     .flags = ActionFlag::kVisible},
                    {.id = ActionId{2},
                     .kind = ActionKind::kSiteSetting,
                     .flags = ActionFlag::kVisible},
                    {.id = ActionId{3},
                     .kind = ActionKind::kToolbar,
                     .flags = ActionFlag::kVisible},
                    {.id = ActionId{4}, .kind = ActionKind::kExtension}});
  Presenter presenter;
  const auto& vm = presenter.Present(store, {}, Dirty::kAll);
  KR_EXPECT_EQ(vm.page_actions.size(), 2u);
  KR_EXPECT_EQ(vm.toolbar_actions.size(), 1u);
  KR_EXPECT(vm.page_actions[0].id == ActionId{1});
}

KR_TEST(
    ChromeCompactButtonAdaptsToAvailableWidthWithoutChangingTheWindowLayout) {
  Store store;
  Presenter presenter;
  ShellLayout layout;
  layout.toolbar_layout = ToolbarLayout::kSingle;
  layout.native_titlebar.enabled = true;
  const auto& spacing = design::kTokens.spacing;
  const float required = 2 * spacing.sidebar_padding +
                         5 * (spacing.button_size + 2 * spacing.button_margin);
  layout.sidebar.width = required - 1;
  KR_EXPECT(!presenter.Present(store, layout, Dirty::kAll)
                 .header.show_compact_toggle);
  layout.sidebar.width = required;
  KR_EXPECT(
      presenter.Present(store, layout, Dirty::kAll).header.show_compact_toggle);
}

KR_TEST(ChromeAddressActionsPreserveTheInputAndSiteMenuAtNarrowWidths) {
  const auto& t = design::kTokens.spacing;
  const float target = t.button_size + 2 * t.button_margin;
  const float field = 2 * t.field_padding + 4 * target;
  const auto capacity = AddressActionCapacity(field);
  KR_EXPECT_EQ(capacity, 2u);
  const std::array kinds{ActionKind::kPage, ActionKind::kExtension,
                         ActionKind::kSiteSetting};
  std::array<std::uint8_t, kinds.size()> visible{};
  PlaceAddressActions(kinds, capacity, visible);
  KR_EXPECT_EQ(visible[0], 1u);
  KR_EXPECT_EQ(visible[1], 0u);
  KR_EXPECT_EQ(visible[2], 1u);
  PlaceAddressActions(kinds, 3, visible);
  KR_EXPECT(
      std::ranges::all_of(visible, [](auto value) { return value == 1; }));
}

KR_TEST(ChromeVerticalSiteToolsConsolidateControlsAndRestoreThemHorizontally) {
  Store store;
  store.SetActions({{.id = ActionId{1},
                     .kind = ActionKind::kExtension,
                     .flags = ActionFlag::kVisible},
                    {.id = ActionId{2},
                     .kind = ActionKind::kSiteSetting,
                     .flags = ActionFlag::kVisible},
                    {.id = ActionId{3},
                     .kind = ActionKind::kSiteTools,
                     .flags = ActionFlag::kVisible}});
  Presenter presenter;
  const auto& vertical = presenter.Present(store, {}, Dirty::kAll);
  KR_EXPECT_EQ(vertical.page_actions.size(), 2u);
  KR_EXPECT(vertical.page_actions.front().id == ActionId{1});
  KR_EXPECT(vertical.page_actions.back().id == ActionId{3});
  KR_EXPECT(!vertical.header.show_site_security);
  store.SetLayoutPrefs({.flags = LayoutFlag::kTabsClassic});
  const auto& horizontal = presenter.Present(store, {}, Dirty::kLayout);
  KR_EXPECT_EQ(horizontal.page_actions.size(), 3u);
  KR_EXPECT(horizontal.header.show_site_security);
}

KR_TEST(ChromeSiteToolsStayReachableWhenAllOtherAddressActionsOverflow) {
  const std::array kinds{ActionKind::kExtension, ActionKind::kSiteSetting,
                         ActionKind::kSiteTools};
  std::array<std::uint8_t, 3> visible{};
  PlaceAddressActions(kinds, 0, visible);
  KR_EXPECT_EQ(visible[0], 0u);
  KR_EXPECT_EQ(visible[1], 0u);
  KR_EXPECT_EQ(visible[2], 1u);
}

KR_TEST(ChromeCopyLinkAndToolsKeepTheirAddressTargetsAtNarrowWidths) {
  const std::array kinds{ActionKind::kExtension, ActionKind::kCopyLink,
                         ActionKind::kSiteTools};
  std::array<std::uint8_t, 3> visible{};
  PlaceAddressActions(kinds, 0, visible);
  KR_EXPECT_EQ(visible[0], 0);
  KR_EXPECT_EQ(visible[1], 1);
  KR_EXPECT_EQ(visible[2], 1);
}

KR_TEST(ChromePanelAbiCopiesHostOwnedRows) {
  headless::Reset();
  KrBrowser browser{};
  browser.struct_size = sizeof(browser);
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser};
  KrShellUI ui{};
  ui.struct_size = sizeof(ui);
  KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  struct Cleanup {
    KrShellUI& ui;
    ~Cleanup() {
      ui.destroy(ui.self);
      headless::RunPendingTasks();
    }
  } cleanup{ui};
  auto* delegate = headless::Delegate();
  KR_ASSERT(delegate);
  // Request ids are generated by the shared shell, including rich panel
  // replies.
  delegate->OnMenuRequested(MenuKind::kApp, 0, {});
  const KrMenuItem header{.struct_size = sizeof(KrMenuItem),
                          .type = KR_MENU_ITEM_HEADER,
                          .label = {"Account", 7},
                          .submenu = 1};
  const KrMenuItem sign_in{.struct_size = sizeof(KrMenuItem),
                           .command_id = 1,
                           .type = KR_MENU_ITEM_COMMAND,
                           .flags = KR_MENU_ITEM_ENABLED,
                           .label = {"Sign In", 7},
                           .submenu = -1};
  const std::array menus{
      KrMenu{sizeof(KrMenu), &header, 1, sizeof(KrMenuItem)},
      KrMenu{sizeof(KrMenu), &sign_in, 1, sizeof(KrMenuItem)}};
  ui.show_menu_panel(ui.self, 1, menus.data(), menus.size(), sizeof(KrMenu));
  KR_ASSERT(headless::LastMenu());
  KR_EXPECT(headless::LastMenu()->menu.panel);
  KR_EXPECT_EQ(headless::LastMenu()->menu.menus[0][0].label,
               std::string("Account"));
}

KR_TEST(ChromePanelAbiPreservesOutputCapacityBeforeItsAppendedSlot) {
  headless::Reset();
  KrBrowser browser{};
  browser.struct_size = sizeof(browser);
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser};
  KrShellUI ui{};
  constexpr auto capacity = offsetof(KrShellUI, show_menu_panel);
  const auto bytes = std::as_writable_bytes(std::span{&ui, std::size_t{1}});
  std::fill(bytes.begin() + capacity, bytes.end(), std::byte{0x5a});
  ui.struct_size = capacity;
  KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  KR_EXPECT_EQ(ui.struct_size, capacity);
  KR_EXPECT(!KR_HAS_SLOT(&ui, KrShellUI, show_menu_panel));
  for (std::size_t i = capacity; i < bytes.size(); ++i)
    KR_EXPECT(bytes[i] == std::byte{0x5a});
  ui.destroy(ui.self);
  headless::RunPendingTasks();
}
}  // namespace
}  // namespace kr

namespace kr {
KR_TEST(OnlySidebarKeepsWindowsCaptionOwnershipDuringHiddenAndRevealedStates) {
  Store store;
  Presenter presenter;
  const NativeTitleBarMetrics native{
      .enabled = true, .height = 32, .right_inset = 138};
  const LayoutPrefs prefs{.flags = LayoutFlag::kToolbarSingle};
  for (const bool reveal : {false, true}) {
    const auto layout = ComputeLayout({.client = {0, 0, 1280, 800}}, prefs,
                                      false, reveal, design::kTokens, native);
    KR_EXPECT(!presenter.Present(store, layout, Dirty::kAll)
                   .header.show_window_controls);
  }
  const auto fallback =
      ComputeLayout({.client = {0, 0, 1280, 800}}, prefs, false);
  KR_EXPECT(presenter.Present(store, fallback, Dirty::kAll)
                .header.show_window_controls);
}
}  // namespace kr
