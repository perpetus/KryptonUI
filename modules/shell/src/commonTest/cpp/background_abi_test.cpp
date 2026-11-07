#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {

KR_TEST(BackdropUpdatesCoalesceAndSkipUnchangedColors) {
  headless::Reset();
  struct Background {
    int calls = 0;
    KrColor start = 0;
    KrColor end = 0;
  } background;
  KrBrowser browser{};
  browser.struct_size = sizeof(browser);
  browser.self = &background;
  browser.set_window_background = [](void* self, KrColor start, KrColor end) {
    auto& value = *static_cast<Background*>(self);
    value = {value.calls + 1, start, end};
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
  KrTheme theme{
      .struct_size = sizeof(KrTheme), .dark = true, .accent = 0xFF5D566A};
  ui.set_theme(ui.self, &theme);
  ui.set_theme(ui.self, &theme);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(background.calls, 1);
  KR_EXPECT_EQ(background.start, headless::LastRender().gradient_start.argb);
  ui.set_theme(ui.self, &theme);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(background.calls, 1);
  theme.accent = 0xFF447464;
  ui.set_theme(ui.self, &theme);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(background.calls, 2);
}

}  // namespace
}  // namespace kr
