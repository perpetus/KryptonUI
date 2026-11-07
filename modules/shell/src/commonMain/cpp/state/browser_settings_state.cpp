#include <utility>

#include "krypton/ui/model/settings.hpp"
namespace kr {
bool BrowserSettingsState::Supports(SettingsControl control) const noexcept {
  return available && (value.controls & SettingsBit(control));
}
bool BrowserSettingsState::Supports(SettingsAction action_id) const noexcept {
  return available &&
         (action_id == SettingsAction::kMakeDefault ? default_actionable
                                                    : actionable) &&
         (value.actions & SettingsBit(action_id));
}
bool BrowserSettingsState::Managed(SettingsControl control) const noexcept {
  return (value.managed & SettingsBit(control)) != 0;
}
bool BrowserSettingsState::Pending(BrowserSetting setting) const noexcept {
  const auto index = std::to_underlying(setting);
  return index < edits.size() && edits[index].request != 0;
}
Result<void> BrowserSettingsState::Apply(BrowserSettingsSnapshot snapshot) {
  constexpr auto controls =
      (std::uint64_t{1} << std::to_underlying(SettingsControl::kCount)) - 1;
  constexpr auto actions =
      (std::uint64_t{1} << std::to_underlying(SettingsAction::kCount)) - 1;
  // Unknown capability/action bits belong to newer hosts; ignore them so
  // appending flags remains ABI-compatible.
  snapshot.controls &= controls;
  snapshot.managed &= controls;
  snapshot.actions &= actions;
  if (snapshot.startup > 2 || (snapshot.managed & ~snapshot.controls))
    return Fail(ErrorCode::kInvalidArgument, "Invalid settings snapshot");
  if (available && snapshot.revision <= value.revision)
    return {};
  value = std::move(snapshot);
  available = true;
  ++revision;
  for (auto& edit : edits) {
    if (!edit.setting)
      continue;
    const auto control = *edit.setting == BrowserSetting::kStartup
                             ? SettingsControl::kStartup
                             : SettingsControl::kAskDownloadLocation;
    if (!Supports(control) || Managed(control))
      edit = {};
  }
  if (action.action && !Supports(*action.action))
    action = {};
  if (!Supports(SettingsControl::kSearch) || Managed(SettingsControl::kSearch))
    pending_engine = 0;
  return {};
}
Result<void> BrowserSettingsState::Complete(std::uint64_t request,
                                            bool success) {
  if (!request)
    return Fail(ErrorCode::kInvalidArgument, "Invalid settings request");
  SettingsOperation* found = action.request == request ? &action : nullptr;
  for (auto& edit : edits)
    if (edit.request == request)
      found = &edit;
  if (!found)
    return {};  // a stale result cannot affect a newer operation
  if (success && found->setting) {
    const auto actual =
        *found->setting == BrowserSetting::kStartup
            ? value.startup
            : static_cast<std::uint32_t>(value.ask_download_location);
    success = actual == found->value;
  }
  *found = {};
  error = !success;
  ++revision;
  return {};
}
}  // namespace kr
