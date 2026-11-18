#include <iterator>

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
        mock.PushLocalModels();
      }
    } catch (...) {
    }
  };
  table_.manage_local_model = [](void* self, std::uint32_t kind,
                                 std::uint32_t id,
                                 std::uint32_t command) noexcept -> KrResult {
    try {
      auto& mock = Self(self);
      if (!mock.settings_visible_ ||
          mock.settings_section_ != KR_SETTINGS_LANGUAGES ||
          kind > KR_LOCAL_MODEL_VOICE || id || command > KR_LOCAL_MODEL_SELECT)
        return KR_ERR_INVALID_ARGUMENT;
      const bool voice = kind == KR_LOCAL_MODEL_VOICE;
      auto& installed =
          voice ? mock.voice_installed_ : mock.translation_installed_;
      auto& notice = voice ? mock.voice_notice_ : mock.translation_notice_;
      if (command == KR_LOCAL_MODEL_SELECT) {
        if (!voice || !installed)
          return KR_ERR_INVALID_ARGUMENT;
        mock.voice_selected_ = true;
      } else {
        installed = command == KR_LOCAL_MODEL_DOWNLOAD;
        if (voice && !installed)
          mock.voice_selected_ = false;
      }
      if (command != KR_LOCAL_MODEL_REMOVE)
        notice = true;
      mock.PushLocalModels();
      return KR_OK;
    } catch (...) {
      return KR_ERR_PLATFORM;
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
void MockBrowser::PushLocalModels() {
  if (!attached_ || !settings_visible_ ||
      settings_section_ != KR_SETTINGS_LANGUAGES)
    return;
  const auto flags = [](bool installed, bool notice) -> std::uint32_t {
    auto value = installed
                     ? KR_LOCAL_MODEL_INSTALLED | KR_LOCAL_MODEL_CAN_REMOVE
                     : KR_LOCAL_MODEL_CAN_DOWNLOAD;
    if (!notice)
      value |= KR_LOCAL_MODEL_SHOW_FIRST_USE;
    return value;
  };
  // The fixture's only voice pack stands in for the system language's pack.
  auto voice =
      flags(voice_installed_, voice_notice_) | KR_LOCAL_MODEL_SUGGESTED;
  if (voice_selected_)
    voice |= KR_LOCAL_MODEL_ACTIVE;
  else if (voice_installed_)
    voice |= KR_LOCAL_MODEL_CAN_SELECT;
  const KrLocalModel rows[] = {
      {0,
       KR_LOCAL_MODEL_TRANSLATION,
       flags(translation_installed_, translation_notice_),
       -1,
       53'000'000,
       Borrow("English ↔ French"),
       {}},
      {0,
       KR_LOCAL_MODEL_VOICE,
       voice,
       -1,
       125'000'000,
       Borrow("English — Small"),
       {}}};
  if (KR_HAS_SLOT(&ui_, KrShellUI, set_local_models_v2) &&
      ui_.set_local_models_v2) {
    const KrLocalModelWithVoiceVariant structured[] = {
        {rows[0], {}, {}, {}, 0},
        {rows[1], Borrow("en"), Borrow("English"),
         Borrow("Moonshine v2 Streaming"), KR_VOICE_MODEL_SMALL}};
    const KrLocalModelsSnapshotV2 snapshot{
        sizeof(KrLocalModelsSnapshotV2), structured, std::size(structured),
        sizeof(KrLocalModelWithVoiceVariant), false};
    (void)ui_.set_local_models_v2(ui_.self, &snapshot);
    return;
  }
  if (!KR_HAS_SLOT(&ui_, KrShellUI, set_local_models) || !ui_.set_local_models)
    return;
  const KrLocalModelsSnapshot snapshot{sizeof(KrLocalModelsSnapshot), rows,
                                       std::size(rows), sizeof(KrLocalModel),
                                       false};
  (void)ui_.set_local_models(ui_.self, &snapshot);
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
