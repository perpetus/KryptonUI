#include <array>
#include <string>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
struct PanelHost {
  std::uint32_t request = 0;
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};

  PanelHost() {
    headless::Reset();
    KrBrowser browser{.struct_size = sizeof(KrBrowser), .self = this};
    browser.request_menu = [](void* self, std::uint32_t request, std::uint32_t,
                              std::uint64_t, KrRect) {
      static_cast<PanelHost*>(self)->request = request;
    };
    const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                                &browser};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
    headless::RunPendingTasks();
    ui.request_context_menu(ui.self, KR_MENU_SITE_DATA, 0, {20, 40, 30, 30});
    KR_ASSERT(KR_HAS_SLOT(&ui, KrShellUI, show_menu_panel_with_tiles));
    KR_ASSERT(ui.show_menu_panel_with_tiles);
  }
  ~PanelHost() {
    ui.destroy(ui.self);
    headless::RunPendingTasks();
  }
};

KR_TEST(TiledPanelCopiesRowsBadgesAndStatusAcrossTheAbi) {
  PanelHost host;
  std::string badge = "12", secondary = "Enabled";
  KrMenuItemMetadata metadata{
      .struct_size = sizeof(metadata),
      .flags = KR_MENU_META_VISIBLE,
      .secondary_label = {secondary.data(), secondary.size()}};
  const std::array root = {KrMenuItem{.struct_size = sizeof(KrMenuItem),
                                      .type = KR_MENU_ITEM_GRID,
                                      .submenu = 1},
                           KrMenuItem{.struct_size = sizeof(KrMenuItem),
                                      .type = KR_MENU_ITEM_SECTION,
                                      .submenu = 1},
                           KrMenuItem{.struct_size = sizeof(KrMenuItem),
                                      .type = KR_MENU_ITEM_FOOTER,
                                      .submenu = 1}};
  const KrMenuItem tile{.struct_size = sizeof(KrMenuItem),
                        .command_id = 42,
                        .type = KR_MENU_ITEM_COMMAND,
                        .flags = KR_MENU_ITEM_ENABLED,
                        .submenu = -1,
                        .metadata = &metadata,
                        .badge = {badge.data(), badge.size()},
                        .badge_color = 0xFFB91C1C};
  const std::array menus = {
      KrMenu{sizeof(KrMenu), root.data(), root.size(), sizeof(KrMenuItem)},
      KrMenu{sizeof(KrMenu), &tile, 1, sizeof(KrMenuItem)}};
  host.ui.show_menu_panel_with_tiles(host.ui.self, host.request, menus.data(),
                                     menus.size(), sizeof(KrMenu));
  badge.assign("99");
  secondary.clear();
  const auto& result = headless::LastMenu();
  KR_ASSERT(result.has_value());
  KR_EXPECT(result->menu.panel);
  KR_EXPECT_EQ(result->menu.menus[0][0].type, MenuItemType::kGrid);
  KR_EXPECT_EQ(result->menu.menus[0][1].type, MenuItemType::kSection);
  KR_EXPECT_EQ(result->menu.menus[0][2].type, MenuItemType::kFooter);
  KR_EXPECT_EQ(result->menu.menus[1][0].badge, std::string("12"));
  KR_EXPECT_EQ(result->menu.menus[1][0].badge_color.argb,
               std::uint32_t{0xFFB91C1C});
  KR_ASSERT(result->menu.menus[1][0].metadata.has_value());
  KR_EXPECT_EQ(result->menu.menus[1][0].metadata->secondary_label,
               std::string("Enabled"));
}

KR_TEST(TiledPanelRejectsMissingChildMenus) {
  PanelHost host;
  for (const auto type :
       {KR_MENU_ITEM_GRID, KR_MENU_ITEM_SECTION, KR_MENU_ITEM_FOOTER}) {
    const KrMenuItem item{.struct_size = sizeof(KrMenuItem),
                          .type = static_cast<std::uint32_t>(type),
                          .submenu = -1};
    const KrMenu menu{sizeof(KrMenu), &item, 1, sizeof(KrMenuItem)};
    host.ui.show_menu_panel_with_tiles(host.ui.self, host.request, &menu, 1,
                                       sizeof(KrMenu));
    KR_EXPECT(!headless::LastMenu().has_value());
  }
}

KR_TEST(OlderSizedMenuRowsHaveNoTileBadge) {
  PanelHost host;
  const KrMenuItem item{.struct_size = offsetof(KrMenuItem, badge),
                        .type = KR_MENU_ITEM_COMMAND,
                        .submenu = -1,
                        .badge = {"ignored", 7},
                        .badge_color = 0xFFFFFFFF};
  const KrMenu menu{sizeof(KrMenu), &item, 1, sizeof(KrMenuItem)};
  host.ui.show_menu_panel_with_tiles(host.ui.self, host.request, &menu, 1,
                                     sizeof(KrMenu));
  KR_ASSERT(headless::LastMenu().has_value());
  const auto& row = headless::LastMenu()->menu.menus[0][0];
  KR_EXPECT(row.badge.empty());
  KR_EXPECT_EQ(row.badge_color.argb, std::uint32_t{0});
}
}  // namespace
}  // namespace kr
