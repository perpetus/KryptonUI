#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {

KR_TEST(HostSurfaceContextMenuPreservesClientDipAnchor) {
  headless::Reset();
  struct Request {
    std::uint32_t id = 0;
    std::uint32_t kind = 0;
    std::uint64_t target = 0;
    KrRect anchor{};
    int count = 0;
  } request;
  KrBrowser browser{};
  browser.struct_size = sizeof(browser);
  browser.self = &request;
  browser.request_menu = [](void* self, std::uint32_t id, std::uint32_t kind,
                            std::uint64_t target, KrRect anchor) {
    auto& result = *static_cast<Request*>(self);
    result = {id, kind, target, anchor, result.count + 1};
  };
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
  KR_ASSERT(KR_HAS_SLOT(&ui, KrShellUI, request_context_menu));
  ui.request_context_menu(ui.self, KR_MENU_PAGE, 17, {713.5f, 319.25f, 0, 0});
  KR_EXPECT_EQ(request.kind, std::uint32_t{KR_MENU_PAGE});
  KR_EXPECT_EQ(request.target, 17u);
  KR_EXPECT_EQ(request.count, 1);
  const KrMenuItem item{.struct_size = sizeof(KrMenuItem),
                        .command_id = 1,
                        .type = KR_MENU_ITEM_COMMAND,
                        .flags = KR_MENU_ITEM_ENABLED,
                        .label = {"Reload", 6},
                        .submenu = -1};
  KR_ASSERT(KR_HAS_SLOT(&ui, KrShellUI, show_menu_with_glyphs));
  std::string glyph = "\uE72C";
  auto rich_item = item;
  rich_item.glyph = {glyph.data(), glyph.size()};
  const KrMenu menu{sizeof(KrMenu), &rich_item, 1, sizeof(KrMenuItem)};
  ui.show_menu_with_glyphs(ui.self, request.id, &menu, 1, sizeof(KrMenu));
  glyph.clear();
  KR_ASSERT(headless::LastMenu().has_value());
  KR_EXPECT_EQ(headless::LastMenu()->anchor.x, 713.5f);
  KR_EXPECT_EQ(headless::LastMenu()->anchor.y, 319.25f);
  KR_EXPECT_EQ(headless::LastMenu()->menu.menus[0][0].glyph,
               std::string("\uE72C"));
  ui.request_context_menu(ui.self, KR_MENU_SECURITY + 1, 0, {});
  KR_EXPECT_EQ(request.count, 1);
}

}  // namespace
}  // namespace kr
