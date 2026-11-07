#include "krypton/ui/shell/shell.hpp"

namespace kr {
bool BrowserPort::CanEditPrivacyFeatures() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, privacy_features_visibility_changed) &&
         table_.privacy_features_visibility_changed &&
         KR_HAS_SLOT(&table_, KrBrowser, set_privacy_feature) &&
         table_.set_privacy_feature;
}
bool BrowserPort::CanRelaunch() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, relaunch_browser) &&
         table_.relaunch_browser;
}
void BrowserPort::PrivacyFeaturesVisibilityChanged(bool visible) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, privacy_features_visibility_changed) &&
      table_.privacy_features_visibility_changed)
    table_.privacy_features_visibility_changed(table_.self, visible);
}
Result<void> BrowserPort::SetPrivacyFeature(PrivacyFeatureId id,
                                            bool enabled) const {
  if (!CanEditPrivacyFeatures())
    return Fail(ErrorCode::kUnsupported, "Optional features unavailable");
  const auto result =
      table_.set_privacy_feature(table_.self, id.value, enabled);
  if (result == KR_OK)
    return {};
  return Fail(result == KR_ERR_UNSUPPORTED ? ErrorCode::kUnsupported
                                           : ErrorCode::kPlatform,
              "Optional feature change rejected");
}
void BrowserPort::Relaunch() const {
  if (CanRelaunch())
    table_.relaunch_browser(table_.self);
}
Result<void> Shell::UpsertPrivacyFeature(PrivacyFeature value) {
  auto result = store_.EditPrivacyFeatures().Upsert(std::move(value));
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::RemovePrivacyFeature(PrivacyFeatureId id) {
  auto result = store_.EditPrivacyFeatures().Remove(id);
  if (result)
    ScheduleRender();
  return result;
}
void Shell::OnPrivacyFeatureChanged(PrivacyFeatureId id, bool enabled) {
  if (store_.EditPrivacyFeatures().Request(id, enabled)) {
    if (!browser_.SetPrivacyFeature(id, enabled))
      store_.EditPrivacyFeatures().Reject(id);
    else if (enabled && OnboardingActive())
      LandOnboarding(Element::kSecurityChip, {});
  }
  ScheduleRender();
}
void Shell::OnPrivacyFeatureConfirmed(PrivacyFeatureId id, bool accepted) {
  if (store_.privacy_features().confirmation != id)
    return;
  if (!accepted) {
    store_.EditPrivacyFeatures().CancelConfirmation();
  } else if (store_.EditPrivacyFeatures().Confirm(id)) {
    if (!browser_.SetPrivacyFeature(id, true))
      store_.EditPrivacyFeatures().Reject(id);
    else if (OnboardingActive())
      LandOnboarding(Element::kSecurityChip, {});
  }
  ScheduleRender();
}
void Shell::OnPrivacyFeatureManage(PrivacyFeatureId id) {
  const auto& state = store_.privacy_features();
  const auto* item = state.Find(id);
  if (state.visible && item && !item->manage_url.empty()) {
    browser_.Navigate(item->manage_url, Disposition::kNewTab);
    if (store_.ui().settings.open)
      OnSettingsDismissed();
  }
}
void Shell::OnPrivacyRelaunch() {
  if (!store_.privacy_features().visible)
    return;
  for (const auto& item : store_.privacy_features().features) {
    if (item.flags.Has(PrivacyFeatureFlag::kRestartPending)) {
      browser_.Relaunch();
      return;
    }
  }
}
}  // namespace kr
