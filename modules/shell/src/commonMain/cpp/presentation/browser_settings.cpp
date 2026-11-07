#include <algorithm>
#include <cmath>

#include "krypton/ui/layout/horizontal_layout.hpp"
#include "krypton/ui/presentation/presenter.hpp"
namespace kr {
bool SettingsAccentUsesDarkText(Color accent) noexcept {
  const auto linear = [](std::uint8_t byte) {
    const double value = byte / 255.0;
    return value <= 0.04045 ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
  };
  const double luminance = 0.2126 * linear(accent.r()) +
                           0.7152 * linear(accent.g()) +
                           0.0722 * linear(accent.b());
  return luminance > 0.179;
}

std::span<const SettingsEntry> SettingsEntries() noexcept {
  static constexpr SettingsEntry entries[]{
      {SettingsSection::kGeneral, UiText::kSettingsDefaultBrowser,
       UiText::kSettingsGeneralHelp, SettingsEntryKind::kDefault},
      {SettingsSection::kGeneral, UiText::kSettingsStartup,
       UiText::kSettingsStartupHelp, SettingsEntryKind::kStartup},
      {SettingsSection::kWorkspaces, UiText::kSettingsCurrentProfile,
       UiText::kSettingsWorkspacesHelp, SettingsEntryKind::kProfile},
      {SettingsSection::kWorkspaces, UiText::kSettingsWorkspaceList,
       UiText::kSettingsWorkspacesHelp, SettingsEntryKind::kWorkspaces},
      {SettingsSection::kWorkspaces, UiText::kSettingsWorkspaceSwipe,
       UiText::kSettingsWorkspaceSwipeHelp, SettingsEntryKind::kWorkspaceSwipe},
      {SettingsSection::kSearch, UiText::kSettingsDefaultSearch,
       UiText::kSettingsSearchHelp, SettingsEntryKind::kSearch},
      {SettingsSection::kDownloads, UiText::kSettingsDownloadLocation,
       UiText::kSettingsDownloadsHelp, SettingsEntryKind::kLocation},
      {SettingsSection::kDownloads, UiText::kSettingsAskDownload,
       UiText::kSettingsDownloadsHelp, SettingsEntryKind::kAskDownload},
      {SettingsSection::kAbout, UiText::kSettingsVersion,
       UiText::kSettingsAboutHelp, SettingsEntryKind::kVersion},
      {SettingsSection::kPrivacy, UiText::kSettingsSitePermissions,
       UiText::kSettingsPrivacyHelp, SettingsEntryKind::kPermissions},
      {SettingsSection::kGeneral, UiText::kSettingsManageStartup,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kManageStartup},
      {SettingsSection::kGeneral, UiText::kSettingsImport,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kImport},
      {SettingsSection::kGeneral, UiText::kSettingsPerformance,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kPerformance},
      {SettingsSection::kGeneral, UiText::kSettingsSystem,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kSystem},
      {SettingsSection::kGeneral, UiText::kSettingsReset,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kReset},
      {SettingsSection::kWorkspaces, UiText::kSettingsManageProfile,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kManageProfile},
      {SettingsSection::kWorkspaces, UiText::kSettingsAddProfile,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kAddProfile},
      {SettingsSection::kWorkspaces, UiText::kSettingsProfilePicker,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kProfilePicker},
      {SettingsSection::kAppearance, UiText::kSettingsFonts,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kFonts},
      {SettingsSection::kPrivacy, UiText::kSettingsClearData,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kClearData},
      {SettingsSection::kPrivacy, UiText::kSettingsCookies,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kCookies},
      {SettingsSection::kPrivacy, UiText::kSettingsSecurity,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kSecurity},
      {SettingsSection::kPrivacy, UiText::kSettingsPrivacyServices,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kPrivacyServices},
      {SettingsSection::kSearch, UiText::kSettingsManageSearch,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kManageSearch},
      {SettingsSection::kDownloads, UiText::kSettingsMoreDownloads,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kDownloads},
      {SettingsSection::kLanguages, UiText::kSettingsLanguageBrowser,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kLanguages},
      {SettingsSection::kAccessibility, UiText::kSettingsAccessibilityBrowser,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kAccessibility},
      {SettingsSection::kAbout, UiText::kSettingsBrowserAbout,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kAbout},
      {SettingsSection::kAbout, UiText::kSettingsCredits,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kCredits},
      {SettingsSection::kAbout, UiText::kSettingsLicense,
       UiText::kSettingsOpenBrowserHelp, SettingsEntryKind::kAction,
       SettingsAction::kLicense},
      {SettingsSection::kAppearance, UiText::kSettingsColorScheme,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kAppearance},
      {SettingsSection::kAppearance, UiText::kSettingsMenuMaterial,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kAppearance},
      {SettingsSection::kAppearance, UiText::kSettingsIcons,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kAppearance},
      {SettingsSection::kAppearance, UiText::kSettingsHoverImages,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kAppearance},
      {SettingsSection::kAppearance, UiText::kSettingsHoverMemory,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kAppearance},
      {SettingsSection::kAppearance, UiText::kSettingsLayout,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kLayout},
      {SettingsSection::kAppearance, UiText::kSettingsSidebarWidth,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kLayout},
      {SettingsSection::kAppearance, UiText::kSettingsSidebarRight,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kLayout},
      {SettingsSection::kAppearance, UiText::kSettingsCenteredAddress,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kLayout},
      {SettingsSection::kAppearance, UiText::kSettingsSearchBarBehavior,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kAppearance},
      {SettingsSection::kAppearance, UiText::kSettingsSearchBarMenuBackdrop,
       UiText::kSettingsMenuHelp, SettingsEntryKind::kAppearance},
      {SettingsSection::kAppearance, UiText::kSettingsMinimalAddress,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kLayout},
      {SettingsSection::kAppearance, UiText::kSettingsRoundedFrame,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kLayout},
      {SettingsSection::kAppearance, UiText::kSettingsCompact,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kLayout},
      {SettingsSection::kAppearance, UiText::kSettingsPinTop,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kLayout},
      {SettingsSection::kAppearance, UiText::kSettingsPinSidebar,
       UiText::kSettingsAppearanceHelp, SettingsEntryKind::kLayout},
  };
  return entries;
}
UiText SettingsDescription(SettingsSection section) noexcept {
  switch (section) {
    case SettingsSection::kGeneral:
      return UiText::kSettingsGeneralHelp;
    case SettingsSection::kWorkspaces:
      return UiText::kSettingsWorkspacesHelp;
    case SettingsSection::kAppearance:
      return UiText::kSettingsAppearanceHelp;
    case SettingsSection::kPrivacy:
      return UiText::kSettingsPrivacyHelp;
    case SettingsSection::kSearch:
      return UiText::kSettingsSearchHelp;
    case SettingsSection::kDownloads:
      return UiText::kSettingsDownloadsHelp;
    case SettingsSection::kLanguages:
      return UiText::kSettingsLanguagesHelp;
    case SettingsSection::kAccessibility:
      return UiText::kSettingsAccessibilityHelp;
    case SettingsSection::kAbout:
      return UiText::kSettingsAboutHelp;
  }
  return UiText::kSettingsEmpty;
}
bool SettingsTextMatches(std::string_view text,
                         std::string_view query) noexcept {
  const auto fold = [](unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
  };
  return query.empty() ||
         std::search(text.begin(), text.end(), query.begin(), query.end(),
                     [&](unsigned char a, unsigned char b) {
                       return fold(a) == fold(b);
                     }) != text.end();
}
void Presenter::PresentSettings(const Store& store) {
  settings_entries_.clear();
  const auto& settings = store.ui().settings;
  const auto& browser = store.browser_settings();
  for (const auto& entry : SettingsEntries()) {
    bool visible = false;
    switch (entry.kind) {
      case SettingsEntryKind::kDefault:
        visible = browser.Supports(SettingsControl::kDefaultBrowser) &&
                  store.browser_services().default_browser_known;
        break;
      case SettingsEntryKind::kStartup:
        visible =
            browser.Supports(SettingsControl::kStartup) &&
            (browser.editable || browser.Managed(SettingsControl::kStartup));
        break;
      case SettingsEntryKind::kProfile:
        visible = browser.Supports(SettingsControl::kProfile);
        break;
      case SettingsEntryKind::kLocation:
        visible = browser.Supports(SettingsControl::kDownloadLocation);
        break;
      case SettingsEntryKind::kAskDownload:
        visible = browser.Supports(SettingsControl::kAskDownloadLocation) &&
                  (browser.editable ||
                   browser.Managed(SettingsControl::kAskDownloadLocation));
        break;
      case SettingsEntryKind::kVersion:
        visible = browser.Supports(SettingsControl::kVersion);
        break;
      case SettingsEntryKind::kSearch:
        visible = browser.Supports(SettingsControl::kSearch) &&
                  !store.browser_services().engines.empty();
        break;
      case SettingsEntryKind::kAction:
        visible = browser.Supports(entry.action);
        break;
      case SettingsEntryKind::kAppearance:
        visible = settings.appearance_editable &&
                  (entry.label != UiText::kSettingsIcons ||
                   settings.icon_pack_editable);
        break;
      case SettingsEntryKind::kLayout:
        visible = settings.layout_editable;
        if (entry.label == UiText::kSettingsSidebarWidth)
          visible &=
              TabLayoutOf(store.layout_prefs().flags) == TabLayout::kVertical;
        for (const auto& option : SettingsLayoutOptions())
          if (option.label == entry.label)
            visible &=
                LayoutOptionAvailable(store.layout_prefs(), option.option);
        break;
      case SettingsEntryKind::kWorkspaces:
        visible = settings.workspace_editable || !store.spaces().empty();
        break;
      case SettingsEntryKind::kWorkspaceSwipe:
        visible = settings.layout_editable && settings.workspace_switchable;
        break;
      case SettingsEntryKind::kPermissions:
        visible = settings.permissions_available;
        break;
    }
    if (visible)
      settings_entries_.push_back(entry);
  }
  settings_sections_.clear();
  for (const auto& section : SettingsSections()) {
    const bool privacy = section.section == SettingsSection::kPrivacy &&
                         !store.privacy_features().features.empty();
    if (privacy ||
        std::ranges::any_of(settings_entries_, [&](const auto& entry) {
          return entry.section == section.section;
        }))
      settings_sections_.push_back(section);
  }
}
}  // namespace kr
