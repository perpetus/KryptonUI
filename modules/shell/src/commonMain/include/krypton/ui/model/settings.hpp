#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "krypton/ui/core/result.hpp"
#include "krypton/ui/model/glass.hpp"
#include "krypton/ui/model/model.hpp"

namespace kr {
enum class SettingsSection : std::uint32_t {
  kGeneral,
  kAppearance,
  kPrivacy,
  kSearch,
  kDownloads,
  kLanguages,
  kAccessibility,
  kAbout,
  kWorkspaces
};
enum class ColorScheme : std::uint32_t { kSystem, kLight, kDark };
enum class MenuMaterialKind : std::uint32_t {
  kDefault,
  kSolid,
  kMica,
  kAcrylic,
  kAcrylicThin
};
enum class AppearanceFlag : std::uint32_t {
  kHideHoverImages = 1u << 0,
  kHideHoverMemory = 1u << 1,
  kAttachedSearchBar = 1u << 2,
  kSearchBarFloatingOnType = 1u << 3,
  kSearchBarMenuBackdrop = 1u << 4,
  // New tab buttons open a New Tab Page instead of the search bar.
  kNewTabPage = 1u << 5,
  kSwitcherAwakeOnly = 1u << 6
};
template <>
inline constexpr bool kIsFlagEnum<AppearanceFlag> = true;

// Matches KrIconPack; the first is the default.
enum class IconPack : std::uint32_t {
  kFluent,
  kLucide,
  kPhosphor,
  kTabler,
  kRadix
};
inline constexpr std::size_t kIconPackCount = 5;
struct AppearancePrefs {
  ColorScheme scheme = ColorScheme::kSystem;
  MenuMaterialKind menu_material = MenuMaterialKind::kDefault;
  Flags<AppearanceFlag> flags;
  IconPack icon_pack = IconPack::kFluent;
  GlassMode glass_mode = GlassMode::kOff;
  friend bool operator==(const AppearancePrefs&,
                         const AppearancePrefs&) = default;
};
enum class AppearanceOption {
  kColorScheme,
  kMenuMaterial,
  kHoverImages,
  kHoverMemory,
  kIconPack,
  kGlassMode,
  kSearchBarBehavior,
  kSearchBarMenuBackdrop,
  kNewTabBehavior,
  kSwitcherAwakeOnly
};
enum class SearchBarBehavior { kNormal, kFloatingOnType, kAlwaysFloating };
// What New tab opens: Zen's search bar, or Chromium's New Tab Page.
enum class NewTabBehavior : std::uint32_t { kSearchBar, kNewTabPage };
struct AppearanceEdit {
  AppearanceOption option;
  std::uint32_t value = 0;
};
enum class LayoutChoice : std::uint32_t {
  kClassic,
  kCompact,
  kDynamic,
  kSidebar,
  kSidebarAndToolbar,
  kCollapsed
};
enum class LayoutOption {
  kSidebarRight,
  kCenteredAddress,
  kMinimalAddress,
  kRoundedFrame,
  kCompactMode,
  kPinTop,
  kPinSidebar,
  kWorkspaceSwipe
};
struct LayoutEdit {
  LayoutOption option;
  bool enabled = false;
};

enum class BrowserSetting : std::uint32_t { kStartup, kAskDownloadLocation };
enum class SettingsAction : std::uint32_t {
  kMakeDefault,
  kChooseDownloadFolder,
  kManageStartup,
  kImport,
  kPerformance,
  kSystem,
  kReset,
  kManageProfile,
  kAddProfile,
  kProfilePicker,
  kFonts,
  kClearData,
  kCookies,
  kSecurity,
  kPrivacyServices,
  kManageSearch,
  kDownloads,
  kLanguages,
  kAccessibility,
  kAbout,
  kCredits,
  kLicense,
  kBrowserSettings,
  kLocalModels,
  kCount
};
enum class SettingsControl : std::uint32_t {
  kDefaultBrowser,
  kStartup,
  kProfile,
  kDownloadLocation,
  kAskDownloadLocation,
  kVersion,
  kSearch,
  kCount
};
constexpr std::uint64_t SettingsBit(SettingsControl value) noexcept {
  return std::uint64_t{1} << std::to_underlying(value);
}
constexpr std::uint64_t SettingsBit(SettingsAction value) noexcept {
  return std::uint64_t{1} << std::to_underlying(value);
}
struct BrowserSettingsSnapshot {
  std::uint64_t revision = 0, controls = 0, managed = 0, actions = 0;
  std::uint32_t startup = 0;
  bool ask_download_location = false;
  std::string profile_name, download_location, product, version, build;
};
struct SettingsOperation {
  std::uint64_t request = 0;
  std::optional<BrowserSetting> setting;
  std::optional<SettingsAction> action;
  std::uint32_t value = 0;
  std::uint32_t open_serial = 0;
};
struct BrowserSettingsState {
  BrowserSettingsSnapshot value;
  bool available = false, editable = false, actionable = false,
       default_actionable = false;
  std::uint64_t pending_engine = 0;
  std::array<SettingsOperation, 2> edits{};
  SettingsOperation action;
  std::uint64_t next_request = 1, revision = 0;
  bool error = false;
  [[nodiscard]] bool Supports(SettingsControl control) const noexcept;
  [[nodiscard]] bool Supports(SettingsAction action) const noexcept;
  [[nodiscard]] bool Managed(SettingsControl control) const noexcept;
  [[nodiscard]] bool Pending(BrowserSetting setting) const noexcept;
  [[nodiscard]] Result<void> Apply(BrowserSettingsSnapshot snapshot);
  [[nodiscard]] Result<void> Complete(std::uint64_t request, bool success);
};
enum class LocalModelKind : std::uint32_t { kTranslation, kVoice };
enum class LocalModelCommand : std::uint32_t { kDownload, kRemove, kSelect };
enum class LocalModelFlag : std::uint32_t {
  kInstalled = 1u << 0,
  kActive = 1u << 1,
  kCanDownload = 1u << 2,
  kCanRemove = 1u << 3,
  kCanSelect = 1u << 4,
  kShowFirstUse = 1u << 5,
  kSuggested = 1u << 6
};
template <>
inline constexpr bool kIsFlagEnum<LocalModelFlag> = true;
enum class VoiceModelSize : std::uint32_t {
  kTiny = 1,
  kSmall = 2,
  kMedium = 3
};
struct VoiceModelVariant {
  std::string language_tag, language_name, family;
  VoiceModelSize size;
  friend bool operator==(const VoiceModelVariant&,
                         const VoiceModelVariant&) = default;
};
struct LocalModelRow {
  std::uint32_t id;
  LocalModelKind kind;
  Flags<LocalModelFlag> flags;
  int progress;
  std::uint64_t download_bytes;
  std::string name, error;
  std::optional<VoiceModelVariant> voice;
  friend bool operator==(const LocalModelRow&, const LocalModelRow&) = default;
};
struct LocalModelsState {
  std::vector<LocalModelRow> rows;
  bool checking = false;
  friend bool operator==(const LocalModelsState&,
                         const LocalModelsState&) = default;
};

struct SettingsState {
  bool open = false;
  bool navigation_collapsed = false;
  SettingsSection section = SettingsSection::kAppearance;
  std::uint32_t open_serial = 0;
  bool appearance_editable = false, layout_editable = false;
  bool icon_pack_editable = false;
  bool workspace_switchable = false;
  bool workspace_editable = false, search_editable = false,
       default_editable = false, permissions_available = false;
};
}  // namespace kr
