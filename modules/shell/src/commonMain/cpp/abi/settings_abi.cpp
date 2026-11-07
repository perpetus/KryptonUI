#include "settings_abi.hpp"

#include <utility>

#include "krypton/ui/shell/shell.hpp"
namespace kr::abi {
namespace {
bool Valid(KrStr s) noexcept {
  return s.data || !s.size;
}
std::string Copy(KrStr s) {
  return s.data ? std::string(s.data, s.size) : std::string{};
}
template <class F>
KrResult Call(F&& function) noexcept {
  try {
    return function() ? KR_OK : KR_ERR_INVALID_ARGUMENT;
  } catch (...) {
    return KR_ERR_PLATFORM;
  }
}
}  // namespace
void FillSettingsUi(KrShellUI& ui) {
  ui.set_suggestion_selection = [](void* self, uint32_t request,
                                   uint64_t generation, size_t index) noexcept {
    return Call([&] {
      return static_cast<Shell*>(self)->SetSuggestionSelection(
          request, generation, index);
    });
  };
  ui.show_notification = [](void* self, KrStr text) noexcept {
    if (!Valid(text))
      return KR_ERR_INVALID_ARGUMENT;
    return Call([&] {
      return static_cast<Shell*>(self)->ShowNotification(
          text.data ? std::string_view(text.data, text.size)
                    : std::string_view{});
    });
  };
  ui.set_settings_snapshot = [](void* self,
                                const KrSettingsSnapshot* value) noexcept {
    if (!value || !KR_HAS_FIELD(value, KrSettingsSnapshot, build) ||
        !Valid(value->profile_name) || !Valid(value->download_location) ||
        !Valid(value->product) || !Valid(value->version) ||
        !Valid(value->build))
      return KR_ERR_INVALID_ARGUMENT;
    return Call([&] {
      return static_cast<Shell*>(self)->SetBrowserSettings(
          {value->revision, value->controls, value->managed, value->actions,
           value->startup, value->ask_download_location,
           Copy(value->profile_name), Copy(value->download_location),
           Copy(value->product), Copy(value->version), Copy(value->build)});
    });
  };
  ui.settings_operation_result =
      [](void* self, const KrSettingsOperationResult* value) noexcept {
        if (!value || !KR_HAS_FIELD(value, KrSettingsOperationResult, result) ||
            value->result < KR_OK || value->result > KR_ERR_PLATFORM)
          return KR_ERR_INVALID_ARGUMENT;
        return Call([&] {
          return static_cast<Shell*>(self)->CompleteSettingsOperation(
              value->request_id, value->result == KR_OK);
        });
      };
}
static_assert(std::to_underlying(SettingsSection::kWorkspaces) ==
              KR_SETTINGS_WORKSPACES);
static_assert(std::to_underlying(SettingsAction::kBrowserSettings) ==
              KR_SETTINGS_ACTION_BROWSER_SETTINGS);
static_assert(std::to_underlying(SettingsControl::kSearch) ==
              KR_SETTINGS_SEARCH_ENGINE);
}  // namespace kr::abi
