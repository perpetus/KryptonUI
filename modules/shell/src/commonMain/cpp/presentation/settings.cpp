#include "krypton/ui/presentation/settings.hpp"

#include <algorithm>
#include <cmath>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/layout/horizontal_layout.hpp"

namespace kr {
namespace {
constexpr std::array kSections{
    SettingsSectionItem{SettingsSection::kGeneral, UiText::kSettingsGeneral,
                        L"\uE713"},
    SettingsSectionItem{SettingsSection::kWorkspaces,
                        UiText::kSettingsWorkspaces, L"\uE716"},
    SettingsSectionItem{SettingsSection::kAppearance,
                        UiText::kSettingsAppearance, L"\uE790"},
    SettingsSectionItem{SettingsSection::kPrivacy, UiText::kSettingsPrivacy,
                        L"\uE72E"},
    SettingsSectionItem{SettingsSection::kSearch, UiText::kSettingsSearch,
                        L"\uE721"},
    SettingsSectionItem{SettingsSection::kDownloads, UiText::kSettingsDownloads,
                        L"\uE896"},
    SettingsSectionItem{SettingsSection::kLanguages, UiText::kSettingsLanguages,
                        L"\uE8C1"},
    SettingsSectionItem{SettingsSection::kAccessibility,
                        UiText::kSettingsAccessibility, L"\uE776"},
    SettingsSectionItem{SettingsSection::kAbout, UiText::kSettingsAbout,
                        L"\uE946"}};
constexpr std::array kLayouts{
    LayoutChoiceItem{LayoutChoice::kClassic, UiText::kLayoutClassic,
                     "hr_classic.png", true},
    LayoutChoiceItem{LayoutChoice::kCompact, UiText::kLayoutCompact,
                     "hr_compact.png", true},
    LayoutChoiceItem{LayoutChoice::kDynamic, UiText::kLayoutDynamic,
                     "hr_dyn.png", true},
    LayoutChoiceItem{LayoutChoice::kSidebar, UiText::kLayoutSidebar,
                     "vt_sidebar.png", false},
    LayoutChoiceItem{LayoutChoice::kSidebarAndToolbar,
                     UiText::kLayoutSidebarToolbar, "vt_sidebar_title.png",
                     false},
    LayoutChoiceItem{LayoutChoice::kCollapsed, UiText::kLayoutCollapsed,
                     "vt_compact.png", false}};
void ExplicitTargets(Flags<LayoutFlag>& flags) noexcept {
  if (!flags.Has(LayoutFlag::kCompactTargetsExplicit) &&
      !flags.HasAny(LayoutFlag::kCompactHideSidebar |
                    LayoutFlag::kCompactHideToolbar))
    flags.Set(TabLayoutOf(flags) == TabLayout::kVertical
                  ? LayoutFlag::kCompactHideSidebar
                  : LayoutFlag::kCompactHideToolbar);
  flags.Set(LayoutFlag::kCompactTargetsExplicit);
}
}  // namespace

std::span<const SettingsSectionItem> SettingsSections() noexcept {
  return kSections;
}
Rect SettingsWindowBounds(Rect owner, Rect work_area) noexcept {
  const auto available =
      work_area.Inset(design::kTokens.settings.window_margin);
  const float width =
      std::min(design::kTokens.settings.window_width, available.width);
  const float height =
      std::min(design::kTokens.settings.window_height, available.height);
  return {std::clamp(owner.x + (owner.width - width) / 2, available.x,
                     available.right() - width),
          std::clamp(owner.y + (owner.height - height) / 2, available.y,
                     available.bottom() - height),
          width, height};
}
std::span<const SettingsLayoutOption> SettingsLayoutOptions() noexcept {
  static constexpr std::array options{
      SettingsLayoutOption{LayoutOption::kSidebarRight,
                           UiText::kSettingsSidebarRight},
      SettingsLayoutOption{LayoutOption::kCenteredAddress,
                           UiText::kSettingsCenteredAddress},
      SettingsLayoutOption{LayoutOption::kMinimalAddress,
                           UiText::kSettingsMinimalAddress},
      SettingsLayoutOption{LayoutOption::kRoundedFrame,
                           UiText::kSettingsRoundedFrame},
      SettingsLayoutOption{LayoutOption::kCompactMode,
                           UiText::kSettingsCompact},
      SettingsLayoutOption{LayoutOption::kPinTop, UiText::kSettingsPinTop},
      SettingsLayoutOption{LayoutOption::kPinSidebar,
                           UiText::kSettingsPinSidebar}};
  return options;
}
std::span<const UiText> SettingsColorSchemes() noexcept {
  static constexpr std::array values{UiText::kThemeSystem, UiText::kThemeLight,
                                     UiText::kThemeDark};
  return values;
}
std::span<const UiText> SettingsMenuMaterials() noexcept {
  static constexpr std::array values{
      UiText::kMaterialDefault, UiText::kMaterialSolid, UiText::kMaterialMica,
      UiText::kMaterialAcrylic, UiText::kMaterialAcrylicThin};
  return values;
}
float SettingsCardWidth(float available,
                        float gap,
                        std::size_t columns) noexcept {
  if (!columns || !std::isfinite(available))
    return 0;
  return std::max(0.0f, (available - std::max(0.0f, gap) *
                                         static_cast<float>(columns - 1)) /
                            static_cast<float>(columns));
}
std::span<const LayoutChoiceItem> SettingsLayouts() noexcept {
  return kLayouts;
}
bool SettingsRowFits(float available,
                     float label,
                     float control,
                     float gap) noexcept {
  return std::isfinite(available) && std::isfinite(label) &&
         std::isfinite(control) && std::isfinite(gap) && available > 0 &&
         std::max(0.0f, label) + std::max(0.0f, control) +
                 std::max(0.0f, gap) <=
             available;
}
LayoutChoice SelectedLayout(const LayoutPrefs& prefs) noexcept {
  switch (TabLayoutOf(prefs.flags)) {
    case TabLayout::kClassic:
      return LayoutChoice::kClassic;
    case TabLayout::kCompact:
      return LayoutChoice::kCompact;
    case TabLayout::kDynamic:
      return LayoutChoice::kDynamic;
    case TabLayout::kVertical:
      break;
  }
  switch (BrowserLayoutOf(prefs.flags)) {
    case ToolbarLayout::kSingle:
      return LayoutChoice::kSidebar;
    case ToolbarLayout::kCollapsed:
      return LayoutChoice::kCollapsed;
    case ToolbarLayout::kMultiple:
      return LayoutChoice::kSidebarAndToolbar;
  }
  return LayoutChoice::kSidebarAndToolbar;
}
LayoutPrefs SelectLayout(LayoutPrefs prefs, LayoutChoice choice) noexcept {
  if (choice <= LayoutChoice::kDynamic) {
    const auto mode = choice == LayoutChoice::kClassic   ? TabLayout::kClassic
                      : choice == LayoutChoice::kCompact ? TabLayout::kCompact
                                                         : TabLayout::kDynamic;
    prefs.flags = WithTabLayout(prefs.flags, mode);
  } else {
    const auto mode = choice == LayoutChoice::kSidebar ? ToolbarLayout::kSingle
                      : choice == LayoutChoice::kCollapsed
                          ? ToolbarLayout::kCollapsed
                          : ToolbarLayout::kMultiple;
    prefs.flags = WithTabLayout(WithBrowserLayout(prefs.flags, mode),
                                TabLayout::kVertical);
  }
  return prefs;
}
LayoutPrefs EditLayout(LayoutPrefs prefs, LayoutEdit edit) noexcept {
  switch (edit.option) {
    case LayoutOption::kSidebarRight:
      prefs.flags.Set(LayoutFlag::kSidebarRight, edit.enabled);
      break;
    case LayoutOption::kCenteredAddress:
      prefs.flags.Set(LayoutFlag::kCenteredAddress, edit.enabled);
      break;
    case LayoutOption::kMinimalAddress:
      prefs.flags.Set(LayoutFlag::kMinimalAddress, edit.enabled);
      break;
    case LayoutOption::kRoundedFrame:
      prefs.flags.Set(LayoutFlag::kSquareFrame, !edit.enabled);
      break;
    case LayoutOption::kCompactMode:
      prefs.flags.Set(LayoutFlag::kCompactMode, edit.enabled);
      break;
    case LayoutOption::kWorkspaceSwipe:
      prefs.flags.Set(LayoutFlag::kWorkspaceSwipe, edit.enabled);
      break;
    case LayoutOption::kPinTop:
    case LayoutOption::kPinSidebar:
      ExplicitTargets(prefs.flags);
      prefs.flags.Set(edit.option == LayoutOption::kPinTop
                          ? LayoutFlag::kCompactHideToolbar
                          : LayoutFlag::kCompactHideSidebar,
                      !edit.enabled);
      break;
  }
  return prefs;
}
bool LayoutOptionValue(const LayoutPrefs& prefs, LayoutOption option) noexcept {
  switch (option) {
    case LayoutOption::kSidebarRight:
      return prefs.flags.Has(LayoutFlag::kSidebarRight);
    case LayoutOption::kCenteredAddress:
      return prefs.flags.Has(LayoutFlag::kCenteredAddress);
    case LayoutOption::kMinimalAddress:
      return prefs.flags.Has(LayoutFlag::kMinimalAddress);
    case LayoutOption::kRoundedFrame:
      return !prefs.flags.Has(LayoutFlag::kSquareFrame);
    case LayoutOption::kCompactMode:
      return prefs.flags.Has(LayoutFlag::kCompactMode);
    case LayoutOption::kWorkspaceSwipe:
      return prefs.flags.Has(LayoutFlag::kWorkspaceSwipe);
    case LayoutOption::kPinTop:
    case LayoutOption::kPinSidebar: {
      auto flags = prefs.flags;
      ExplicitTargets(flags);
      return !flags.Has(option == LayoutOption::kPinTop
                            ? LayoutFlag::kCompactHideToolbar
                            : LayoutFlag::kCompactHideSidebar);
    }
  }
  return false;
}
bool LayoutOptionAvailable(const LayoutPrefs& prefs,
                           LayoutOption option) noexcept {
  const bool vertical = TabLayoutOf(prefs.flags) == TabLayout::kVertical;
  if (option == LayoutOption::kSidebarRight ||
      option == LayoutOption::kPinSidebar)
    return vertical;
  if (option == LayoutOption::kCenteredAddress)
    return TabLayoutOf(prefs.flags) != TabLayout::kCompact &&
           (!vertical ||
            BrowserLayoutOf(prefs.flags) != ToolbarLayout::kSingle);
  if (option == LayoutOption::kPinTop)
    return !vertical || BrowserLayoutOf(prefs.flags) != ToolbarLayout::kSingle;
  return true;
}
SearchBarBehavior SelectedSearchBarBehavior(
    const AppearancePrefs& prefs) noexcept {
  if (prefs.flags.Has(AppearanceFlag::kAttachedSearchBar))
    return SearchBarBehavior::kNormal;
  if (prefs.flags.Has(AppearanceFlag::kSearchBarFloatingOnType))
    return SearchBarBehavior::kFloatingOnType;
  return SearchBarBehavior::kAlwaysFloating;
}

Result<AppearancePrefs> EditAppearance(AppearancePrefs prefs,
                                       AppearanceEdit edit) noexcept {
  switch (edit.option) {
    case AppearanceOption::kSearchBarMenuBackdrop:
      if (edit.value > 1)
        return Fail(ErrorCode::kInvalidArgument, "Invalid search bar backdrop");
      prefs.flags.Set(AppearanceFlag::kSearchBarMenuBackdrop, edit.value != 0);
      break;
    case AppearanceOption::kSearchBarBehavior:
      if (edit.value >
          static_cast<std::uint32_t>(SearchBarBehavior::kAlwaysFloating))
        return Fail(ErrorCode::kInvalidArgument, "Invalid URL bar behavior");
      prefs.flags.Set(
          AppearanceFlag::kAttachedSearchBar,
          edit.value == static_cast<std::uint32_t>(SearchBarBehavior::kNormal));
      prefs.flags.Set(AppearanceFlag::kSearchBarFloatingOnType,
                      edit.value == static_cast<std::uint32_t>(
                                        SearchBarBehavior::kFloatingOnType));
      break;
    case AppearanceOption::kColorScheme:
      if (edit.value > std::to_underlying(ColorScheme::kDark))
        return Fail(ErrorCode::kInvalidArgument, "Invalid color scheme");
      prefs.scheme = static_cast<ColorScheme>(edit.value);
      break;
    case AppearanceOption::kMenuMaterial:
      if (edit.value > std::to_underlying(MenuMaterialKind::kAcrylicThin))
        return Fail(ErrorCode::kInvalidArgument, "Invalid menu material");
      prefs.menu_material = static_cast<MenuMaterialKind>(edit.value);
      break;
    case AppearanceOption::kHoverImages:
    case AppearanceOption::kHoverMemory:
      if (edit.value > 1)
        return Fail(ErrorCode::kInvalidArgument, "Invalid appearance toggle");
      prefs.flags.Set(edit.option == AppearanceOption::kHoverImages
                          ? AppearanceFlag::kHideHoverImages
                          : AppearanceFlag::kHideHoverMemory,
                      edit.value == 0);
      break;
    case AppearanceOption::kGlassMode:
      if (edit.value > std::to_underlying(GlassMode::kQuality))
        return Fail(ErrorCode::kInvalidArgument, "Invalid glass mode");
      prefs.glass_mode = static_cast<GlassMode>(edit.value);
      break;
    case AppearanceOption::kIconPack:
      if (edit.value >= kIconPackCount)
        return Fail(ErrorCode::kInvalidArgument, "Invalid icon pack");
      prefs.icon_pack = static_cast<IconPack>(edit.value);
      break;
  }
  return prefs;
}
std::size_t SettingsCardColumns(float available,
                                float measured_minimum,
                                std::size_t count) noexcept {
  if (!count || !std::isfinite(available) || !std::isfinite(measured_minimum) ||
      measured_minimum <= 0)
    return count ? 1u : 0u;
  const float fitted = std::floor(std::max(0.0f, available) / measured_minimum);
  if (fitted >= static_cast<float>(count))
    return count;
  return static_cast<std::size_t>(std::max(1.0f, fitted));
}
}  // namespace kr
