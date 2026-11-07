#include <cstddef>
#include <cstdint>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
struct CreatedUi {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  ~CreatedUi() {
    if (ui.self)
      ui.destroy(ui.self);
    headless::RunPendingTasks();
  }
};

KR_TEST(CreationThemeIsAvailableToThePlatformBeforeFirstRender) {
  headless::Reset();
  KrBrowser browser{.struct_size = sizeof(KrBrowser)};
  KrTheme theme{
      .struct_size = sizeof(KrTheme), .dark = true, .accent = 0xFF445566};
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser, &theme};
  CreatedUi created;
  KR_EXPECT_EQ(kr_ui_create(&params, &created.ui), KR_OK);
  KR_EXPECT(headless::InitialThemeIsDark());
  KR_EXPECT_EQ(headless::RenderCount(), std::size_t{0});
  theme.dark = false;
  headless::RunPendingTasks();
  KR_EXPECT(headless::InitialThemeIsDark());
}

KR_TEST(CreationAcceptsTheOriginalPrefixWithoutReadingTheNewTail) {
  headless::Reset();
  KrBrowser browser{.struct_size = sizeof(KrBrowser)};
  const KrCreateParams params{
      offsetof(KrCreateParams, initial_theme), KR_UI_ABI_VERSION, nullptr,
      &browser, reinterpret_cast<const KrTheme*>(std::uintptr_t{1})};
  CreatedUi created;
  KR_EXPECT_EQ(kr_ui_create(&params, &created.ui), KR_OK);
  KR_EXPECT(!headless::InitialThemeIsDark());
}

KR_TEST(CreationRejectsAnIncompleteInitialThemeWithoutCreatingNativeUI) {
  headless::Reset();
  KrBrowser browser{.struct_size = sizeof(KrBrowser)};
  const KrTheme theme{.struct_size = offsetof(KrTheme, accent), .dark = true};
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser, &theme};
  CreatedUi created;
  KR_EXPECT_EQ(kr_ui_create(&params, &created.ui), KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(created.ui.self == nullptr);
  KR_EXPECT(headless::Delegate() == nullptr);
}
}  // namespace
}  // namespace kr
