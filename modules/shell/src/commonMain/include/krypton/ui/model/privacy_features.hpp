#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "krypton/ui/core/flags.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/core/result.hpp"

namespace kr {
using PrivacyFeatureId = Id<struct PrivacyFeatureTag>;
enum class PrivacyFeatureGroup : std::uint32_t {
  kRemote,
  kLocalNetwork,
  kOnDevice
};
enum class PrivacyRisk : std::uint32_t { kNone, kLow, kModerate, kHigh };
enum class PrivacyFeatureFlag : std::uint32_t {
  kEnabled = 1u << 0,
  kNeedsRestart = 1u << 1,
  kRestartPending = 1u << 2,
  kManaged = 1u << 3,
  kOnboarding = 1u << 4
};
template <>
inline constexpr bool kIsFlagEnum<PrivacyFeatureFlag> = true;

struct PrivacyFeature {
  PrivacyFeatureId id;
  PrivacyFeatureGroup group = PrivacyFeatureGroup::kOnDevice;
  PrivacyRisk risk = PrivacyRisk::kNone;
  Flags<PrivacyFeatureFlag> flags;
  std::uint32_t order = 0;
  std::string title, summary, data_shared, risk_detail, when_off, manage_url;
};

// The host owns saved choices. A successful intent remains pending until the
// host publishes its authoritative record, including a rejected/managed choice.
struct PrivacyFeaturesState {
  [[nodiscard]] Result<void> Upsert(PrivacyFeature value);
  [[nodiscard]] Result<void> Remove(PrivacyFeatureId id);
  [[nodiscard]] const PrivacyFeature* Find(PrivacyFeatureId id) const noexcept;
  [[nodiscard]] bool Editable(PrivacyFeatureId id) const noexcept;
  [[nodiscard]] bool Request(PrivacyFeatureId id, bool enabled);
  [[nodiscard]] bool Confirm(PrivacyFeatureId id);
  void Reject(PrivacyFeatureId id);
  void SetVisible(bool value, bool editable, bool relaunch);
  void CancelConfirmation();

  std::vector<PrivacyFeature> features;
  std::unordered_map<PrivacyFeatureId, bool> pending;
  std::optional<PrivacyFeatureId> confirmation;
  bool visible = false, can_edit = false, can_relaunch = false;
  bool failed = false;
  std::uint64_t revision = 0;
};
}  // namespace kr
