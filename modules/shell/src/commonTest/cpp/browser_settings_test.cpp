#include "krypton/testing/test.hpp"
#include "krypton/ui/presentation/presenter.hpp"

namespace kr {
KR_TEST(BrowserSettingsRejectInvalidSnapshotsAndIgnoreOlderRevisions) {
  BrowserSettingsState state;
  KR_EXPECT(!state.Apply({.startup = 3}));
  KR_ASSERT(state.Apply({.controls = UINT64_C(1) << 40}));
  KR_EXPECT_EQ(state.value.controls, UINT64_C(0));
  KR_EXPECT(!state.Apply({.managed = SettingsBit(SettingsControl::kStartup)}));
  KR_ASSERT(state.Apply({.revision = 4,
                         .controls = SettingsBit(SettingsControl::kStartup),
                         .startup = 1}));
  KR_ASSERT(state.Apply({.revision = 3, .startup = 2}));
  KR_EXPECT_EQ(state.value.startup, 1u);
  KR_EXPECT(state.Supports(SettingsControl::kStartup));
}
KR_TEST(BrowserSettingsRequireConfirmedValuesAndIgnoreStaleCompletions) {
  BrowserSettingsState state;
  KR_ASSERT(state.Apply(
      {.revision = 1, .controls = SettingsBit(SettingsControl::kStartup)}));
  state.edits[0] = {
      .request = 7, .setting = BrowserSetting::kStartup, .value = 1};
  KR_ASSERT(state.Complete(6, true));
  KR_EXPECT(state.Pending(BrowserSetting::kStartup));
  KR_ASSERT(state.Complete(7, true));
  KR_EXPECT(state.error);
  KR_EXPECT_EQ(state.value.startup, 0u);
  state.edits[0] = {
      .request = 8, .setting = BrowserSetting::kStartup, .value = 1};
  KR_ASSERT(state.Apply({.revision = 2,
                         .controls = SettingsBit(SettingsControl::kStartup),
                         .startup = 1}));
  KR_ASSERT(state.Complete(8, true));
  KR_EXPECT(!state.error);
  KR_EXPECT(!state.Pending(BrowserSetting::kStartup));
  KR_ASSERT(state.Complete(7, false));
  KR_EXPECT(!state.error);
  KR_EXPECT(!state.Complete(0, true));
}
KR_TEST(BrowserSettingsCapabilityRemovalCancelsPendingControls) {
  BrowserSettingsState state;
  state.actionable = true;
  KR_ASSERT(state.Apply({.revision = 1,
                         .controls = SettingsBit(SettingsControl::kStartup),
                         .actions = SettingsBit(SettingsAction::kImport)}));
  state.edits[0] = {
      .request = 1, .setting = BrowserSetting::kStartup, .value = 1};
  state.action = {.request = 2, .action = SettingsAction::kImport};
  KR_ASSERT(state.Apply({.revision = 2}));
  KR_EXPECT(!state.Pending(BrowserSetting::kStartup));
  KR_EXPECT_EQ(state.action.request, UINT64_C(0));
  KR_EXPECT(!state.Supports(SettingsAction::kImport));
}
KR_TEST(SettingsInventoryHidesUnsupportedControlsAndEmptySections) {
  Store store;
  Presenter presenter;
  store.OpenSettings(SettingsSection::kGeneral, false, false);
  const auto& empty = presenter.Present(store, {}, Dirty::kAll);
  KR_EXPECT(empty.settings.entries.empty());
  KR_EXPECT(empty.settings.sections.empty());
  auto& settings = store.EditBrowserSettings();
  settings.actionable = true;
  KR_ASSERT(settings.Apply(
      {.revision = 1,
       .actions = SettingsBit(SettingsAction::kBrowserSettings)}));
  const auto& fallback = presenter.Present(store, {}, Dirty::kSettings);
  KR_EXPECT(fallback.settings.entries.empty());
  KR_EXPECT(fallback.settings.sections.empty());
  KR_ASSERT(settings.Apply(
      {.revision = 2, .actions = SettingsBit(SettingsAction::kLanguages)}));
  const auto& supported = presenter.Present(store, {}, Dirty::kAll);
  KR_ASSERT(supported.settings.sections.size() == 1);
  KR_EXPECT(supported.settings.sections.front().section ==
            SettingsSection::kLanguages);
  KR_ASSERT(supported.settings.entries.size() == 1);
  KR_EXPECT(supported.settings.entries.front().action ==
            SettingsAction::kLanguages);
  KR_ASSERT(settings.Apply({.revision = 3}));
  KR_EXPECT(
      presenter.Present(store, {}, Dirty::kSettings).settings.sections.empty());
}
KR_TEST(SettingsAccentsChooseReadableForegrounds) {
  KR_EXPECT(SettingsAccentUsesDarkText(Color{0xFFFFFFFF}));
  KR_EXPECT(!SettingsAccentUsesDarkText(Color{0xFF000000}));
}
KR_TEST(SettingsSearchMatchesLocalizedLabelsAndDescriptions) {
  KR_EXPECT(SettingsTextMatches("Download location", "LOCATION"));
  KR_EXPECT(
      SettingsTextMatches("Paramètres de confidentialité", "confidentialité"));
  KR_EXPECT(SettingsTextMatches("إعدادات اللغة", "اللغة"));
  KR_EXPECT(!SettingsTextMatches("Languages", "downloads"));
  Store store;
  KR_ASSERT(store.SetUiText("settings_download_location",
                            "Dossier des téléchargements"));
  KR_EXPECT(SettingsTextMatches(store.text()[UiText::kSettingsDownloadLocation],
                                "téléchargements"));
}
KR_TEST(WorkspaceEditorsShareValidationAndDefaultDeletionProtection) {
  Space first{.id = SpaceId{1}, .name = "Default"};
  KR_EXPECT(!PrepareWorkspaceEdit(WorkspaceEditKind::kRemove, &first, false, "",
                                  "", {}));
  KR_EXPECT(!PrepareWorkspaceEdit(WorkspaceEditKind::kUpsert, nullptr, false,
                                  " \t\r\n", "", {}));
  KR_EXPECT(!PrepareWorkspaceEdit(WorkspaceEditKind::kUpsert, nullptr, false,
                                  std::string_view("bad\0name", 8), "", {}));
  const auto edit = PrepareWorkspaceEdit(WorkspaceEditKind::kUpsert, &first,
                                         false, "  Research  ", "R", {});
  KR_ASSERT(edit);
  KR_EXPECT_EQ(edit->name, std::string("Research"));
  KR_EXPECT(edit->id == first.id);
}
KR_TEST(BrowserServicesSurviveResettingOnboardingDrafts) {
  Store store;
  KR_ASSERT(store.EditBrowserServices().UpsertSearchEngine(
      {SearchEngineId{1}, SearchEngineFlag::kDefault, 0, "Engine", "e", {}}));
  KR_ASSERT(store.EditBrowserServices().SetDefaultBrowser(
      DefaultBrowserFlag::kIsDefault));
  store.EditOnboarding().open = true;
  store.EditOnboarding() = {};
  KR_EXPECT_EQ(store.browser_services().DefaultEngine().value, UINT64_C(1));
  KR_EXPECT(store.browser_services().default_browser.Has(
      DefaultBrowserFlag::kIsDefault));
}
}  // namespace kr
