#include <algorithm>
#include <tuple>

#include "krypton/ui/presentation/presenter.hpp"

namespace kr {
UiText PrivacyGroupText(PrivacyFeatureGroup group) noexcept {
  switch (group) {
    case PrivacyFeatureGroup::kRemote:
      return UiText::kPrivacyRemote;
    case PrivacyFeatureGroup::kLocalNetwork:
      return UiText::kPrivacyLocalNetwork;
    case PrivacyFeatureGroup::kOnDevice:
      return UiText::kPrivacyOnDevice;
  }
  return UiText::kPrivacyOnDevice;
}
UiText PrivacyRiskText(PrivacyRisk risk) noexcept {
  switch (risk) {
    case PrivacyRisk::kNone:
      return UiText::kPrivacyRiskNone;
    case PrivacyRisk::kLow:
      return UiText::kPrivacyRiskLow;
    case PrivacyRisk::kModerate:
      return UiText::kPrivacyRiskModerate;
    case PrivacyRisk::kHigh:
      return UiText::kPrivacyRiskHigh;
  }
  return UiText::kPrivacyRiskHigh;
}
void Presenter::PresentPrivacyFeatures(const PrivacyFeaturesState& state) {
  privacy_features_.clear();
  bool restart = false;
  for (const auto& item : state.features) {
    privacy_features_.push_back(
        {&item, PrivacyRiskText(item.risk), state.Editable(item.id)});
    restart |= item.flags.Has(PrivacyFeatureFlag::kRestartPending);
  }
  std::ranges::sort(privacy_features_, [](const auto& a, const auto& b) {
    return std::tie(a.value->group, a.value->order, a.value->id.value) <
           std::tie(b.value->group, b.value->order, b.value->id.value);
  });
  vm_.privacy_features = {.rows = privacy_features_,
                          .confirmation = state.confirmation
                                              ? state.Find(*state.confirmation)
                                              : nullptr,
                          .visible = state.visible,
                          .restart_pending = restart,
                          .can_relaunch = state.can_relaunch,
                          .failed = state.failed,
                          .revision = state.revision};
}
}  // namespace kr
