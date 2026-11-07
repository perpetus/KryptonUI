#pragma once

#include <span>

#include "krypton/ui/model/privacy_features.hpp"
#include "krypton/ui/model/ui_text.hpp"

namespace kr {
struct PrivacyFeatureVm {
  const PrivacyFeature* value = nullptr;
  UiText risk_caption;
  bool editable = false;
};
struct PrivacyFeaturesVm {
  std::span<const PrivacyFeatureVm> rows;
  const PrivacyFeature* confirmation = nullptr;
  bool visible = false, restart_pending = false, can_relaunch = false;
  bool failed = false;
  std::uint64_t revision = 0;
};
[[nodiscard]] UiText PrivacyGroupText(PrivacyFeatureGroup group) noexcept;
[[nodiscard]] UiText PrivacyRiskText(PrivacyRisk risk) noexcept;
}  // namespace kr
