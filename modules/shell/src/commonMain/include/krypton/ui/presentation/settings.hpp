#pragma once

#include <array>
#include <span>
#include <string_view>

#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/core/result.hpp"
#include "krypton/ui/model/settings.hpp"
#include "krypton/ui/model/ui_text.hpp"

namespace kr {
[[nodiscard]] bool SettingsAccentUsesDarkText(Color accent) noexcept;
[[nodiscard]] Rect SettingsWindowBounds(Rect owner, Rect work_area) noexcept;

enum class SettingsEntryKind {
  kDefault,
  kStartup,
  kProfile,
  kLocation,
  kAskDownload,
  kVersion,
  kSearch,
  kAction,
  kAppearance,
  kLayout,
  kWorkspaces,
  kPermissions,
  kWorkspaceSwipe
};
struct SettingsEntry {
  SettingsSection section;
  UiText label, description;
  SettingsEntryKind kind;
  SettingsAction action = SettingsAction::kCount;
};
[[nodiscard]] std::span<const SettingsEntry> SettingsEntries() noexcept;
[[nodiscard]] UiText SettingsDescription(SettingsSection section) noexcept;
[[nodiscard]] bool SettingsTextMatches(std::string_view text,
                                       std::string_view query) noexcept;

struct SettingsSectionItem {
  SettingsSection section;
  UiText label;
  std::wstring_view glyph;
};
struct LayoutChoiceItem {
  LayoutChoice choice;
  UiText label;
  std::string_view image;
  bool horizontal;
};
struct SettingsLayoutOption {
  LayoutOption option;
  UiText label;
};
[[nodiscard]] std::span<const SettingsLayoutOption>
SettingsLayoutOptions() noexcept;
[[nodiscard]] std::span<const UiText> SettingsColorSchemes() noexcept;
[[nodiscard]] std::span<const UiText> SettingsMenuMaterials() noexcept;
[[nodiscard]] SearchBarBehavior SelectedSearchBarBehavior(
    const AppearancePrefs& prefs) noexcept;
[[nodiscard]] float SettingsCardWidth(float available,
                                      float gap,
                                      std::size_t columns) noexcept;
[[nodiscard]] bool SettingsRowFits(float available,
                                   float label,
                                   float control,
                                   float gap) noexcept;

[[nodiscard]] std::span<const SettingsSectionItem> SettingsSections() noexcept;
[[nodiscard]] std::span<const LayoutChoiceItem> SettingsLayouts() noexcept;
[[nodiscard]] LayoutChoice SelectedLayout(const LayoutPrefs& prefs) noexcept;
[[nodiscard]] LayoutPrefs SelectLayout(LayoutPrefs prefs,
                                       LayoutChoice choice) noexcept;
[[nodiscard]] LayoutPrefs EditLayout(LayoutPrefs prefs,
                                     LayoutEdit edit) noexcept;
[[nodiscard]] bool LayoutOptionValue(const LayoutPrefs& prefs,
                                     LayoutOption option) noexcept;
[[nodiscard]] bool LayoutOptionAvailable(const LayoutPrefs& prefs,
                                         LayoutOption option) noexcept;
[[nodiscard]] Result<AppearancePrefs> EditAppearance(
    AppearancePrefs prefs,
    AppearanceEdit edit) noexcept;
[[nodiscard]] std::size_t SettingsCardColumns(float available,
                                              float measured_minimum,
                                              std::size_t count) noexcept;
}  // namespace kr
