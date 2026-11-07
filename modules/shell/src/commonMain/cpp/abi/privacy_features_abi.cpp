#include "privacy_features_abi.hpp"

#include "krypton/ui/core/log.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr::abi {
namespace {
template <typename Function>
KrResult Call(Function&& function) noexcept {
  try {
    return function() ? KR_OK : KR_ERR_INVALID_ARGUMENT;
  } catch (...) {
    Log(LogLevel::kError, "Optional feature callback failed");
    return KR_ERR_PLATFORM;
  }
}
bool Valid(KrStr text) noexcept {
  return text.data || !text.size;
}
std::string Copy(KrStr text) {
  return text.data ? std::string(text.data, text.size) : std::string{};
}
}  // namespace
void FillPrivacyFeaturesUi(KrShellUI& ui) noexcept {
  ui.upsert_privacy_feature = [](void* self,
                                 const KrPrivacyFeature* item) noexcept {
    return Call([&]() -> Result<void> {
      if (!item || !KR_HAS_FIELD(item, KrPrivacyFeature, manage_url) ||
          !Valid(item->title) || !Valid(item->summary) ||
          !Valid(item->data_shared) || !Valid(item->risk_detail) ||
          !Valid(item->when_off) || !Valid(item->manage_url))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete optional feature");
      return static_cast<Shell*>(self)->UpsertPrivacyFeature(
          {PrivacyFeatureId{item->id},
           static_cast<PrivacyFeatureGroup>(item->group),
           static_cast<PrivacyRisk>(item->risk),
           Flags<PrivacyFeatureFlag>::FromBits(item->flags), item->order,
           Copy(item->title), Copy(item->summary), Copy(item->data_shared),
           Copy(item->risk_detail), Copy(item->when_off),
           Copy(item->manage_url)});
    });
  };
  ui.remove_privacy_feature = [](void* self, KrPrivacyFeatureId id) noexcept {
    return Call([&] {
      return static_cast<Shell*>(self)->RemovePrivacyFeature(
          PrivacyFeatureId{id});
    });
  };
}
}  // namespace kr::abi
