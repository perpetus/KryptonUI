#include <algorithm>
#include <utility>

#include "krypton/ui/presentation/settings.hpp"
#include "krypton/ui/shell/shell.hpp"
namespace kr {
Result<void> Shell::ShowNotification(std::string_view text) {
  if (text.empty() || text.size() > 4096)
    return Fail(ErrorCode::kInvalidArgument, "Invalid notification text");
  view_->ShowNotification(text);
  return {};
}
void BrowserPort::SettingsVisibilityChanged(SettingsSection section,
                                            bool visible) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, settings_visibility_changed) &&
      table_.settings_visibility_changed)
    table_.settings_visibility_changed(table_.self, std::to_underlying(section),
                                       visible);
}
bool BrowserPort::CanSwitchWorkspaces() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, switch_space) && table_.switch_space;
}
bool BrowserPort::CanEditSettings() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, edit_setting) && table_.edit_setting;
}
bool BrowserPort::CanInvokeSettings() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, invoke_settings_action) &&
         table_.invoke_settings_action;
}
Result<void> BrowserPort::EditBrowserSetting(const KrSettingsEdit& edit) const {
  if (!CanEditSettings())
    return Fail(ErrorCode::kUnsupported, "Settings edits unavailable");
  return table_.edit_setting(table_.self, &edit) == KR_OK
             ? Result<void>{}
             : Fail(ErrorCode::kPlatform, "Settings edit rejected");
}
Result<void> BrowserPort::InvokeSettingsAction(std::uint64_t request,
                                               SettingsAction action) const {
  if (!CanInvokeSettings())
    return Fail(ErrorCode::kUnsupported, "Settings action unavailable");
  return table_.invoke_settings_action(table_.self, request,
                                       std::to_underlying(action)) == KR_OK
             ? Result<void>{}
             : Fail(ErrorCode::kPlatform, "Settings action rejected");
}
Result<void> Shell::SetBrowserSettings(BrowserSettingsSnapshot snapshot) {
  auto result = store_.EditBrowserSettings().Apply(std::move(snapshot));
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::CompleteSettingsOperation(std::uint64_t request,
                                              bool success) {
  auto& state = store_.EditBrowserSettings();
  const auto action = state.action;
  const auto& settings = store_.ui().settings;
  const bool dismiss = success && action.request == request && action.action &&
                       settings.open &&
                       action.open_serial == settings.open_serial &&
                       *action.action != SettingsAction::kMakeDefault &&
                       *action.action != SettingsAction::kChooseDownloadFolder;
  auto result = state.Complete(request, success);
  if (result) {
    if (dismiss)
      OnSettingsDismissed();
    ScheduleRender();
  }
  return result;
}
void Shell::OnBrowserSettingEdited(BrowserSetting setting,
                                   std::uint32_t value) {
  if (!store_.ui().settings.open ||
      setting > BrowserSetting::kAskDownloadLocation)
    return;
  const auto section = store_.ui().settings.section;
  if ((setting == BrowserSetting::kStartup &&
       section != SettingsSection::kGeneral) ||
      (setting == BrowserSetting::kAskDownloadLocation &&
       section != SettingsSection::kDownloads))
    return;
  const auto control = setting == BrowserSetting::kStartup
                           ? SettingsControl::kStartup
                           : SettingsControl::kAskDownloadLocation;
  auto& state = store_.EditBrowserSettings();
  if (!state.Supports(control) || !state.editable || state.Managed(control) ||
      state.Pending(setting) ||
      value > (setting == BrowserSetting::kStartup ? 2u : 1u))
    return;
  const auto actual =
      setting == BrowserSetting::kStartup
          ? state.value.startup
          : static_cast<std::uint32_t>(state.value.ask_download_location);
  if (value == actual)
    return;
  const auto request = state.next_request++;
  state.edits[std::to_underlying(setting)] = {
      .request = request, .setting = setting, .value = value};
  state.error = false;
  ++state.revision;
  ScheduleRender();
  const KrSettingsEdit edit{sizeof(KrSettingsEdit), request,
                            std::to_underlying(setting), value};
  const auto result = browser_.EditBrowserSetting(edit);
  if (!result) {
    const auto completed = CompleteSettingsOperation(request, false);
    (void)completed;
  }
}
void Shell::OnSettingsAction(SettingsAction action) {
  if (!store_.ui().settings.open || action >= SettingsAction::kCount)
    return;
  auto& state = store_.EditBrowserSettings();
  if (!state.Supports(action) || state.action.request)
    return;
  if (action == SettingsAction::kChooseDownloadFolder &&
      state.Managed(SettingsControl::kDownloadLocation))
    return;
  if (action == SettingsAction::kMakeDefault) {
    const auto& service = store_.browser_services();
    if (!browser_.CanSetDefaultBrowser() || !service.default_browser_known ||
        !service.default_browser.Has(DefaultBrowserFlag::kCanSet) ||
        service.default_browser.HasAny(DefaultBrowserFlag::kIsDefault |
                                       DefaultBrowserFlag::kManaged |
                                       DefaultBrowserFlag::kPending) ||
        state.Managed(SettingsControl::kDefaultBrowser))
      return;
  }

  const auto section = store_.ui().settings.section;
  const bool valid_section =
      action == SettingsAction::kBrowserSettings ||
      (action == SettingsAction::kMakeDefault &&
       section == SettingsSection::kGeneral) ||
      (action == SettingsAction::kChooseDownloadFolder &&
       section == SettingsSection::kDownloads) ||
      std::ranges::any_of(SettingsEntries(), [&](const auto& entry) {
        return entry.section == section && entry.action == action;
      });
  if (!valid_section)
    return;
  const auto request = state.next_request++;
  state.action = {.request = request,
                  .action = action,
                  .open_serial = store_.ui().settings.open_serial};
  state.error = false;
  ++state.revision;
  ScheduleRender();
  const auto result = action == SettingsAction::kMakeDefault
                          ? browser_.RequestDefaultBrowser()
                          : browser_.InvokeSettingsAction(request, action);
  if (!result) {
    const auto completed = CompleteSettingsOperation(request, false);
    (void)completed;
  }
}
void Shell::OnSettingsSearchEngine(SearchEngineId engine) {
  if (!store_.ui().settings.open ||
      store_.ui().settings.section != SettingsSection::kSearch ||
      !browser_.CanChooseSearch())
    return;
  const auto& services = store_.browser_services();
  if (!store_.browser_settings().Supports(SettingsControl::kSearch) ||
      services.SearchManaged() || !services.FindSearchEngine(engine) ||
      services.DefaultEngine() == engine)
    return;
  auto& settings = store_.EditBrowserSettings();
  if (settings.pending_engine || settings.Managed(SettingsControl::kSearch))
    return;
  settings.pending_engine = engine.value;
  settings.error = false;
  ++settings.revision;
  ScheduleRender();
  const auto result = browser_.SetDefaultSearchEngine(engine);
  if (!result) {
    auto& state = store_.EditBrowserSettings();
    state.error = true;
    state.pending_engine = 0;
    ++state.revision;
    ScheduleRender();
  }
}
Result<void> Shell::OnSettingsWorkspaceSubmitted(WorkspaceEditKind kind,
                                                 SpaceId id,
                                                 std::string_view name,
                                                 std::string_view glyph) {
  if (!store_.ui().settings.open ||
      store_.ui().settings.section != SettingsSection::kWorkspaces ||
      !browser_.CanEditWorkspaces())
    return Fail(ErrorCode::kUnsupported, "Workspace editing unavailable");
  const auto* existing = store_.FindSpace(id);
  if ((id && !existing) || kind > WorkspaceEditKind::kRemove ||
      (kind == WorkspaceEditKind::kRemove &&
       (!id || store_.spaces().empty() || store_.spaces().front().id == id)))
    return Fail(ErrorCode::kInvalidArgument, "Workspace cannot be removed");
  const bool can_remove =
      existing && !store_.spaces().empty() && store_.spaces().front().id != id;
  const auto value = PrepareWorkspaceEdit(kind, existing, can_remove, name,
                                          glyph, store_.theme().accent);
  if (!value)
    return std::unexpected(value.error());
  browser_.EditWorkspace(kind, *value);
  return {};
}
}  // namespace kr
