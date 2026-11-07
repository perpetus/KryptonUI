#include <cstddef>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton_ui/abi.h"

namespace kr {
namespace {
PrivacyFeature Feature(std::uint64_t id, PrivacyRisk risk = PrivacyRisk::kLow) {
  return {.id = PrivacyFeatureId{id},
          .risk = risk,
          .title = "Feature",
          .summary = "Summary",
          .data_shared = "Shared",
          .risk_detail = "Risk",
          .when_off = "Off"};
}
struct Host {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  int edits = 0, visible = 0, restarts = 0;
  bool reject = false;
  Host(std::size_t size = sizeof(KrBrowser)) {
    headless::Reset();
    KrBrowser browser{.struct_size = size, .self = this};
    browser.privacy_features_visibility_changed = [](void* self, bool shown) {
      static_cast<Host*>(self)->visible += shown ? 1 : -1;
    };
    browser.set_privacy_feature = [](void* self, KrPrivacyFeatureId, bool) {
      auto& host = *static_cast<Host*>(self);
      ++host.edits;
      return host.reject ? KR_ERR_INVALID_ARGUMENT : KR_OK;
    };
    browser.relaunch_browser = [](void* self) {
      ++static_cast<Host*>(self)->restarts;
    };
    KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION, nullptr,
                          &browser};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  }
  ~Host() { ui.destroy(ui.self); }
  KrResult Publish(std::uint32_t flags = 0,
                   std::uint32_t risk = KR_PRIVACY_RISK_MODERATE) {
    const KrPrivacyFeature feature{sizeof(KrPrivacyFeature),
                                   1,
                                   KR_PRIVACY_GROUP_REMOTE,
                                   risk,
                                   flags,
                                   1,
                                   {"Widevine", 8},
                                   {"Summary", 7},
                                   {"Shared", 6},
                                   {"Risk", 4},
                                   {"Off", 3},
                                   {nullptr, 0}};
    return ui.upsert_privacy_feature(ui.self, &feature);
  }
};
}  // namespace
KR_TEST(PrivacyFeaturesRequireConfirmationAndWaitForHostAcknowledgment) {
  Host host;
  KR_ASSERT(host.Publish() == KR_OK);
  KR_ASSERT(host.ui.show_settings(host.ui.self, KR_SETTINGS_PRIVACY) == KR_OK);
  KR_EXPECT_EQ(host.visible, 1);
  headless::RunPendingTasks();
  headless::Delegate()->OnPrivacyFeatureChanged(PrivacyFeatureId{1}, true);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.edits, 0);
  KR_ASSERT(headless::LastRender().privacy_features.confirmation);
  KR_EXPECT_EQ(
      headless::LastRender().privacy_features.confirmation->data_shared,
      "Shared");
  headless::Delegate()->OnPrivacyFeatureConfirmed(PrivacyFeatureId{1}, true);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.edits, 1);
  KR_EXPECT(!headless::LastRender().privacy_features.rows[0].editable);
  KR_EXPECT(!headless::LastRender().privacy_features.rows[0].value.flags.Has(
      PrivacyFeatureFlag::kEnabled));
  KR_ASSERT(host.Publish(KR_PRIVACY_FEATURE_ENABLED |
                         KR_PRIVACY_FEATURE_RESTART_PENDING) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(headless::LastRender().privacy_features.rows[0].editable);
  KR_EXPECT(headless::LastRender().privacy_features.restart_pending);
  headless::Delegate()->OnPrivacyRelaunch();
  KR_EXPECT_EQ(host.restarts, 1);
  headless::Delegate()->OnSettingsDismissed();
  KR_EXPECT_EQ(host.visible, 0);
}
KR_TEST(PrivacyFeaturesRejectMalformedAbiAndAcceptLongerRecords) {
  Host host;
  KR_EXPECT(host.ui.upsert_privacy_feature(host.ui.self, nullptr) ==
            KR_ERR_INVALID_ARGUMENT);
  KrPrivacyFeature value{};
  value.struct_size = offsetof(KrPrivacyFeature, manage_url);
  KR_EXPECT(host.ui.upsert_privacy_feature(host.ui.self, &value) ==
            KR_ERR_INVALID_ARGUMENT);
  value.struct_size = sizeof(value) + 8;
  value.id = 7;
  value.title = {"QR", 2};
  value.risk = 99;
  KR_EXPECT(host.ui.upsert_privacy_feature(host.ui.self, &value) ==
            KR_ERR_INVALID_ARGUMENT);
  value.risk = KR_PRIVACY_RISK_NONE;
  value.summary = {nullptr, 1};
  KR_EXPECT(host.ui.upsert_privacy_feature(host.ui.self, &value) ==
            KR_ERR_INVALID_ARGUMENT);
  value.summary = {nullptr, 0};
  KR_EXPECT(host.ui.upsert_privacy_feature(host.ui.self, &value) == KR_OK);
  KR_EXPECT(host.ui.remove_privacy_feature(host.ui.self, 7) == KR_OK);
  KR_EXPECT(host.ui.remove_privacy_feature(host.ui.self, 7) ==
            KR_ERR_INVALID_ARGUMENT);
}
KR_TEST(PrivacyFeaturesOldHostCannotEditOrRelaunch) {
  Host host(offsetof(KrBrowser, privacy_features_visibility_changed));
  KR_ASSERT(host.Publish(KR_PRIVACY_FEATURE_RESTART_PENDING) == KR_OK);
  KR_ASSERT(host.ui.show_settings(host.ui.self, KR_SETTINGS_PRIVACY) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(!headless::LastRender().privacy_features.rows[0].editable);
  KR_EXPECT(!headless::LastRender().privacy_features.can_relaunch);
  headless::Delegate()->OnPrivacyFeatureChanged(PrivacyFeatureId{1}, true);
  headless::Delegate()->OnPrivacyRelaunch();
  KR_EXPECT_EQ(host.edits, 0);
  KR_EXPECT_EQ(host.restarts, 0);
}
KR_TEST(PrivacyFeaturesManagedRejectCancelAndDisablePaths) {
  Host host;
  KR_ASSERT(host.Publish(KR_PRIVACY_FEATURE_MANAGED) == KR_OK);
  KR_ASSERT(host.ui.show_settings(host.ui.self, KR_SETTINGS_PRIVACY) == KR_OK);
  headless::Delegate()->OnPrivacyFeatureChanged(PrivacyFeatureId{1}, true);
  KR_EXPECT_EQ(host.edits, 0);
  KR_ASSERT(host.Publish() == KR_OK);
  headless::Delegate()->OnPrivacyFeatureChanged(PrivacyFeatureId{1}, true);
  headless::Delegate()->OnPrivacyFeatureConfirmed(PrivacyFeatureId{1}, false);
  KR_EXPECT_EQ(host.edits, 0);
  KR_ASSERT(host.Publish(KR_PRIVACY_FEATURE_ENABLED) == KR_OK);
  host.reject = true;
  headless::Delegate()->OnPrivacyFeatureChanged(PrivacyFeatureId{1}, false);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.edits, 1);
  KR_EXPECT(headless::LastRender().privacy_features.failed);
  KR_EXPECT(headless::LastRender().privacy_features.rows[0].editable);
  KR_EXPECT(headless::LastRender().privacy_features.rows[0].value.flags.Has(
      PrivacyFeatureFlag::kEnabled));
}
KR_TEST(PrivacyFeaturesGroupOrderAndDirtyReuse) {
  Store store;
  auto& state = store.EditPrivacyFeatures();
  auto a = Feature(2);
  a.order = 7;
  auto b = Feature(1);
  b.group = PrivacyFeatureGroup::kRemote;
  auto c = Feature(3);
  c.order = 2;
  KR_ASSERT(state.Upsert(a));
  KR_ASSERT(state.Upsert(b));
  KR_ASSERT(state.Upsert(c));
  state.SetVisible(true, true, true);
  Presenter presenter;
  const auto& vm = presenter.Present(store, {}, store.TakeDirty());
  KR_EXPECT_EQ(vm.privacy_features.rows[0].value->id.value, 1u);
  KR_EXPECT_EQ(vm.privacy_features.rows[1].value->id.value, 3u);
  const auto* rows = vm.privacy_features.rows.data();
  store.SetNav({});
  const auto& next = presenter.Present(store, {}, store.TakeDirty());
  KR_EXPECT(next.privacy_features.rows.data() == rows);
}
}  // namespace kr
