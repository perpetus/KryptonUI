#include <optional>
#include <string>
#include <vector>

#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/testing/test.hpp"

namespace kr::fixtures {
namespace {
struct Snapshot {
  std::vector<std::string> sources, engines;
  KrSearchEngineId default_engine = 0;
  std::vector<KrImportProgress> progress;
  std::uint32_t default_flags = 0;
  int shows = 0;
  std::uint32_t step = 0;
};
KrShellUI StubUi(Snapshot& snapshot) {
  KrShellUI ui{.struct_size = sizeof(KrShellUI), .self = &snapshot};
  ui.set_theme = [](void*, const KrTheme*) {};
  ui.set_layout_prefs = [](void*, const KrLayoutPrefs*) {};
  ui.put_image = [](void*, const KrImage*) {};
  ui.set_spaces = [](void*, const KrSpace*, std::size_t, std::size_t,
                     KrSpaceId) {};
  ui.tab_inserted = [](void*, std::size_t, const KrTab*) {};
  ui.set_active_tab = [](void*, KrTabId) {};
  ui.set_nav_state = [](void*, const KrNavState*) {};
  ui.set_actions = [](void*, const KrAction*, std::size_t, std::size_t) {};
  ui.show_onboarding = [](void* self, const KrOnboardingRequest* request) {
    auto& value = *static_cast<Snapshot*>(self);
    ++value.shows;
    value.step = request->step;
    return KR_OK;
  };
  ui.upsert_import_source = [](void* self, const KrImportSource* source) {
    static_cast<Snapshot*>(self)->sources.emplace_back(source->name.data,
                                                       source->name.size);
    return KR_OK;
  };
  ui.upsert_search_engine = [](void* self, const KrSearchEngine* engine) {
    auto& value = *static_cast<Snapshot*>(self);
    value.engines.emplace_back(engine->name.data, engine->name.size);
    if (engine->flags & KR_SEARCH_ENGINE_DEFAULT)
      value.default_engine = engine->id;
    return KR_OK;
  };
  ui.set_import_progress = [](void* self, const KrImportProgress* progress) {
    static_cast<Snapshot*>(self)->progress.push_back(*progress);
    return KR_OK;
  };
  ui.set_default_browser_state = [](void* self,
                                    const KrDefaultBrowserState* state) {
    static_cast<Snapshot*>(self)->default_flags = state->flags;
    return KR_OK;
  };
  ui.remove_search_engine = [](void*, KrSearchEngineId) { return KR_OK; };
  return ui;
}
}  // namespace

KR_TEST(MockOnboardingPublishesOnlyWhileVisible) {
  MockBrowser browser(MockOptions{}, MockHostCallbacks{});
  Snapshot snapshot;
  browser.Attach(StubUi(snapshot));
  const auto& host = browser.table();
  KR_EXPECT(snapshot.sources.empty());
  KR_EXPECT(browser.ShowOnboarding(KR_ONBOARDING_STEP_COLOR));
  KR_EXPECT_EQ(snapshot.step, std::uint32_t{KR_ONBOARDING_STEP_COLOR});
  host.onboarding_visibility_changed(host.self, true);
  KR_EXPECT_EQ(snapshot.sources.size(), std::size_t{2});
  KR_EXPECT_EQ(snapshot.engines.size(), std::size_t{3});
  KR_EXPECT_EQ(snapshot.default_engine, KrSearchEngineId{1});
  KR_EXPECT_EQ(snapshot.default_flags,
               std::uint32_t{KR_DEFAULT_BROWSER_CAN_SET});
  // Nothing in the fixture names another browser.
  for (const auto& name : snapshot.sources)
    KR_EXPECT(name.find("profile") != std::string::npos);

  KR_EXPECT(host.set_default_search_engine(host.self, 9) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.set_default_search_engine(host.self, 3) == KR_OK);
  KR_EXPECT_EQ(snapshot.default_engine, KrSearchEngineId{3});
  KR_EXPECT_EQ(browser.default_search_engine(), KrSearchEngineId{3});
  host.onboarding_visibility_changed(host.self, false);
  KR_EXPECT(host.set_default_search_engine(host.self, 2) == KR_OK);
  KR_EXPECT_EQ(snapshot.default_engine, KrSearchEngineId{3});  // not visible
}

KR_TEST(MockOnboardingImportAdvancesInExplicitSteps) {
  MockHostCallbacks callbacks;
  callbacks.import_requested = [](KrImportSourceId) { return true; };
  MockBrowser browser(MockOptions{}, std::move(callbacks));
  Snapshot snapshot;
  browser.Attach(StubUi(snapshot));
  const auto& host = browser.table();
  KR_EXPECT(host.start_import(host.self, 9, KR_IMPORT_BOOKMARKS) ==
            KR_ERR_INVALID_ARGUMENT);
  KR_EXPECT(host.start_import(host.self, 1, KR_IMPORT_EXTENSIONS) ==
            KR_ERR_INVALID_ARGUMENT);  // not offered by that profile
  KR_EXPECT(host.start_import(host.self, 1, KR_IMPORT_BOOKMARKS) == KR_OK);
  KR_EXPECT(host.start_import(host.self, 1, KR_IMPORT_BOOKMARKS) ==
            KR_ERR_INVALID_ARGUMENT);  // one at a time
  KR_EXPECT_EQ(snapshot.progress.back().state,
               std::uint32_t{KR_IMPORT_RUNNING});
  int steps = 0;
  while (browser.AdvanceImport())
    ++steps;
  KR_EXPECT_EQ(steps, 3);
  KR_EXPECT_EQ(snapshot.progress.back().state,
               std::uint32_t{KR_IMPORT_SUCCEEDED});
  KR_EXPECT_EQ(snapshot.progress.back().fraction, 1.0f);
  KR_EXPECT(!browser.AdvanceImport());
}

KR_TEST(MockOnboardingDefaultBrowserAndCompletion) {
  bool waiting = false;
  std::optional<KrOnboardingResult> finished;
  MockHostCallbacks callbacks;
  callbacks.default_browser_requested = [&] {
    waiting = true;
    return true;
  };
  callbacks.onboarding_finished = [&](const KrOnboardingResult& result) {
    finished = result;
  };
  MockBrowser browser(MockOptions{}, std::move(callbacks));
  Snapshot snapshot;
  browser.Attach(StubUi(snapshot));
  const auto& host = browser.table();
  KR_EXPECT(host.request_default_browser(host.self) == KR_OK);
  KR_EXPECT(waiting);
  KR_EXPECT((snapshot.default_flags & KR_DEFAULT_BROWSER_PENDING) != 0);
  browser.CompleteDefaultBrowserRequest(true);
  KR_EXPECT_EQ(snapshot.default_flags,
               std::uint32_t{KR_DEFAULT_BROWSER_CAN_SET |
                             KR_DEFAULT_BROWSER_IS_DEFAULT});
  KR_EXPECT(host.request_default_browser(host.self) ==
            KR_ERR_INVALID_ARGUMENT);  // already the default

  const KrOnboardingResult result{sizeof(KrOnboardingResult),
                                  KR_ONBOARDING_SKIPPED,
                                  KR_ONBOARDING_STEP_SHAPE};
  host.onboarding_finished(host.self, &result);
  KR_ASSERT(finished.has_value());
  KR_EXPECT_EQ(finished->outcome, std::uint32_t{KR_ONBOARDING_SKIPPED});
  KR_ASSERT(browser.onboarding_result().has_value());

  // krypton://onboarding replays it from the address field.
  snapshot.shows = 0;
  host.navigate(host.self, {"krypton://onboarding", 20}, KR_OPEN_CURRENT_TAB);
  KR_EXPECT_EQ(snapshot.shows, 1);
}
}  // namespace kr::fixtures
