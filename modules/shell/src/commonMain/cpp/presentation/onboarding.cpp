#include "krypton/ui/presentation/onboarding.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "krypton/ui/layout/search_box_layout.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton/ui/presentation/theme_editor.hpp"

namespace kr {
namespace {
constexpr std::array kQuestions{
    OnboardingStep::kShape,   OnboardingStep::kColor,
    OnboardingStep::kImport,  OnboardingStep::kSearch,
    OnboardingStep::kPrivacy, OnboardingStep::kDefaultBrowser};

[[nodiscard]] constexpr std::uint32_t Bit(OnboardingStep step) noexcept {
  return 1u << std::to_underlying(step);
}

[[nodiscard]] bool OffersPrivacy(const PrivacyFeaturesState& privacy) noexcept {
  return std::ranges::any_of(privacy.features, [](const auto& feature) {
    return feature.flags.Has(PrivacyFeatureFlag::kOnboarding);
  });
}

// The solid presets for the current scheme: the reference's first light and
// first dark pages.
[[nodiscard]] std::size_t PresetPage(bool dark) noexcept {
  return dark ? 2 : 0;
}
}  // namespace

bool OnboardingStepAvailable(OnboardingStep step,
                             const OnboardingState& state,
                             const PrivacyFeaturesState& privacy,
                             const BrowserServicesState& services) noexcept {
  const auto& can = state.capabilities;
  // A moment that already sent its answer stays reachable, so Back can show
  // the result even after the host's state makes it moot.
  const bool committed = (state.committed & Bit(step)) != 0;
  switch (step) {
    case OnboardingStep::kIgnition:
    case OnboardingStep::kReady:
      return true;
    case OnboardingStep::kShape:
      return can.layout;
    case OnboardingStep::kColor:
      return can.theme;
    case OnboardingStep::kImport:
      return can.import && (committed || !state.import_sources.empty() ||
                            state.import.phase != ImportPhase::kIdle);
    case OnboardingStep::kSearch:
      return can.search && !services.engines.empty() &&
             !services.SearchManaged();
    case OnboardingStep::kPrivacy:
      return can.privacy && OffersPrivacy(privacy);
    case OnboardingStep::kDefaultBrowser: {
      const auto flags = services.default_browser;
      return can.default_browser && services.default_browser_known &&
             (committed || state.default_requested ||
              (flags.Has(DefaultBrowserFlag::kCanSet) &&
               !flags.Has(DefaultBrowserFlag::kIsDefault) &&
               !flags.Has(DefaultBrowserFlag::kManaged)));
    }
  }
  return false;
}

OnboardingStep NextOnboardingStep(
    OnboardingStep step,
    int direction,
    const OnboardingState& state,
    const PrivacyFeaturesState& privacy,
    const BrowserServicesState& services) noexcept {
  if (direction == 0)
    return step;
  const int last = static_cast<int>(kOnboardingStepCount) - 1;
  int index = static_cast<int>(std::to_underlying(step));
  // The bookends are always available, so this ends at one of them.
  for (;;) {
    index = std::clamp(index + (direction > 0 ? 1 : -1), 0, last);
    const auto candidate = static_cast<OnboardingStep>(index);
    if (index == 0 || index == last ||
        OnboardingStepAvailable(candidate, state, privacy, services))
      return candidate;
  }
}

std::size_t OnboardingRail(
    const OnboardingState& state,
    const PrivacyFeaturesState& privacy,
    std::array<OnboardingStep, kOnboardingStepCount>& out,
    const BrowserServicesState& services) noexcept {
  std::size_t count = 0;
  for (const auto step : kQuestions) {
    if (step == state.step ||
        OnboardingStepAvailable(step, state, privacy, services))
      out[count++] = step;
  }
  return count;
}

UiText OnboardingTitle(OnboardingStep step) noexcept {
  switch (step) {
    case OnboardingStep::kIgnition:
      return UiText::kOnboardingIgnitionTitle;
    case OnboardingStep::kShape:
      return UiText::kOnboardingShapeTitle;
    case OnboardingStep::kColor:
      return UiText::kOnboardingColorTitle;
    case OnboardingStep::kImport:
      return UiText::kOnboardingImportTitle;
    case OnboardingStep::kSearch:
      return UiText::kOnboardingSearchTitle;
    case OnboardingStep::kPrivacy:
      return UiText::kOnboardingPrivacyTitle;
    case OnboardingStep::kDefaultBrowser:
      return UiText::kOnboardingDefaultTitle;
    case OnboardingStep::kReady:
      return UiText::kOnboardingReadyTitle;
  }
  return UiText::kOnboardingReadyTitle;
}

UiText OnboardingBody(OnboardingStep step) noexcept {
  switch (step) {
    case OnboardingStep::kIgnition:
      return UiText::kOnboardingIgnitionBody;
    case OnboardingStep::kShape:
      return UiText::kOnboardingShapeBody;
    case OnboardingStep::kColor:
      return UiText::kOnboardingColorBody;
    case OnboardingStep::kImport:
      return UiText::kOnboardingImportBody;
    case OnboardingStep::kSearch:
      return UiText::kOnboardingSearchBody;
    case OnboardingStep::kPrivacy:
      return UiText::kOnboardingPrivacyBody;
    case OnboardingStep::kDefaultBrowser:
      return UiText::kOnboardingDefaultBody;
    case OnboardingStep::kReady:
      return UiText::kOnboardingReadyBody;
  }
  return UiText::kOnboardingReadyBody;
}

UiText ImportDataText(ImportData item) noexcept {
  switch (item) {
    case ImportData::kBookmarks:
      return UiText::kOnboardingImportBookmarks;
    case ImportData::kHistory:
      return UiText::kOnboardingImportHistory;
    case ImportData::kPasswords:
      return UiText::kOnboardingImportPasswords;
    case ImportData::kSearchEngines:
      return UiText::kOnboardingImportSearchEngines;
    case ImportData::kAutofill:
      return UiText::kOnboardingImportAutofill;
    case ImportData::kExtensions:
      return UiText::kOnboardingImportExtensions;
  }
  return UiText::kOnboardingImportBookmarks;
}

void FormatOnboardingText(std::string& out,
                          std::string_view pattern,
                          std::string_view first,
                          std::string_view second) {
  out.clear();
  while (!pattern.empty()) {
    const auto at = pattern.find('{');
    if (at == std::string_view::npos || at + 2 >= pattern.size() ||
        pattern[at + 2] != '}' ||
        (pattern[at + 1] != '0' && pattern[at + 1] != '1')) {
      const auto keep = at == std::string_view::npos ? pattern.size() : at + 1;
      out.append(pattern.substr(0, keep));
      pattern.remove_prefix(keep);
      continue;
    }
    out.append(pattern.substr(0, at));
    out.append(pattern[at + 1] == '0' ? first : second);
    pattern.remove_prefix(at + 3);
  }
}

ShellLayout GhostLayout(const WindowState& window,
                        const LayoutPrefs& prefs,
                        const NativeTitleBarMetrics& titlebar) {
  // Two tabs: Dynamic shows its multi-tab strip, as a browsing window would.
  return ComputeLayout(window, prefs, false, false, design::kTokens, titlebar,
                       2);
}

Rect OnboardingColorField(Rect client, const ShellLayout& layout) noexcept {
  return layout.card.empty() ? client : layout.card;
}

Point ColorFieldPoint(Rect field, Point client) noexcept {
  const float side = std::min(field.width, field.height);
  if (!(side > 0))
    return {0.5f, 0.5f};
  return ClampThemePoint(
      {0.5f + (client.x - (field.x + field.width / 2)) / side,
       0.5f + (client.y - (field.y + field.height / 2)) / side});
}

Point ColorFieldPosition(Rect field, Point point) noexcept {
  const float side = std::max(0.0f, std::min(field.width, field.height));
  return {field.x + field.width / 2 + (point.x - 0.5f) * side,
          field.y + field.height / 2 + (point.y - 0.5f) * side};
}

Rect PlaceOnboardingCard(Rect client,
                         const ShellLayout& layout,
                         OnboardingStep step,
                         float measured_width,
                         float measured_height) noexcept {
  const float margin =
      OnboardingRem(design::kTokens.onboarding.card_margin_rem);
  const bool bookend =
      step == OnboardingStep::kIgnition || step == OnboardingStep::kReady;
  Rect region = bookend || layout.card.empty() ? client : layout.card;
  // A page too small for the card lends it the whole window instead.
  if (!bookend && (region.width < measured_width + 2 * margin ||
                   region.height < measured_height + 2 * margin))
    region = client;
  const Rect area = region.Inset(margin);
  const float width = std::clamp(measured_width, 0.0f, area.width);
  const float height = std::clamp(measured_height, 0.0f, area.height);
  const float x = area.x + (area.width - width) / 2;
  const float y = step == OnboardingStep::kColor
                      ? area.bottom() - height
                      : area.y + (area.height - height) / 2;
  return {x, y, width, height};
}

Rect OnboardingReadyTarget(const OnboardingState& state, Rect client) noexcept {
  if (const auto* address = state.Landing(Element::kAddress))
    return address->bounds;
  const auto& overlay = design::kTokens.overlay;
  return ComputeSearchBoxBounds(client,
                                overlay.input_height + 2 * overlay.input_margin,
                                design::kTokens.type.size);
}

void Presenter::PresentOnboarding(const Store& store,
                                  const ShellLayout& layout) {
  const auto& state = store.onboarding();
  const auto& services = store.browser_services();
  const auto& privacy = store.privacy_features();
  auto& out = vm_.onboarding;
  if (!state.open) {
    out = {};
    return;
  }
  const UiTextView text = store.text();
  const std::size_t rail_count =
      OnboardingRail(state, privacy, rail_, services);
  const auto rail = std::span<const OnboardingStep>(rail_).first(rail_count);
  const auto at = std::ranges::find(rail, state.step);
  const std::size_t rail_index = static_cast<std::size_t>(at - rail.begin());
  progress_.clear();
  if (at != rail.end()) {
    FormatOnboardingText(progress_, text[UiText::kOnboardingStepOf],
                         std::to_string(rail_index + 1),
                         std::to_string(rail_count));
  }

  onboarding_privacy_.clear();
  for (const auto& row : privacy_features_) {
    if (row.value->flags.Has(PrivacyFeatureFlag::kOnboarding))
      onboarding_privacy_.push_back(row);
  }

  import_status_.clear();
  const auto* source = state.FindImportSource(
      state.import.source ? state.import.source : state.draft.import_source);
  const std::string_view name =
      source ? std::string_view(source->name) : std::string_view{};
  if (state.import_requested || state.import.phase == ImportPhase::kRunning)
    FormatOnboardingText(import_status_, text[UiText::kOnboardingImporting],
                         name);
  else if (state.import.phase == ImportPhase::kSucceeded)
    FormatOnboardingText(import_status_, text[UiText::kOnboardingImported],
                         name);
  else if (state.import.phase == ImportPhase::kFailed)
    FormatOnboardingText(import_status_, text[UiText::kOnboardingImportFailed],
                         name);

  const bool dark = store.theme().dark;
  std::size_t first = 0, count = 0;
  for (const auto& preset : design::kTokens.editor_presets) {
    const auto page = static_cast<std::size_t>(preset.page);
    if (page < PresetPage(dark))
      ++first;
    else if (page == PresetPage(dark))
      ++count;
  }

  const auto& window = store.window();
  const Rect field = OnboardingColorField(window.client, layout);
  const auto* theme = store.WorkspaceThemeFor(store.active_space());
  const bool finished = state.finishing;
  out = {
      .open = true,
      .finishing = finished,
      .outcome = state.outcome,
      .step = state.step,
      .direction = state.direction,
      .open_serial = state.open_serial,
      .step_serial = state.step_serial,
      .rail = rail,
      .rail_index = rail_index,
      .title = OnboardingTitle(state.step),
      .body = OnboardingBody(state.step),
      .progress = progress_,
      .can_go_back = state.step != OnboardingStep::kIgnition && !finished,
      .layout = SelectedLayout(store.layout_prefs()),
      .layouts = SettingsLayouts(),
      .sidebar_right =
          store.layout_prefs().flags.Has(LayoutFlag::kSidebarRight),
      .side_available = LayoutOptionAvailable(store.layout_prefs(),
                                              LayoutOption::kSidebarRight),
      .ghost = state.ghost.has_value(),
      .ghost_layout = state.ghost ? GhostLayout(window, *state.ghost,
                                                store.ui().native_titlebar)
                                  : ShellLayout{},
      .chrome_veiled = state.chrome_veiled,
      .morph_serial = state.morph_serial,
      .theme = theme,
      .presets =
          std::span{design::kTokens.editor_presets}.subspan(first, count),
      .first_preset = first,
      .color_field = field,
      .color_point = ColorFieldPosition(field, theme && theme->point_count
                                                   ? theme->points[0].position
                                                   : Point{0.5f, 0.5f}),
      .reveal = state.reveal,
      .import_sources = state.import_sources,
      .import_source = state.draft.import_source,
      .import_items = state.draft.import_items,
      .import = state.import,
      .import_requested = state.import_requested,
      .import_status = import_status_,
      .engines = services.engines,
      .engine =
          state.draft.engine ? state.draft.engine : services.DefaultEngine(),
      .privacy = onboarding_privacy_,
      .privacy_confirmation =
          privacy.confirmation ? privacy.Find(*privacy.confirmation) : nullptr,
      .privacy_restart = std::ranges::any_of(
          onboarding_privacy_,
          [](const auto& row) {
            return row.value->flags.Has(PrivacyFeatureFlag::kRestartPending);
          }),
      .can_relaunch = privacy.can_relaunch,
      .default_browser = services.default_browser,
      .default_requested = state.default_requested,
      .landings = state.landings,
      .flight = state.flight,
      .ready_target = OnboardingReadyTarget(state, window.client),
      .ready_radius = state.Landing(Element::kAddress)
                          ? design::kTokens.radius.pill
                          : design::kTokens.radius.overlay,
  };
}
}  // namespace kr
