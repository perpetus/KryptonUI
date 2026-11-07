#include <cstddef>
#include <vector>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton_ui/abi.h"

using namespace kr;

namespace {
struct Host {
  KrShellUI ui{.struct_size = sizeof(KrShellUI)};
  int visible = 0, privacy_visible = 0, layouts = 0, imports = 0,
      default_requests = 0, finished = 0;
  std::vector<bool> theme_commits;
  KrLayoutPrefs layout{.struct_size = sizeof(KrLayoutPrefs)};
  KrImportSourceId import_source = 0;
  std::uint32_t import_items = 0;
  KrSearchEngineId engine = 0;
  KrOnboardingResult result{};
  bool complete_import_inline = false;

  explicit Host(bool onboarding = true,
                std::size_t browser_size = sizeof(KrBrowser)) {
    headless::Reset();
    KrBrowser browser{.struct_size = browser_size, .self = this};
    browser.set_layout_prefs = [](void* self, const KrLayoutPrefs* value) {
      auto& host = *static_cast<Host*>(self);
      host.layout = *value;
      ++host.layouts;
    };
    browser.set_workspace_theme = [](void* self, const KrWorkspaceTheme*,
                                     bool commit) {
      static_cast<Host*>(self)->theme_commits.push_back(commit);
    };
    browser.privacy_features_visibility_changed = [](void* self, bool shown) {
      static_cast<Host*>(self)->privacy_visible += shown ? 1 : -1;
    };
    browser.set_privacy_feature = [](void*, KrPrivacyFeatureId, bool) {
      return KR_OK;
    };
    if (onboarding) {
      browser.onboarding_visibility_changed = [](void* self, bool shown) {
        static_cast<Host*>(self)->visible += shown ? 1 : -1;
      };
      browser.start_import = [](void* self, KrImportSourceId source,
                                std::uint32_t items) {
        auto& host = *static_cast<Host*>(self);
        ++host.imports;
        host.import_source = source;
        host.import_items = items;
        if (host.complete_import_inline) {
          const KrImportProgress progress{sizeof(KrImportProgress), source,
                                          KR_IMPORT_SUCCEEDED, 1};
          KR_EXPECT(host.ui.set_import_progress(host.ui.self, &progress) ==
                    KR_OK);
          KR_EXPECT(host.ui.remove_import_source(host.ui.self, source) ==
                    KR_OK);
        }
        return KR_OK;
      };
      browser.request_default_browser = [](void* self) {
        ++static_cast<Host*>(self)->default_requests;
        return KR_OK;
      };
      browser.set_default_search_engine = [](void* self, KrSearchEngineId id) {
        static_cast<Host*>(self)->engine = id;
        return KR_OK;
      };
      browser.onboarding_finished = [](void* self,
                                       const KrOnboardingResult* value) {
        auto& host = *static_cast<Host*>(self);
        host.result = *value;
        ++host.finished;
      };
    }
    const KrCreateParams create{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                nullptr, &browser};
    KR_ASSERT(kr_ui_create(&create, &ui) == KR_OK);
    const KrWindowState window{.struct_size = sizeof(KrWindowState),
                               .flags = KR_WINDOW_ACTIVE,
                               .scale_factor = 1,
                               .client = {0, 0, 1280, 800}};
    ui.set_window_state(ui.self, &window);
  }
  ~Host() {
    if (ui.self)
      ui.destroy(ui.self);
    headless::Reset();
  }
  void Destroy() {
    ui.destroy(ui.self);
    ui.self = nullptr;
  }
  KrResult Show(std::uint32_t step = KR_ONBOARDING_STEP_IGNITION) {
    const KrOnboardingRequest request{sizeof(KrOnboardingRequest),
                                      KR_ONBOARDING_FIRST_RUN, step};
    const auto shown = ui.show_onboarding(ui.self, &request);
    headless::RunPendingTasks();
    return shown;
  }
  void Publish() {
    const KrImportSource source{
        sizeof(KrImportSource), 7, KR_IMPORT_BOOKMARKS | KR_IMPORT_HISTORY, 0,
        {"Sample profile", 14}, 0};
    KR_ASSERT(ui.upsert_import_source(ui.self, &source) == KR_OK);
    for (std::uint64_t id : {1u, 2u}) {
      const KrSearchEngine record{sizeof(KrSearchEngine),
                                  id,
                                  id == 1 ? KR_SEARCH_ENGINE_DEFAULT : 0u,
                                  static_cast<std::uint32_t>(id),
                                  {"Engine", 6},
                                  {"e", 1},
                                  0};
      KR_ASSERT(ui.upsert_search_engine(ui.self, &record) == KR_OK);
    }
    const KrDefaultBrowserState state{sizeof(KrDefaultBrowserState),
                                      KR_DEFAULT_BROWSER_CAN_SET};
    KR_ASSERT(ui.set_default_browser_state(ui.self, &state) == KR_OK);
    headless::RunPendingTasks();
  }
  static const headless::RenderSnapshot::OnboardingSnapshot& Last() {
    return headless::LastRender().onboarding;
  }
  static void Act(OnboardingAction action) {
    headless::Delegate()->OnOnboardingAction(action);
    headless::RunPendingTasks();
  }
};
}  // namespace

KR_TEST(OnboardingAbiNeedsAHostThatRecordsCompletion) {
  Host host(false);
  KR_EXPECT(host.Show() == KR_ERR_UNSUPPORTED);
  KR_EXPECT(!Host::Last().open);
  KR_EXPECT(host.ui.show_onboarding(host.ui.self, nullptr) ==
            KR_ERR_INVALID_ARGUMENT);
  KrOnboardingRequest short_request{offsetof(KrOnboardingRequest, step),
                                    KR_ONBOARDING_FIRST_RUN, 0};
  KR_EXPECT(host.ui.show_onboarding(host.ui.self, &short_request) ==
            KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(OnboardingAbiStopsAtTheLibraryForOlderHostTables) {
  // An older UI table ends before the onboarding slots.
  headless::Reset();
  KrBrowser browser{.struct_size = sizeof(KrBrowser)};
  KrShellUI ui{.struct_size = offsetof(KrShellUI, show_onboarding)};
  const KrCreateParams create{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                              nullptr, &browser};
  KR_ASSERT(kr_ui_create(&create, &ui) == KR_OK);
  KR_EXPECT(!KR_HAS_SLOT(&ui, KrShellUI, show_onboarding));
  ui.destroy(ui.self);
  headless::Reset();
}

KR_TEST(OnboardingAbiWalksEveryMomentAndCommitsOnNext) {
  Host host;
  KR_ASSERT(host.Show() == KR_OK);
  KR_EXPECT_EQ(host.visible, 1);
  KR_EXPECT_EQ(host.privacy_visible, 1);
  host.Publish();
  const auto& first = Host::Last();
  KR_EXPECT(first.open);
  KR_EXPECT(first.step == OnboardingStep::kIgnition);
  KR_EXPECT(!first.can_go_back);
  KR_EXPECT(first.progress.empty());
  // Privacy has nothing flagged for onboarding, so it is not on the rail.
  KR_EXPECT_EQ(first.rail.size(), std::size_t{5});
  KR_EXPECT_EQ(first.import_sources.size(), std::size_t{1});
  KR_EXPECT_EQ(first.engines.size(), std::size_t{2});

  // Other surfaces wait while onboarding owns the window.
  KR_EXPECT(host.ui.show_settings(host.ui.self, KR_SETTINGS_APPEARANCE) !=
            KR_OK);
  KR_EXPECT(host.ui.show_library(host.ui.self, KR_LIBRARY_HISTORY) != KR_OK);

  Host::Act(OnboardingAction::kNext);
  KR_EXPECT(Host::Last().step == OnboardingStep::kShape);
  KR_EXPECT_EQ(Host::Last().progress, std::string("Step 1 of 5"));
  headless::Delegate()->OnOnboardingLayoutHovered(LayoutChoice::kClassic);
  headless::RunPendingTasks();
  KR_EXPECT(Host::Last().ghost);
  KR_EXPECT(!Host::Last().chrome_veiled);
  headless::Delegate()->OnOnboardingLayoutChosen(LayoutChoice::kClassic);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.layouts, 1);
  KR_EXPECT((host.layout.flags & KR_LAYOUT_TABS_CLASSIC) != 0);
  KR_EXPECT(Host::Last().chrome_veiled);
  const auto morph = Host::Last().morph_serial;
  headless::Delegate()->OnOnboardingMorphSettled(morph + 1);  // stale
  headless::RunPendingTasks();
  KR_EXPECT(Host::Last().chrome_veiled);
  // The veil lifts after the morph even before the host confirms.
  headless::Delegate()->OnOnboardingMorphSettled(morph);
  headless::RunPendingTasks();
  KR_EXPECT(!Host::Last().chrome_veiled);
  KR_EXPECT(!Host::Last().ghost);

  Host::Act(OnboardingAction::kNext);
  KR_EXPECT(Host::Last().step == OnboardingStep::kColor);
  const Rect field = Host::Last().color_field;
  headless::Delegate()->OnOnboardingColorPoint(
      {field.x + field.width * 0.6f, field.y + field.height * 0.3f});
  headless::RunPendingTasks();
  KR_ASSERT(Host::Last().theme.has_value());
  KR_EXPECT(Host::Last().theme->customized);
  KR_EXPECT_EQ(host.theme_commits.size(), std::size_t{1});
  KR_EXPECT(!host.theme_commits.back());
  headless::Delegate()->OnOnboardingThemeEdited(
      {ThemeEditKind::kPreset, {}, 0, Host::Last().first_preset}, {640, 400});
  headless::RunPendingTasks();
  KR_EXPECT_EQ(Host::Last().reveal.serial, std::uint32_t{1});
  KR_EXPECT_EQ(Host::Last().reveal.origin.x, 640.0f);

  // Back keeps the draft but returns the host's preview.
  Host::Act(OnboardingAction::kBack);
  KR_EXPECT(Host::Last().step == OnboardingStep::kShape);
  KR_EXPECT_EQ(host.theme_commits.size(), std::size_t{3});
  KR_EXPECT(!host.theme_commits.back());
  Host::Act(OnboardingAction::kNext);
  KR_EXPECT(Host::Last().theme->customized);
  Host::Act(OnboardingAction::kNext);
  KR_EXPECT(host.theme_commits.back());
  KR_EXPECT(Host::Last().step == OnboardingStep::kImport);

  // The library is on screen, so the import lands there.
  const Anchor library{AnchorKind::kElement,
                       std::to_underlying(Element::kLibrary),
                       {12, 700, 32, 32}};
  headless::Delegate()->OnAnchorsChanged(std::span(&library, 1));
  headless::Delegate()->OnOnboardingImportChosen(ImportSourceId{7});
  headless::Delegate()->OnOnboardingImportItemToggled(ImportData::kHistory,
                                                      false);
  headless::Delegate()->OnOnboardingImportItemToggled(ImportData::kPasswords,
                                                      true);  // unsupported
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.imports, 0);
  Host::Act(OnboardingAction::kNext);
  KR_EXPECT_EQ(host.imports, 1);
  KR_EXPECT_EQ(host.import_source, KrImportSourceId{7});
  KR_EXPECT_EQ(host.import_items, std::uint32_t{KR_IMPORT_BOOKMARKS});
  KR_EXPECT(Host::Last().flight.target == Element::kLibrary);
  KR_EXPECT_EQ(Host::Last().flight.serial, std::uint32_t{1});
  KR_EXPECT(Host::Last().import_requested);
  KR_EXPECT(!Host::Last().import_status.empty());
  const KrImportProgress progress{sizeof(KrImportProgress), 7,
                                  KR_IMPORT_SUCCEEDED, 1};
  KR_ASSERT(host.ui.set_import_progress(host.ui.self, &progress) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(Host::Last().import_status,
               std::string("Imported from Sample profile"));

  KR_EXPECT(Host::Last().step == OnboardingStep::kSearch);
  KR_EXPECT_EQ(Host::Last().engine.value, std::uint64_t{1});
  headless::Delegate()->OnOnboardingSearchChosen(SearchEngineId{2});
  Host::Act(OnboardingAction::kBack);
  KR_EXPECT_EQ(host.engine, KrSearchEngineId{0});  // Back never commits
  Host::Act(OnboardingAction::kNext);
  KR_EXPECT(Host::Last().step == OnboardingStep::kSearch);
  KR_EXPECT_EQ(Host::Last().engine.value, std::uint64_t{2});  // kept
  Host::Act(OnboardingAction::kNext);
  KR_EXPECT_EQ(host.engine, KrSearchEngineId{2});

  KR_EXPECT(Host::Last().step == OnboardingStep::kDefaultBrowser);
  headless::Delegate()->OnOnboardingDefaultChoice(true);
  headless::Delegate()->OnOnboardingDefaultChoice(true);
  headless::RunPendingTasks();
  KR_EXPECT_EQ(host.default_requests, 1);
  KR_EXPECT(Host::Last().default_requested);
  const KrDefaultBrowserState now{
      sizeof(KrDefaultBrowserState),
      KR_DEFAULT_BROWSER_IS_DEFAULT | KR_DEFAULT_BROWSER_CAN_SET};
  KR_ASSERT(host.ui.set_default_browser_state(host.ui.self, &now) == KR_OK);
  headless::RunPendingTasks();
  KR_EXPECT(!Host::Last().default_requested);
  KR_EXPECT(Host::Last().default_browser.Has(DefaultBrowserFlag::kIsDefault));
  KR_EXPECT(Host::Last().step == OnboardingStep::kDefaultBrowser);

  Host::Act(OnboardingAction::kNext);
  KR_EXPECT(Host::Last().step == OnboardingStep::kReady);
  KR_EXPECT_EQ(host.finished, 0);
  Host::Act(OnboardingAction::kNext);
  KR_EXPECT_EQ(host.finished, 1);
  KR_EXPECT_EQ(host.result.outcome,
               static_cast<std::uint32_t>(KR_ONBOARDING_COMPLETED));
  KR_EXPECT_EQ(host.result.last_step,
               static_cast<std::uint32_t>(KR_ONBOARDING_STEP_READY));
  KR_EXPECT(Host::Last().finishing);
  Host::Act(OnboardingAction::kBack);  // ignored while finishing
  KR_EXPECT(Host::Last().step == OnboardingStep::kReady);

  headless::Delegate()->OnOnboardingHandoffFinished();
  headless::RunPendingTasks();
  KR_EXPECT(!Host::Last().open);
  KR_EXPECT_EQ(host.visible, 0);
  KR_EXPECT_EQ(host.privacy_visible, 0);
  // The card became the address field.
  KR_EXPECT(headless::LastRender().command_bar_open);
  host.Destroy();
  KR_EXPECT_EQ(host.finished, 1);
}

KR_TEST(OnboardingImportAcceptsSynchronousCompletionAndSourceRemoval) {
  Host host;
  KR_ASSERT(host.Show() == KR_OK);
  host.Publish();
  host.complete_import_inline = true;
  Host::Act(OnboardingAction::kNext);  // Shape
  Host::Act(OnboardingAction::kNext);  // Color
  Host::Act(OnboardingAction::kNext);  // Import
  KR_ASSERT(Host::Last().step == OnboardingStep::kImport);
  headless::Delegate()->OnOnboardingImportChosen(ImportSourceId{7});
  Host::Act(OnboardingAction::kNext);
  KR_EXPECT_EQ(host.imports, 1);
  KR_EXPECT(!Host::Last().import_requested);
  KR_EXPECT(Host::Last().import.phase == ImportPhase::kSucceeded);
  KR_EXPECT(Host::Last().step == OnboardingStep::kSearch);
}

KR_TEST(OnboardingAbiSkipAndCloseReportExactlyOnce) {
  {
    Host host;
    KR_ASSERT(host.Show() == KR_OK);
    Host::Act(OnboardingAction::kSkip);
    KR_EXPECT_EQ(host.finished, 1);
    KR_EXPECT_EQ(host.result.outcome,
                 static_cast<std::uint32_t>(KR_ONBOARDING_SKIPPED));
    // Closing the window during the exit motion does not report again.
    host.Destroy();
    KR_EXPECT_EQ(host.finished, 1);
    KR_EXPECT_EQ(host.visible, 0);
  }
  {
    Host host;
    KR_ASSERT(host.Show(KR_ONBOARDING_STEP_SHAPE) == KR_OK);
    KR_EXPECT(Host::Last().step == OnboardingStep::kShape);
    host.Destroy();
    KR_EXPECT_EQ(host.finished, 1);
    KR_EXPECT_EQ(host.result.outcome,
                 static_cast<std::uint32_t>(KR_ONBOARDING_CLOSED));
    KR_EXPECT_EQ(host.result.last_step,
                 static_cast<std::uint32_t>(KR_ONBOARDING_STEP_SHAPE));
  }
}

KR_TEST(OnboardingAbiRecommendedJumpsToReadyWithoutCommitting) {
  {
    Host host;
    KR_ASSERT(host.Show() == KR_OK);
    host.Publish();
    Host::Act(OnboardingAction::kRecommended);
    KR_EXPECT(Host::Last().step == OnboardingStep::kReady);
    KR_EXPECT_EQ(host.layouts + host.imports + host.default_requests, 0);
    KR_EXPECT_EQ(host.engine, KrSearchEngineId{0});
    KR_EXPECT(host.theme_commits.empty());
  }
  {
    // An unavailable start step moves forward to the next available one.
    Host host;
    KR_ASSERT(host.Show(KR_ONBOARDING_STEP_SEARCH) == KR_OK);
    KR_EXPECT(Host::Last().step == OnboardingStep::kReady);
  }
}

KR_TEST(OnboardingAbiRejectsMalformedRecords) {
  Host host;
  KR_ASSERT(host.Show() == KR_OK);
  KrImportSource source{sizeof(KrImportSource), 1, KR_IMPORT_BOOKMARKS, 0,
                        {nullptr, 3},           0};
  KR_EXPECT(host.ui.upsert_import_source(host.ui.self, &source) ==
            KR_ERR_INVALID_ARGUMENT);
  source.name = {"Profile", 7};
  source.struct_size = offsetof(KrImportSource, icon);
  KR_EXPECT(host.ui.upsert_import_source(host.ui.self, &source) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.ui.remove_import_source(host.ui.self, 99) ==
            KR_ERR_INVALID_ARGUMENT);
  const KrImportProgress bad{sizeof(KrImportProgress), 1, 17, 0};
  KR_EXPECT(host.ui.set_import_progress(host.ui.self, &bad) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.ui.set_default_browser_state(host.ui.self, nullptr) ==
            KR_ERR_INVALID_ARGUMENT);
  KrSearchEngine engine{sizeof(KrSearchEngine), 1, 0, 0, {"E", 1},
                        {nullptr, 2},           0};
  KR_EXPECT(host.ui.upsert_search_engine(host.ui.self, &engine) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.ui.remove_search_engine(host.ui.self, 4) ==
            KR_ERR_INVALID_ARGUMENT);
  headless::RunPendingTasks();
  KR_EXPECT(Host::Last().import_sources.empty());
  KR_EXPECT(Host::Last().engines.empty());
}
