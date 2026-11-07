#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/layout/shell_layout.hpp"
#include "krypton/ui/model/onboarding.hpp"
#include "krypton/ui/model/privacy_features.hpp"
#include "krypton/ui/model/ui_text.hpp"
#include "krypton/ui/presentation/privacy_features.hpp"
#include "krypton/ui/presentation/settings.hpp"

namespace kr {

// What the onboarding surface draws. Borrows from the Store and Presenter;
// valid only during Render.
struct OnboardingVm {
  bool open = false;
  bool finishing = false;
  OnboardingOutcome outcome = OnboardingOutcome::kCompleted;
  OnboardingStep step = OnboardingStep::kIgnition;
  std::int8_t direction = 1;
  std::uint32_t open_serial = 0, step_serial = 0;
  // The question moments in order; bookends are not on the rail.
  std::span<const OnboardingStep> rail;
  std::size_t rail_index = 0;  // == rail.size() on a bookend
  UiText title = UiText::kOnboardingIgnitionTitle;
  UiText body = UiText::kOnboardingIgnitionBody;
  std::string_view progress;  // "Step 2 of 6", empty on bookends
  bool can_go_back = false;

  // Shape.
  LayoutChoice layout = LayoutChoice::kSidebarAndToolbar;
  std::span<const LayoutChoiceItem> layouts;
  bool sidebar_right = false, side_available = false;
  bool ghost = false;
  ShellLayout ghost_layout;
  bool chrome_veiled = false;
  std::uint32_t morph_serial = 0;

  // Color.
  const WorkspaceTheme* theme = nullptr;  // the draft being painted
  std::span<const design::EditorPreset> presets;
  std::size_t first_preset = 0;
  Rect color_field;   // client DIPs
  Point color_point;  // the primary point, client DIPs
  OnboardingReveal reveal;

  // Import.
  std::span<const ImportSource> import_sources;
  ImportSourceId import_source;
  Flags<ImportData> import_items;
  ImportProgress import;
  bool import_requested = false;
  std::string_view import_status;

  // Search.
  std::span<const SearchEngine> engines;
  SearchEngineId engine;

  // Privacy: the host's rows flagged for onboarding.
  std::span<const PrivacyFeatureVm> privacy;
  const PrivacyFeature* privacy_confirmation = nullptr;
  bool privacy_restart = false, can_relaunch = false;

  // Default browser.
  Flags<DefaultBrowserFlag> default_browser;
  bool default_requested = false;

  // Where answers land, and where the card goes when it finishes.
  std::span<const OnboardingLanding> landings;
  OnboardingFlight flight;
  Rect ready_target;
  float ready_radius = 0;
};

[[nodiscard]] constexpr const design::OnboardingPalette& OnboardingPaletteFor(
    bool dark) noexcept {
  return dark ? design::kTokens.onboarding.dark
              : design::kTokens.onboarding.light;
}
// A token expressed in rem, using the native body size as the root font.
[[nodiscard]] constexpr float OnboardingRem(float rem) noexcept {
  return rem * design::kTokens.type.size;
}

[[nodiscard]] bool OnboardingStepAvailable(
    OnboardingStep step,
    const OnboardingState& state,
    const PrivacyFeaturesState& privacy,
    const BrowserServicesState& services = {}) noexcept;
// The next available step in `direction`, staying put at either end.
[[nodiscard]] OnboardingStep NextOnboardingStep(
    OnboardingStep step,
    int direction,
    const OnboardingState& state,
    const PrivacyFeaturesState& privacy,
    const BrowserServicesState& services = {}) noexcept;
// Available question moments in order. Returns how many were written.
std::size_t OnboardingRail(
    const OnboardingState& state,
    const PrivacyFeaturesState& privacy,
    std::array<OnboardingStep, kOnboardingStepCount>& out,
    const BrowserServicesState& services = {}) noexcept;
[[nodiscard]] UiText OnboardingTitle(OnboardingStep step) noexcept;
[[nodiscard]] UiText OnboardingBody(OnboardingStep step) noexcept;
[[nodiscard]] UiText ImportDataText(ImportData item) noexcept;
// Replaces "{0}" and "{1}" in host-translatable text without throwing.
void FormatOnboardingText(std::string& out,
                          std::string_view pattern,
                          std::string_view first,
                          std::string_view second = {});

// The real window as it would look with `prefs`, for the ghost preview.
[[nodiscard]] ShellLayout GhostLayout(const WindowState& window,
                                      const LayoutPrefs& prefs,
                                      const NativeTitleBarMetrics& titlebar);
// The page area used as the colour field.
[[nodiscard]] Rect OnboardingColorField(Rect client,
                                        const ShellLayout& layout) noexcept;
// Pointer in client DIPs -> normalized theme-board point, and back. The board
// circle is inscribed in the field's shorter side.
[[nodiscard]] Point ColorFieldPoint(Rect field, Point client) noexcept;
[[nodiscard]] Point ColorFieldPosition(Rect field, Point point) noexcept;
// Where the conversation card sits for a measured content size: centred on
// the whole window for the bookends, in the page area otherwise, and along
// the page's lower edge while the page is the colour field.
[[nodiscard]] Rect PlaceOnboardingCard(Rect client,
                                       const ShellLayout& layout,
                                       OnboardingStep step,
                                       float measured_width,
                                       float measured_height) noexcept;
// What the card becomes when it finishes: the address field when it is on
// screen, otherwise the command bar's place.
[[nodiscard]] Rect OnboardingReadyTarget(const OnboardingState& state,
                                         Rect client) noexcept;

}  // namespace kr
