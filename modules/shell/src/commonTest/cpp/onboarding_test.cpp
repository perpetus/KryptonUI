#include "krypton/ui/presentation/onboarding.hpp"

#include <cmath>
#include <limits>
#include <string>

#include "krypton/testing/test.hpp"
#include "krypton/ui/layout/search_box_layout.hpp"
#include "krypton/ui/presentation/settings.hpp"

using namespace kr;

namespace {
OnboardingState Open(OnboardingCapabilities can = {}) {
  OnboardingState state;
  state.open = true;
  state.capabilities = can;
  return state;
}
constexpr OnboardingCapabilities kAll{.layout = true,
                                      .theme = true,
                                      .import = true,
                                      .search = true,
                                      .privacy = true,
                                      .default_browser = true};
ImportSource Source(std::uint64_t id, std::uint32_t order = 0) {
  return {ImportSourceId{id}, ImportData::kBookmarks | ImportData::kHistory,
          order, "Sample profile", ImageId{}};
}
SearchEngine Engine(std::uint64_t id,
                    Flags<SearchEngineFlag> flags = {},
                    std::uint32_t order = 0) {
  return {SearchEngineId{id}, flags, order, "Engine", "e", ImageId{}};
}
PrivacyFeaturesState Catalog(bool onboarding) {
  PrivacyFeaturesState privacy;
  PrivacyFeature feature{.id = PrivacyFeatureId{1}, .title = "Feature"};
  if (onboarding)
    feature.flags.Set(PrivacyFeatureFlag::kOnboarding);
  (void)privacy.Upsert(feature);
  return privacy;
}
}  // namespace

KR_TEST(OnboardingSkipsMomentsTheHostCannotOffer) {
  const PrivacyFeaturesState none;
  BrowserServicesState services;
  auto state = Open();
  KR_EXPECT(NextOnboardingStep(OnboardingStep::kIgnition, 1, state, none,
                               services) == OnboardingStep::kReady);
  KR_EXPECT(NextOnboardingStep(OnboardingStep::kReady, -1, state, none,
                               services) == OnboardingStep::kIgnition);
  KR_EXPECT(NextOnboardingStep(OnboardingStep::kReady, 1, state, none,
                               services) == OnboardingStep::kReady);

  state = Open(kAll);
  // Capabilities alone are not enough: each moment needs something to show.
  KR_EXPECT(NextOnboardingStep(OnboardingStep::kColor, 1, state, none,
                               services) == OnboardingStep::kReady);
  KR_ASSERT(state.UpsertImportSource(Source(1)));
  KR_ASSERT(services.UpsertSearchEngine(Engine(1, SearchEngineFlag::kDefault)));
  KR_ASSERT(services.SetDefaultBrowser(DefaultBrowserFlag::kCanSet));
  const auto privacy = Catalog(true);
  OnboardingStep step = OnboardingStep::kIgnition;
  std::vector<OnboardingStep> visited;
  while (step != OnboardingStep::kReady) {
    step = NextOnboardingStep(step, 1, state, privacy, services);
    visited.push_back(step);
  }
  KR_EXPECT_EQ(visited.size(), std::size_t{7});
  KR_EXPECT(visited.front() == OnboardingStep::kShape);
  KR_EXPECT(visited[4] == OnboardingStep::kPrivacy);

  // A catalog without onboarding records, a managed search default and a
  // browser that is already the default all skip their moments.
  KR_ASSERT(services.UpsertSearchEngine(Engine(2, SearchEngineFlag::kManaged)));
  KR_ASSERT(services.SetDefaultBrowser(DefaultBrowserFlag::kIsDefault |
                                       DefaultBrowserFlag::kCanSet));
  KR_EXPECT(NextOnboardingStep(OnboardingStep::kImport, 1, state,
                               Catalog(false),
                               services) == OnboardingStep::kReady);
  KR_EXPECT(NextOnboardingStep(OnboardingStep::kReady, -1, state,
                               Catalog(false),
                               services) == OnboardingStep::kImport);

  // A moment that already sent its answer stays reachable.
  state.committed = 1u << std::to_underlying(OnboardingStep::kDefaultBrowser);
  KR_EXPECT(OnboardingStepAvailable(OnboardingStep::kDefaultBrowser, state,
                                    none, services));
}

KR_TEST(OnboardingRailListsQuestionsAndKeepsTheCurrentOne) {
  BrowserServicesState services;
  auto state = Open({.layout = true, .search = true});
  std::array<OnboardingStep, kOnboardingStepCount> rail{};
  KR_EXPECT_EQ(OnboardingRail(state, {}, rail, services), std::size_t{1});
  KR_EXPECT(rail[0] == OnboardingStep::kShape);
  state.step = OnboardingStep::kSearch;  // engines not published yet
  KR_EXPECT_EQ(OnboardingRail(state, {}, rail, services), std::size_t{2});
  KR_EXPECT(rail[1] == OnboardingStep::kSearch);
}

KR_TEST(OnboardingRejectsMalformedHostRecords) {
  BrowserServicesState services;
  auto state = Open(kAll);
  KR_EXPECT(!state.UpsertImportSource(Source(0)));
  auto unnamed = Source(1);
  unnamed.name.clear();
  KR_EXPECT(!state.UpsertImportSource(unnamed));
  auto empty = Source(1);
  empty.items = {};
  KR_EXPECT(!state.UpsertImportSource(empty));
  auto unknown = Source(1);
  unknown.items = Flags<ImportData>::FromBits(1u << 9);
  KR_EXPECT(!state.UpsertImportSource(unknown));
  KR_EXPECT(state.import_sources.empty());

  KR_ASSERT(state.UpsertImportSource(Source(2, 2)));
  KR_ASSERT(state.UpsertImportSource(Source(1, 1)));
  KR_EXPECT_EQ(state.import_sources.front().id.value, std::uint64_t{1});
  state.draft.import_source = ImportSourceId{2};
  KR_ASSERT(state.RemoveImportSource(ImportSourceId{2}));
  KR_EXPECT(!state.draft.import_source);
  KR_EXPECT(!state.RemoveImportSource(ImportSourceId{2}));

  const float nan = std::numeric_limits<float>::quiet_NaN();
  KR_EXPECT(!state.SetImportProgress(
      {ImportSourceId{1}, ImportPhase::kRunning, nan}));
  KR_EXPECT(!state.SetImportProgress(
      {ImportSourceId{1}, ImportPhase::kRunning, 1.5f}));
  KR_EXPECT(!state.SetImportProgress({{}, ImportPhase::kRunning, 0.5f}));
  KR_EXPECT(!state.SetImportProgress(
      {ImportSourceId{1}, static_cast<ImportPhase>(9), 0.5f}));
  KR_ASSERT(state.SetImportProgress(
      {ImportSourceId{1}, ImportPhase::kRunning, -1.0f}));
  KR_EXPECT(state.import.phase == ImportPhase::kRunning);

  KR_EXPECT(!services.SetDefaultBrowser(
      Flags<DefaultBrowserFlag>::FromBits(1u << 7)));
  KR_EXPECT(!services.default_browser_known);

  // Only one engine is the default.
  KR_ASSERT(
      services.UpsertSearchEngine(Engine(1, SearchEngineFlag::kDefault, 1)));
  KR_ASSERT(
      services.UpsertSearchEngine(Engine(2, SearchEngineFlag::kDefault, 0)));
  KR_EXPECT_EQ(services.DefaultEngine().value, std::uint64_t{2});
  KR_EXPECT_EQ(services.engines.front().id.value, std::uint64_t{2});
  KR_EXPECT(!services.engines.back().flags.Has(SearchEngineFlag::kDefault));
  KR_EXPECT(!services.UpsertSearchEngine(
      Engine(3, Flags<SearchEngineFlag>::FromBits(1u << 5))));
  KR_EXPECT(!services.RemoveSearchEngine(SearchEngineId{9}));
}

KR_TEST(OnboardingLandingsChangeOnlyWhenTheChromeMoves) {
  auto state = Open();
  const Anchor library{AnchorKind::kElement,
                       std::to_underlying(Element::kLibrary),
                       {10, 400, 32, 32}};
  KR_EXPECT(state.SetLanding(library));
  KR_EXPECT(!state.SetLanding(library));
  KR_EXPECT(!state.SetLanding({AnchorKind::kTab, 1, {0, 0, 10, 10}}));
  KR_EXPECT(!state.SetLanding({AnchorKind::kElement,
                               std::to_underlying(Element::kBack),
                               {0, 0, 10, 10}}));
  KR_ASSERT(state.Landing(Element::kLibrary));
  KR_EXPECT(state.Landing(Element::kLibrary)->bounds == library.bounds);
  auto hidden = library;
  hidden.visible = false;
  KR_EXPECT(state.SetLanding(hidden));
  KR_EXPECT(!state.Landing(Element::kLibrary));
}

KR_TEST(OnboardingColorFieldMapsThePageOntoTheBoard) {
  const Rect field{100, 50, 800, 400};
  const Point center = ColorFieldPoint(field, {500, 250});
  KR_EXPECT_EQ(center.x, 0.5f);
  KR_EXPECT_EQ(center.y, 0.5f);
  // The board circle is inscribed in the shorter side.
  const Point edge = ColorFieldPoint(field, {500, 50});
  KR_EXPECT(std::abs(edge.y) < 1e-5f);
  const Point far = ColorFieldPoint(field, {5000, 250});
  KR_EXPECT(std::abs(far.x - 1.0f) < 1e-5f);
  const Point back =
      ColorFieldPosition(field, ColorFieldPoint(field, {560, 290}));
  KR_EXPECT(std::abs(back.x - 560) < 1e-3f);
  KR_EXPECT(std::abs(back.y - 290) < 1e-3f);
  const Point degenerate = ColorFieldPoint({0, 0, 0, 0}, {10, 10});
  KR_EXPECT_EQ(degenerate.x, 0.5f);
}

KR_TEST(OnboardingCardSitsInThePageExceptOnBookends) {
  const Rect client{0, 0, 1200, 800};
  ShellLayout layout;
  layout.card = {300, 60, 880, 720};
  const float margin =
      OnboardingRem(design::kTokens.onboarding.card_margin_rem);
  const Rect ignition =
      PlaceOnboardingCard(client, layout, OnboardingStep::kIgnition, 400, 200);
  KR_EXPECT_EQ(ignition.x, 400.0f);
  KR_EXPECT_EQ(ignition.y, 300.0f);
  const Rect shape =
      PlaceOnboardingCard(client, layout, OnboardingStep::kShape, 400, 200);
  KR_EXPECT_EQ(shape.x, 300 + (880 - 400) / 2.0f);
  KR_EXPECT_EQ(shape.y, 60 + (720 - 200) / 2.0f);
  const Rect color =
      PlaceOnboardingCard(client, layout, OnboardingStep::kColor, 400, 200);
  KR_EXPECT_EQ(color.bottom(), layout.card.bottom() - margin);
  // A page too narrow for the card lends it the window instead.
  layout.card = {900, 60, 280, 720};
  const Rect narrow =
      PlaceOnboardingCard(client, layout, OnboardingStep::kShape, 400, 200);
  KR_EXPECT_EQ(narrow.x, 400.0f);
  // Oversized content is clamped; the card scrolls.
  const Rect tall =
      PlaceOnboardingCard(client, layout, OnboardingStep::kIgnition, 400, 5000);
  KR_EXPECT_EQ(tall.height, 800 - 2 * margin);
}

KR_TEST(OnboardingGhostIsTheRealLayoutForTheChoice) {
  WindowState window;
  window.client = {0, 0, 1280, 800};
  window.scale_factor = 1;
  LayoutPrefs prefs;
  for (const auto& item : SettingsLayouts()) {
    const auto chosen = SelectLayout(prefs, item.choice);
    KR_EXPECT(
        GhostLayout(window, chosen, {}) ==
        ComputeLayout(window, chosen, false, false, design::kTokens, {}, 2));
  }
}

KR_TEST(OnboardingReadyTargetIsTheAddressFieldOrTheCommandBar) {
  auto state = Open();
  const Rect client{0, 0, 1200, 800};
  const auto& overlay = design::kTokens.overlay;
  KR_EXPECT(OnboardingReadyTarget(state, client) ==
            ComputeSearchBoxBounds(
                client, overlay.input_height + 2 * overlay.input_margin,
                design::kTokens.type.size));
  const Rect address{320, 12, 500, 34};
  KR_EXPECT(state.SetLanding(
      {AnchorKind::kElement, std::to_underlying(Element::kAddress), address}));
  KR_EXPECT(OnboardingReadyTarget(state, client) == address);
}

KR_TEST(OnboardingTextFormatsPlaceholdersWithoutThrowing) {
  std::string out;
  FormatOnboardingText(out, "Step {0} of {1}", "2", "6");
  KR_EXPECT_EQ(out, std::string("Step 2 of 6"));
  FormatOnboardingText(out, "{1}/{0}", "a", "b");
  KR_EXPECT_EQ(out, std::string("b/a"));
  FormatOnboardingText(out, "{2} {0 { {", "x");
  KR_EXPECT_EQ(out, std::string("{2} {0 { {"));
  FormatOnboardingText(out, "end {", "x");
  KR_EXPECT_EQ(out, std::string("end {"));
}
