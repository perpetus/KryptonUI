#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
void MockBrowser::PushAppearance() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, set_appearance_prefs) ||
      !ui_.set_appearance_prefs)
    return;
  const auto& prefs = options_.appearance;
  const KrAppearancePrefs value{
      sizeof(KrAppearancePrefs), static_cast<std::uint32_t>(prefs.scheme),
      prefs.menu_material,       prefs.appearance_flags,
      prefs.icon_pack,           prefs.glass_mode};
  (void)ui_.set_appearance_prefs(ui_.self, &value);
}
bool MockBrowser::TryOpenSettings(std::string_view url) {
  constexpr std::string_view prefix = "krypton://settings";
  if (!url.starts_with(prefix) ||
      (url.size() > prefix.size() && url[prefix.size()] != '/'))
    return false;
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, show_settings) ||
      !ui_.show_settings)
    return false;
  const auto path = url.substr(prefix.size());
  const auto section =
      path.starts_with("/privacy") || path.starts_with("/site-permissions")
          ? KR_SETTINGS_PRIVACY
      : path.starts_with("/search")        ? KR_SETTINGS_SEARCH
      : path.starts_with("/downloads")     ? KR_SETTINGS_DOWNLOADS
      : path.starts_with("/languages")     ? KR_SETTINGS_LANGUAGES
      : path.starts_with("/accessibility") ? KR_SETTINGS_ACCESSIBILITY
      : path.starts_with("/workspaces") || path.starts_with("/profiles")
          ? KR_SETTINGS_WORKSPACES
      : path.starts_with("/about") ? KR_SETTINGS_ABOUT
      : path.empty() || path == "/" || path.starts_with("/appearance")
          ? KR_SETTINGS_APPEARANCE
          : KR_SETTINGS_GENERAL;
  return ui_.show_settings(ui_.self, section) == KR_OK;
}
}  // namespace kr::fixtures
