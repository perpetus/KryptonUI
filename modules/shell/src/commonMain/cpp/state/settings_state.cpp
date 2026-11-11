#include "krypton/ui/state/store.hpp"

namespace kr {
Result<void> Store::SetAppearancePrefs(const AppearancePrefs& prefs,
                                       bool icon_pack_editable) {
  if (prefs.scheme > ColorScheme::kDark ||
      prefs.menu_material > MenuMaterialKind::kAcrylicThin ||
      prefs.icon_pack > IconPack::kRadix ||
      prefs.glass_mode > GlassMode::kQuality)
    return Fail(ErrorCode::kInvalidArgument, "Invalid appearance preferences");
  if (appearance_ != prefs) {
    appearance_ = prefs;
    MarkDirty(Dirty::kSettings | Dirty::kTheme | Dirty::kTabPreview);
  }
  if (ui_.settings.icon_pack_editable != icon_pack_editable) {
    ui_.settings.icon_pack_editable = icon_pack_editable;
    MarkDirty(Dirty::kSettings);
  }
  return {};
}
void Store::OpenSettings(SettingsSection section,
                         bool appearance_editable,
                         bool layout_editable) {
  auto& settings = ui_.settings;
  settings.open = true;
  settings.section = section;
  settings.appearance_editable = appearance_editable;
  settings.icon_pack_editable &= appearance_editable;
  settings.layout_editable = layout_editable;
  ++settings.open_serial;
  MarkDirty(Dirty::kSettings);
}
void Store::CloseSettings() {
  if (!ui_.settings.open)
    return;
  ui_.settings.open = false;
  MarkDirty(Dirty::kSettings);
}
void Store::SelectSettingsSection(SettingsSection section) {
  if (section == ui_.settings.section)
    return;
  ui_.settings.section = section;
  MarkDirty(Dirty::kSettings);
}
void Store::ToggleSettingsNavigation() {
  ui_.settings.navigation_collapsed = !ui_.settings.navigation_collapsed;
  MarkDirty(Dirty::kSettings);
}
}  // namespace kr
