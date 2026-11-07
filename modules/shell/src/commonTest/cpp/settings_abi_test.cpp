#include <cstddef>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
struct SettingsHost {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  KrAppearancePrefs appearance{};
  KrLayoutPrefs layout{.struct_size = sizeof(KrLayoutPrefs)};
  int appearance_calls = 0, layout_calls = 0;
  explicit SettingsHost(bool callbacks = true,
                        std::uint32_t browser_size = sizeof(KrBrowser)) {
    headless::Reset();
    KrBrowser browser{.struct_size = browser_size, .self = this};
    if (callbacks) {
      browser.set_appearance_prefs = [](void* self,
                                        const KrAppearancePrefs* value) {
        auto& host = *static_cast<SettingsHost*>(self);
        host.appearance = *value;
        ++host.appearance_calls;
      };
      browser.set_layout_prefs = [](void* self, const KrLayoutPrefs* value) {
        auto& host = *static_cast<SettingsHost*>(self);
        host.layout = *value;
        ++host.layout_calls;
      };
    }
    const KrCreateParams create{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                nullptr, &browser};
    KR_ASSERT(kr_ui_create(&create, &ui) == KR_OK);
  }
  ~SettingsHost() {
    ui.destroy(ui.self);
    headless::Reset();
  }
};
}  // namespace
KR_TEST(SettingsAbiUserEditsWaitForHostConfirmation) {
  static_assert(KR_UI_ABI_VERSION == 2);
  SettingsHost host;
  KR_ASSERT(host.ui.show_settings(host.ui.self, KR_SETTINGS_APPEARANCE) ==
            KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().settings.state.appearance_editable);
  headless::Delegate()->OnAppearanceEdited(
      {AppearanceOption::kColorScheme, KR_COLOR_DARK});
  KR_EXPECT_EQ(host.appearance_calls, 1);
  KR_EXPECT_EQ(host.appearance.color_scheme,
               static_cast<std::uint32_t>(KR_COLOR_DARK));
  KR_EXPECT(headless::LastRender().settings.appearance.scheme ==
            ColorScheme::kSystem);
  KR_ASSERT(host.ui.set_appearance_prefs(host.ui.self, &host.appearance) ==
            KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().settings.appearance.scheme ==
            ColorScheme::kDark);
  headless::Delegate()->OnSettingsLayoutSelected(LayoutChoice::kDynamic);
  KR_EXPECT_EQ(host.layout_calls, 1);
  KR_EXPECT((host.layout.flags & KR_LAYOUT_TABS_DYNAMIC) != 0);
  headless::Delegate()->OnSettingsSectionSelected(SettingsSection::kPrivacy);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().settings.appearance_visible);
  headless::Delegate()->OnAppearanceEdited(
      {AppearanceOption::kColorScheme, KR_COLOR_LIGHT});
  KR_EXPECT_EQ(host.appearance_calls, 1);
}
KR_TEST(
    SettingsAbiRejectsMalformedSnapshotsAndSupportsHostsWithoutPreferenceCallbacks) {
  SettingsHost host(false);
  KR_EXPECT(host.ui.set_appearance_prefs(host.ui.self, nullptr) ==
            KR_ERR_INVALID_ARGUMENT);
  KrAppearancePrefs prefs{sizeof(KrAppearancePrefs), KR_COLOR_SYSTEM,
                          KR_MENU_MATERIAL_DEFAULT, 0};
  prefs.struct_size =
      offsetof(KrAppearancePrefs, flags) + sizeof(prefs.flags) - 1;
  KR_EXPECT(host.ui.set_appearance_prefs(host.ui.self, &prefs) ==
            KR_ERR_INVALID_ARGUMENT);
  prefs.struct_size = sizeof(prefs);
  prefs.color_scheme = 100;
  KR_EXPECT(host.ui.set_appearance_prefs(host.ui.self, &prefs) ==
            KR_ERR_INVALID_ARGUMENT);
  prefs.color_scheme = KR_COLOR_SYSTEM;
  prefs.menu_material = 100;
  KR_EXPECT(host.ui.set_appearance_prefs(host.ui.self, &prefs) ==
            KR_ERR_INVALID_ARGUMENT);
  prefs.menu_material = KR_MENU_MATERIAL_MICA;
  prefs.flags = KR_APPEARANCE_HIDE_HOVER_MEMORY | (1u << 31);
  KR_ASSERT(host.ui.set_appearance_prefs(host.ui.self, &prefs) == KR_OK);
  KR_EXPECT(host.ui.show_settings(host.ui.self, 100) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_ASSERT(host.ui.show_settings(host.ui.self, KR_SETTINGS_APPEARANCE) ==
            KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().settings.state.appearance_editable);
  KR_EXPECT(!headless::LastRender().settings.state.layout_editable);
  KR_EXPECT(headless::LastRender().settings.appearance.flags.bits() ==
            KR_APPEARANCE_HIDE_HOVER_MEMORY);
  headless::Delegate()->OnSettingsDismissed();
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().settings.state.open);
}
KR_TEST(SettingsAbiDisablesIncompleteAppearanceSlotsButKeepsLegacyLayoutEdits) {
  const auto slot =
      static_cast<std::uint32_t>(offsetof(KrBrowser, set_appearance_prefs));
  for (const auto size : {slot, slot + 1}) {
    SettingsHost host(true, size);
    KR_ASSERT(host.ui.show_settings(host.ui.self, KR_SETTINGS_APPEARANCE) ==
              KR_OK);
    headless::RunPendingTasks();
    KR_EXPECT(!headless::LastRender().settings.state.appearance_editable);
    KR_EXPECT(headless::LastRender().settings.state.layout_editable);
    headless::Delegate()->OnAppearanceEdited(
        {AppearanceOption::kColorScheme, KR_COLOR_DARK});
    KR_EXPECT_EQ(host.appearance_calls, 0);
    headless::Delegate()->OnSettingsLayoutSelected(LayoutChoice::kDynamic);
    KR_EXPECT_EQ(host.layout_calls, 1);
  }
}
}  // namespace kr
