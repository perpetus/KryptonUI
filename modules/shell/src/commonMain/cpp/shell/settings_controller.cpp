#include "krypton/ui/presentation/settings.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr {
bool BrowserPort::CanEditAppearance() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, set_appearance_prefs) &&
         table_.set_appearance_prefs;
}
bool BrowserPort::CanEditLayout() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, set_layout_prefs) &&
         table_.set_layout_prefs;
}
void BrowserPort::SetAppearancePrefs(const AppearancePrefs& prefs) const {
  if (!CanEditAppearance())
    return;
  const KrAppearancePrefs value{sizeof(KrAppearancePrefs),
                                std::to_underlying(prefs.scheme),
                                std::to_underlying(prefs.menu_material),
                                prefs.flags.bits(),
                                std::to_underlying(prefs.icon_pack),
                                std::to_underlying(prefs.glass_mode)};
  table_.set_appearance_prefs(table_.self, &value);
}
Result<void> Shell::SetAppearancePrefs(const AppearancePrefs& prefs,
                                       bool icon_pack_supplied) {
  auto result = store_.SetAppearancePrefs(
      prefs, icon_pack_supplied && browser_.CanEditAppearance());
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::ShowSettings(std::optional<SettingsSection> requested) {
  DismissBoostEditor();
  const auto section = requested.value_or(store_.ui().settings.section);
  if (OnboardingActive())
    return Fail(ErrorCode::kInvalidArgument, "Onboarding is showing");
  if (section > SettingsSection::kWorkspaces)
    return Fail(ErrorCode::kInvalidArgument, "Invalid settings section");
  OnThemeEditorDismissed();
  OnWorkspaceEditorDismissed();
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  CloseAddress();
  const bool privacy = section == SettingsSection::kPrivacy;
  const bool was_visible = store_.privacy_features().visible;
  store_.EditPrivacyFeatures().SetVisible(
      privacy, browser_.CanEditPrivacyFeatures(), browser_.CanRelaunch());
  const bool settings_was_open = store_.ui().settings.open;
  const auto previous_section = store_.ui().settings.section;
  store_.OpenSettings(section, browser_.CanEditAppearance(),
                      browser_.CanEditLayout());
  auto& settings = store_.EditBrowserSettings();
  settings.editable = browser_.CanEditSettings();
  settings.actionable = browser_.CanInvokeSettings();
  settings.default_actionable = browser_.CanSetDefaultBrowser();
  // Populate capability state before a synchronous host callback.
  store_.SetSettingsCapabilities(
      browser_.CanEditWorkspaces(), browser_.CanChooseSearch(),
      browser_.CanSetDefaultBrowser(), browser_.CanEditSitePermissions(),
      browser_.CanSwitchWorkspaces());
  ScheduleRender();
  if (settings_was_open && previous_section != section)
    browser_.SettingsVisibilityChanged(previous_section, false);
  if (!settings_was_open || previous_section != section)
    browser_.SettingsVisibilityChanged(section, true);
  if (privacy != was_visible)
    browser_.PrivacyFeaturesVisibilityChanged(privacy);
  return {};
}
void Shell::OnSettingsDismissed() {
  const bool was_visible = store_.privacy_features().visible;
  store_.EditPrivacyFeatures().SetVisible(
      false, browser_.CanEditPrivacyFeatures(), browser_.CanRelaunch());
  const auto section = store_.ui().settings.section;
  const bool was_open = store_.ui().settings.open;
  store_.CloseSettings();
  if (was_open)
    browser_.SettingsVisibilityChanged(section, false);
  if (was_visible)
    browser_.PrivacyFeaturesVisibilityChanged(false);
  ScheduleRender();
}
void Shell::OnSettingsDeactivated() {
  const auto& settings = store_.ui().settings;
  if (!settings.open)
    return;
  // The host defers browser destinations and owns the folder picker. Keep the
  // settings subscription until the pending action has actually run.
  const auto& action = store_.browser_settings().action;
  if (action.request && action.open_serial == settings.open_serial)
    return;
  OnSettingsDismissed();
}
void Shell::OnSettingsSectionSelected(SettingsSection section) {
  if (section > SettingsSection::kWorkspaces)
    return;
  if (!store_.ui().settings.open)
    return;
  const bool privacy = section == SettingsSection::kPrivacy;
  const bool was_visible = store_.privacy_features().visible;
  store_.EditPrivacyFeatures().SetVisible(
      privacy, browser_.CanEditPrivacyFeatures(), browser_.CanRelaunch());
  const auto previous_section = store_.ui().settings.section;
  store_.SelectSettingsSection(section);
  if (previous_section != section) {
    browser_.SettingsVisibilityChanged(previous_section, false);
    browser_.SettingsVisibilityChanged(section, true);
  }
  if (privacy != was_visible)
    browser_.PrivacyFeaturesVisibilityChanged(privacy);
  ScheduleRender();
}
void Shell::OnSettingsNavigationToggled() {
  if (!store_.ui().settings.open)
    return;
  store_.ToggleSettingsNavigation();
  ScheduleRender();
}
void Shell::OnAppearanceEdited(AppearanceEdit edit) {
  if (edit.option == AppearanceOption::kIconPack &&
      !store_.ui().settings.icon_pack_editable)
    return;
  if (!store_.ui().settings.open ||
      store_.ui().settings.section != SettingsSection::kAppearance ||
      !store_.ui().settings.appearance_editable)
    return;
  const auto prefs = EditAppearance(store_.appearance_prefs(), edit);
  if (prefs && *prefs != store_.appearance_prefs())
    browser_.SetAppearancePrefs(*prefs);
}
void Shell::OnSettingsLayoutSelected(LayoutChoice choice) {
  if (!store_.ui().settings.open ||
      store_.ui().settings.section != SettingsSection::kAppearance ||
      choice > LayoutChoice::kCollapsed ||
      !store_.ui().settings.layout_editable)
    return;
  const auto prefs = SelectLayout(store_.layout_prefs(), choice);
  if (prefs.flags != store_.layout_prefs().flags)
    browser_.SetLayoutPrefs(prefs);
}
void Shell::OnSettingsLayoutEdited(LayoutEdit edit) {
  const bool workspace_swipe = edit.option == LayoutOption::kWorkspaceSwipe;
  if (!store_.ui().settings.open ||
      store_.ui().settings.section != (workspace_swipe
                                           ? SettingsSection::kWorkspaces
                                           : SettingsSection::kAppearance) ||
      !store_.ui().settings.layout_editable ||
      (workspace_swipe && !browser_.CanSwitchWorkspaces()) ||
      !LayoutOptionAvailable(store_.layout_prefs(), edit.option))
    return;
  const auto prefs = EditLayout(store_.layout_prefs(), edit);
  if (prefs.flags != store_.layout_prefs().flags)
    browser_.SetLayoutPrefs(prefs);
}
}  // namespace kr
