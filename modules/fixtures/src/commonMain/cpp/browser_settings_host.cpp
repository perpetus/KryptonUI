#include "krypton/fixtures/mock_browser.hpp"
namespace kr::fixtures {
namespace {
KrStr Borrow(std::string_view value) {
  return {value.data(), value.size()};
}
}  // namespace
bool MockBrowser::ShowSettings(std::uint32_t section) {
  return attached_ && KR_HAS_SLOT(&ui_, KrShellUI, show_settings) &&
         ui_.show_settings && ui_.show_settings(ui_.self, section) == KR_OK;
}
void MockBrowser::InitSettings() {
  table_.settings_visibility_changed = [](void* self, std::uint32_t section,
                                          bool visible) noexcept {
    try {
      auto& mock = Self(self);
      mock.settings_visible_ = visible;
      mock.settings_section_ = section;
      if (visible) {
        mock.PushSettings();
        mock.PushBrowserServices();
      }
    } catch (...) {
    }
  };
  table_.edit_setting = [](void* self,
                           const KrSettingsEdit* edit) noexcept -> KrResult {
    try {
      if (!edit || !KR_HAS_FIELD(edit, KrSettingsEdit, value) ||
          !edit->request_id || edit->setting > KR_SETTING_ASK_DOWNLOAD)
        return KR_ERR_INVALID_ARGUMENT;
      auto& mock = Self(self);
      const auto control = edit->setting == KR_SETTING_STARTUP
                               ? KR_SETTINGS_STARTUP
                               : KR_SETTINGS_ASK_DOWNLOAD;
      if (!mock.settings_visible_ ||
          !(mock.settings_controls_ & (UINT64_C(1) << control)) ||
          (mock.settings_managed_ & (UINT64_C(1) << control)) ||
          edit->value > (edit->setting == KR_SETTING_STARTUP ? 2u : 1u))
        return KR_ERR_INVALID_ARGUMENT;
      if (edit->setting == KR_SETTING_STARTUP)
        mock.settings_startup_ = edit->value;
      else
        mock.settings_ask_download_ = edit->value != 0;
      mock.PushSettings();
      mock.CompleteSettings(edit->request_id, KR_OK);
      return KR_OK;
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
  table_.invoke_settings_action =
      [](void* self, std::uint64_t request,
         std::uint32_t action) noexcept -> KrResult {
    try {
      auto& mock = Self(self);
      if (!mock.settings_visible_ || !request ||
          action > KR_SETTINGS_ACTION_BROWSER_SETTINGS ||
          !(mock.settings_actions_ & (UINT64_C(1) << action)))
        return KR_ERR_INVALID_ARGUMENT;
      mock.last_settings_action_ = action;
      if (action == KR_SETTINGS_ACTION_CHOOSE_DOWNLOAD_FOLDER) {
        mock.settings_download_ = "D:\\Preview downloads (sample data)";
        mock.PushSettings();
      } else if (action == KR_SETTINGS_ACTION_MAKE_DEFAULT) {
        const auto result = mock.table_.request_default_browser(self);
        if (result != KR_OK)
          return result;
      } else
        mock.Log(KR_LOG_INFO,
                 "Preview only: browser settings destination recorded; no "
                 "browser service was invoked.");
      mock.CompleteSettings(request, KR_OK);
      return KR_OK;
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
}
void MockBrowser::PushSettings() {
  if (!attached_ || !settings_visible_ ||
      !KR_HAS_SLOT(&ui_, KrShellUI, set_settings_snapshot) ||
      !ui_.set_settings_snapshot)
    return;
  const KrSettingsSnapshot snapshot{
      sizeof(KrSettingsSnapshot), ++settings_revision_,
      settings_controls_,         settings_managed_,
      settings_actions_,          settings_startup_,
      settings_ask_download_,     Borrow("Preview profile (sample data)"),
      Borrow(settings_download_), Borrow("Krypton preview fixture"),
      Borrow("Sample version"),   Borrow("UI fixture, not the browser host")};
  (void)ui_.set_settings_snapshot(ui_.self, &snapshot);
}
void MockBrowser::CompleteSettings(std::uint64_t request,
                                   KrResult result) const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, settings_operation_result) ||
      !ui_.settings_operation_result)
    return;
  const KrSettingsOperationResult value{sizeof(KrSettingsOperationResult),
                                        request, result};
  (void)ui_.settings_operation_result(ui_.self, &value);
}
void MockBrowser::SetSettingsManaged(std::uint64_t controls) {
  settings_managed_ = controls & settings_controls_;
  PushSettings();
}
void MockBrowser::SetSettingsCapabilities(std::uint64_t controls,
                                          std::uint64_t actions) {
  settings_controls_ = controls;
  settings_actions_ = actions;
  settings_managed_ &= controls;
  PushSettings();
}
}  // namespace kr::fixtures
