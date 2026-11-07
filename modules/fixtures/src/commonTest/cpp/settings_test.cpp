#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/testing/test.hpp"

namespace kr::fixtures {
namespace {
struct SettingsCapture {
  std::uint64_t revision = 0, managed = 0, actions = 0;
  std::uint32_t startup = 0, destination = 0;
  int snapshots = 0, completions = 0, engines = 0;
  std::string profile;
};
KrShellUI SettingsStub(SettingsCapture& capture) {
  KrShellUI ui{.struct_size = sizeof(KrShellUI), .self = &capture};
  ui.set_theme = [](void*, const KrTheme*) {};
  ui.set_layout_prefs = [](void*, const KrLayoutPrefs*) {};
  ui.put_image = [](void*, const KrImage*) {};
  ui.set_spaces = [](void*, const KrSpace*, std::size_t, std::size_t,
                     KrSpaceId) {};
  ui.tab_inserted = [](void*, std::size_t, const KrTab*) {};
  ui.set_active_tab = [](void*, KrTabId) {};
  ui.set_nav_state = [](void*, const KrNavState*) {};
  ui.set_actions = [](void*, const KrAction*, std::size_t, std::size_t) {};
  ui.set_settings_snapshot = [](void* self,
                                const KrSettingsSnapshot* value) noexcept {
    auto& c = *static_cast<SettingsCapture*>(self);
    ++c.snapshots;
    c.revision = value->revision;
    c.startup = value->startup;
    c.managed = value->managed;
    c.actions = value->actions;
    c.profile.assign(value->profile_name.data, value->profile_name.size);
    return KR_OK;
  };
  ui.settings_operation_result = [](void* self,
                                    const KrSettingsOperationResult*) noexcept {
    ++static_cast<SettingsCapture*>(self)->completions;
    return KR_OK;
  };
  ui.upsert_search_engine = [](void* self, const KrSearchEngine*) noexcept {
    ++static_cast<SettingsCapture*>(self)->engines;
    return KR_OK;
  };
  ui.show_settings = [](void* self, std::uint32_t section) noexcept {
    static_cast<SettingsCapture*>(self)->destination = section;
    return KR_OK;
  };
  return ui;
}
}  // namespace
KR_TEST(SettingsFixturePublishesOnlyWhileVisibleAndMarksSampleData) {
  MockBrowser browser({}, {});
  SettingsCapture capture;
  browser.Attach(SettingsStub(capture));
  KR_EXPECT_EQ(capture.snapshots, 0);
  const auto& host = browser.table();
  host.settings_visibility_changed(host.self, KR_SETTINGS_SEARCH, true);
  KR_EXPECT_EQ(capture.snapshots, 1);
  KR_EXPECT_EQ(capture.engines, 3);
  KR_EXPECT(capture.profile.find("sample data") != std::string::npos);
  host.settings_visibility_changed(host.self, KR_SETTINGS_SEARCH, false);
  browser.SetSettingsManaged(UINT64_C(1) << KR_SETTINGS_STARTUP);
  KR_EXPECT_EQ(capture.snapshots, 1);
  host.settings_visibility_changed(host.self, KR_SETTINGS_GENERAL, true);
  KR_EXPECT_EQ(capture.snapshots, 2);
  KR_EXPECT(capture.managed & (UINT64_C(1) << KR_SETTINGS_STARTUP));
}
KR_TEST(SettingsFixtureConfirmsValuesBeforeCompletionAndUsesSemanticActions) {
  MockBrowser browser({}, {});
  SettingsCapture capture;
  browser.Attach(SettingsStub(capture));
  const auto& host = browser.table();
  host.settings_visibility_changed(host.self, KR_SETTINGS_GENERAL, true);
  KrSettingsEdit edit{sizeof(KrSettingsEdit), 1, KR_SETTING_STARTUP, 2};
  KR_EXPECT(host.edit_setting(host.self, &edit) == KR_OK);
  KR_EXPECT_EQ(capture.startup, 2u);
  KR_EXPECT_EQ(capture.completions, 1);
  browser.SetSettingsManaged(UINT64_C(1) << KR_SETTINGS_STARTUP);
  edit.value = 1;
  KR_EXPECT(host.edit_setting(host.self, &edit) == KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT_EQ(capture.startup, 2u);
  KR_EXPECT(host.invoke_settings_action(host.self, 2,
                                        KR_SETTINGS_ACTION_COOKIES) == KR_OK);
  KR_EXPECT(browser.last_settings_action() ==
            std::uint32_t{KR_SETTINGS_ACTION_COOKIES});
  KR_EXPECT_EQ(capture.completions, 2);
  KR_EXPECT(browser.ShowSettings(KR_SETTINGS_WORKSPACES));
  KR_EXPECT_EQ(capture.destination, std::uint32_t{KR_SETTINGS_WORKSPACES});
  browser.SetSettingsCapabilities(0, 0);
  KR_EXPECT(
      host.invoke_settings_action(host.self, 3, KR_SETTINGS_ACTION_COOKIES) ==
      KR_ERR_INVALID_ARGUMENT);
}
}  // namespace kr::fixtures
