#include "krypton/ui/presentation/settings.hpp"

#include <cmath>
#include <limits>

#include "krypton/testing/test.hpp"
#include "krypton/ui/presentation/presenter.hpp"

namespace kr {
KR_TEST(SettingsWindowUsesReferenceScaleAndStaysInsideTheWorkArea) {
  const auto centered =
      SettingsWindowBounds({0, 0, 2000, 1068}, {0, 0, 2000, 1068});
  KR_EXPECT_EQ(centered.width, 880.0f);
  KR_EXPECT_EQ(centered.height, 760.0f);
  KR_EXPECT_EQ(centered.x, 560.0f);
  KR_EXPECT_EQ(centered.y, 154.0f);
  const auto small =
      SettingsWindowBounds({-900, -20, 2000, 1200}, {-800, 0, 800, 600});
  KR_EXPECT_EQ(small.width, 768.0f);
  KR_EXPECT_EQ(small.height, 568.0f);
  KR_EXPECT_EQ(small.x, -784.0f);
  KR_EXPECT_EQ(small.y, 16.0f);
}
KR_TEST(SettingsLayoutChoicesRoundTripAndPreserveIndependentPreferences) {
  const LayoutPrefs saved{.flags = LayoutFlag::kSidebarRight |
                                   LayoutFlag::kCompactMode |
                                   LayoutFlag::kMinimalAddress,
                          .sidebar_width = 317};
  for (const auto& choice : SettingsLayouts()) {
    const auto selected = SelectLayout(saved, choice.choice);
    KR_EXPECT(SelectedLayout(selected) == choice.choice);
    KR_EXPECT(selected.flags.Has(LayoutFlag::kSidebarRight));
    KR_EXPECT(selected.flags.Has(LayoutFlag::kMinimalAddress));
    KR_EXPECT_EQ(selected.sidebar_width, saved.sidebar_width);
    KR_EXPECT(!choice.image.empty());
  }
}
KR_TEST(SettingsAppearanceEditsValidateValuesAndRetainOtherPreferences) {
  AppearancePrefs prefs{.scheme = ColorScheme::kDark,
                        .menu_material = MenuMaterialKind::kAcrylic};
  const auto images =
      EditAppearance(prefs, {AppearanceOption::kHoverImages, 0});
  KR_ASSERT(images.has_value());
  KR_EXPECT(images->flags.Has(AppearanceFlag::kHideHoverImages));
  KR_EXPECT(images->scheme == ColorScheme::kDark);
  KR_EXPECT(images->menu_material == MenuMaterialKind::kAcrylic);
  KR_EXPECT(!EditAppearance(prefs, {AppearanceOption::kColorScheme, 3}));
  KR_EXPECT(!EditAppearance(prefs, {AppearanceOption::kMenuMaterial, 5}));
  KR_EXPECT(!EditAppearance(prefs, {AppearanceOption::kHoverMemory, 2}));
}
KR_TEST(SettingsOnlyAppearanceHasOptionsAndSectionsRetainTheirIdentity) {
  Store store;
  Presenter presenter;
  for (const auto& section : SettingsSections()) {
    store.OpenSettings(section.section, true, true);
    const auto& vm = presenter.Present(store, {}, Dirty::kAll);
    KR_EXPECT(vm.settings.state.open);
    KR_EXPECT(vm.settings.state.section == section.section);
    KR_EXPECT_EQ(vm.settings.appearance_visible,
                 section.section == SettingsSection::kAppearance);
  }
  store.CloseSettings();
  KR_EXPECT(!presenter.Present(store, {}, Dirty::kAll).settings.state.open);
}
KR_TEST(SettingsLayoutModifiersFollowLayoutCapabilitiesAndPinIndependently) {
  auto prefs = SelectLayout({}, LayoutChoice::kCompact);
  KR_EXPECT(!LayoutOptionAvailable(prefs, LayoutOption::kCenteredAddress));
  KR_EXPECT(!LayoutOptionAvailable(prefs, LayoutOption::kSidebarRight));
  prefs = SelectLayout(prefs, LayoutChoice::kSidebarAndToolbar);
  KR_EXPECT(LayoutOptionAvailable(prefs, LayoutOption::kSidebarRight));
  prefs = EditLayout(prefs, {LayoutOption::kPinTop, false});
  prefs = EditLayout(prefs, {LayoutOption::kPinSidebar, true});
  KR_EXPECT(!LayoutOptionValue(prefs, LayoutOption::kPinTop));
  KR_EXPECT(LayoutOptionValue(prefs, LayoutOption::kPinSidebar));
  prefs = EditLayout(prefs, {LayoutOption::kRoundedFrame, false});
  KR_EXPECT(prefs.flags.Has(LayoutFlag::kSquareFrame));
}
KR_TEST(
    SettingsCardsReflowFromMeasuredTranslatedLabelsWithoutFixedBreakpoints) {
  KR_EXPECT_EQ(SettingsCardColumns(600, 200, 3), 3u);
  KR_EXPECT_EQ(SettingsCardColumns(590, 200, 3), 2u);
  KR_EXPECT_EQ(SettingsCardColumns(600, 350, 3), 1u);
  KR_EXPECT_EQ(SettingsCardColumns(600, std::numeric_limits<float>::min(), 3),
               3u);
  KR_EXPECT_EQ(SettingsCardColumns(0, 200, 3), 1u);
  KR_EXPECT_EQ(SettingsCardColumns(600, 200, 0), 0u);
  KR_EXPECT_EQ(SettingsCardWidth(500, 8, 2), 246.0f);
  KR_EXPECT_EQ(SettingsCardWidth(1, 8, 3), 0.0f);
}
KR_TEST(SettingsRowsStackForNarrowWindowsAndLongTranslations) {
  KR_EXPECT(SettingsRowFits(500, 200, 180, 16));
  KR_EXPECT(!SettingsRowFits(300, 200, 180, 16));
  KR_EXPECT(!SettingsRowFits(500, 400, 180, 16));
  KR_EXPECT(SettingsRowFits(396, 200, 180, 16));
  KR_EXPECT(!SettingsRowFits(0, 200, 180, 16));
  KR_EXPECT(
      !SettingsRowFits(500, std::numeric_limits<float>::infinity(), 180, 16));
}
}  // namespace kr
