#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/testing/test.hpp"

namespace kr::fixtures {
KR_TEST(MockPrivacyCatalogPublishesOnDemandAndModelsRestart) {
  MockBrowser browser(MockOptions{}, MockHostCallbacks{});
  struct Snapshot {
    int updates = 0;
    std::uint32_t flags = 0;
    std::uint32_t suggestions = 0;
    std::string title, shared;
  } snapshot;
  KrShellUI ui{.struct_size = sizeof(ui), .self = &snapshot};
  ui.set_theme = [](void*, const KrTheme*) {};
  ui.set_layout_prefs = [](void*, const KrLayoutPrefs*) {};
  ui.put_image = [](void*, const KrImage*) {};
  ui.set_spaces = [](void*, const KrSpace*, std::size_t, std::size_t,
                     KrSpaceId) {};
  ui.tab_inserted = [](void*, std::size_t, const KrTab*) {};
  ui.set_active_tab = [](void*, KrTabId) {};
  ui.set_nav_state = [](void*, const KrNavState*) {};
  ui.set_actions = [](void*, const KrAction*, std::size_t, std::size_t) {};
  ui.upsert_privacy_feature = [](void* self, const KrPrivacyFeature* feature) {
    auto& value = *static_cast<Snapshot*>(self);
    ++value.updates;
    if (feature->id == 0x10000)
      value.suggestions = feature->flags;
    if (feature->id != 1)
      return KR_OK;
    value.flags = feature->flags;
    value.title.assign(feature->title.data, feature->title.size);
    value.shared.assign(feature->data_shared.data, feature->data_shared.size);
    return KR_OK;
  };
  browser.Attach(ui);
  const auto& host = browser.table();
  KR_EXPECT_EQ(snapshot.updates, 0);
  host.privacy_features_visibility_changed(host.self, true);
  KR_EXPECT_EQ(snapshot.updates, 4);
  KR_EXPECT_EQ(snapshot.suggestions, 0u);
  KR_EXPECT_EQ(host.set_privacy_feature(host.self, 0x10000, true), KR_OK);
  KR_EXPECT_EQ(snapshot.suggestions,
               static_cast<std::uint32_t>(KR_PRIVACY_FEATURE_ENABLED));
  KR_EXPECT_EQ(host.set_privacy_feature(host.self, 0x10000, false), KR_OK);
  KR_EXPECT_EQ(snapshot.suggestions, 0u);
  KR_EXPECT(snapshot.title.find("Widevine") != std::string::npos);
  KR_EXPECT(snapshot.shared.find("certificate revocation") !=
            std::string::npos);
  KR_EXPECT((snapshot.flags & KR_PRIVACY_FEATURE_ENABLED) == 0);
  KR_EXPECT(host.set_privacy_feature(host.self, 1, true) == KR_OK);
  KR_EXPECT((snapshot.flags & KR_PRIVACY_FEATURE_RESTART_PENDING) != 0);
  host.relaunch_browser(host.self);
  KR_EXPECT((snapshot.flags & KR_PRIVACY_FEATURE_RESTART_PENDING) == 0);
  KR_EXPECT(host.set_privacy_feature(host.self, 8, true) ==
            KR_ERR_INVALID_ARGUMENT);
  host.privacy_features_visibility_changed(host.self, false);
  const auto count = snapshot.updates;
  KR_EXPECT(host.set_privacy_feature(host.self, 1, false) == KR_OK);
  KR_EXPECT_EQ(snapshot.updates, count);
}
}  // namespace kr::fixtures
