#include "krypton/ui/model/privacy_features.hpp"

#include <algorithm>
#include <utility>

namespace kr {
const PrivacyFeature* PrivacyFeaturesState::Find(
    PrivacyFeatureId id) const noexcept {
  const auto it = std::ranges::find(features, id, &PrivacyFeature::id);
  return it == features.end() ? nullptr : &*it;
}
Result<void> PrivacyFeaturesState::Upsert(PrivacyFeature value) {
  if (!value.id.value || value.group > PrivacyFeatureGroup::kOnDevice ||
      value.risk > PrivacyRisk::kHigh || value.title.empty())
    return Fail(ErrorCode::kInvalidArgument, "Invalid optional feature");
  const auto id = value.id;
  const auto it = std::ranges::find(features, id, &PrivacyFeature::id);
  if (it == features.end())
    features.push_back(std::move(value));
  else
    *it = std::move(value);
  pending.erase(id);
  if (confirmation == id)
    confirmation.reset();
  failed = false;
  ++revision;
  return {};
}
Result<void> PrivacyFeaturesState::Remove(PrivacyFeatureId id) {
  if (!Find(id))
    return Fail(ErrorCode::kNotFound, "Unknown optional feature");
  std::erase_if(features, [id](const auto& value) { return value.id == id; });
  pending.erase(id);
  if (confirmation == id)
    confirmation.reset();
  ++revision;
  return {};
}
bool PrivacyFeaturesState::Editable(PrivacyFeatureId id) const noexcept {
  const auto* item = Find(id);
  return visible && can_edit && item &&
         !item->flags.Has(PrivacyFeatureFlag::kManaged) &&
         !pending.contains(id);
}
bool PrivacyFeaturesState::Request(PrivacyFeatureId id, bool enabled) {
  if (!Editable(id) || confirmation ||
      Find(id)->flags.Has(PrivacyFeatureFlag::kEnabled) == enabled)
    return false;
  failed = false;
  ++revision;
  if (enabled && Find(id)->risk >= PrivacyRisk::kModerate) {
    confirmation = id;
    return false;
  }
  pending.emplace(id, enabled);
  return true;
}
bool PrivacyFeaturesState::Confirm(PrivacyFeatureId id) {
  if (confirmation != id || !Editable(id))
    return false;
  confirmation.reset();
  pending.emplace(id, true);
  ++revision;
  return true;
}
void PrivacyFeaturesState::CancelConfirmation() {
  if (confirmation) {
    confirmation.reset();
    ++revision;
  }
}
void PrivacyFeaturesState::Reject(PrivacyFeatureId id) {
  pending.erase(id);
  failed = true;
  ++revision;
}
void PrivacyFeaturesState::SetVisible(bool value,
                                      bool editable,
                                      bool relaunch) {
  if (visible == value && can_edit == editable && can_relaunch == relaunch)
    return;
  visible = value;
  can_edit = editable;
  can_relaunch = relaunch;
  if (!visible)
    confirmation.reset();
  ++revision;
}
}  // namespace kr
