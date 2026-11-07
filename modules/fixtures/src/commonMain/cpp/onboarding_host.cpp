#include <algorithm>
#include <array>
#include <type_traits>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::fixtures {
namespace {
KrStr Borrow(std::string_view text) noexcept {
  return {text.data(), text.size()};
}
struct SampleSource {
  KrImportSourceId id;
  std::uint32_t items;
  std::string_view name;
};
// Real brand names exercise the bundled icons without network or host images.
constexpr std::array kSources{
    SampleSource{1,
                 KR_IMPORT_BOOKMARKS | KR_IMPORT_HISTORY | KR_IMPORT_PASSWORDS |
                     KR_IMPORT_SEARCH_ENGINES,
                 "Google Chrome · Personal profile"},
    SampleSource{2,
                 KR_IMPORT_BOOKMARKS | KR_IMPORT_HISTORY | KR_IMPORT_AUTOFILL |
                     KR_IMPORT_EXTENSIONS,
                 "Arc · Work profile"}};
struct SampleEngine {
  KrSearchEngineId id;
  std::string_view name, keyword;
};
constexpr std::array kEngines{
    SampleEngine{1, "Google", "google.com"},
    SampleEngine{2, "DuckDuckGo", "duckduckgo.com"},
    SampleEngine{3, "Brave Search", "search.brave.com"}};
// Deliberate fixture samples, not estimates used by the production shell.
constexpr std::array kImportSteps{0.3f, 0.65f, 0.9f, 1.0f};

template <typename Function>
auto AtBoundary(Function&& function) noexcept {
  try {
    return function();
  } catch (...) {
    // The preview obeys the same no-unwind C boundary as the browser.
    if constexpr (std::is_same_v<decltype(function()), KrResult>)
      return KR_ERR_PLATFORM;
  }
}
}  // namespace

void MockBrowser::InitOnboarding() {
  table_.onboarding_visibility_changed = [](void* self, bool visible) noexcept {
    AtBoundary([&] {
      auto& mock = Self(self);
      mock.onboarding_visible_ = visible;
      if (visible)
        mock.PushOnboarding();
    });
  };
  table_.start_import = [](void* self, KrImportSourceId source,
                           std::uint32_t items) noexcept {
    return AtBoundary([&] {
      auto& mock = Self(self);
      const auto known = std::ranges::find(kSources, source, &SampleSource::id);
      if (known == kSources.end() || !(items & known->items) ||
          (items & ~known->items) || mock.import_state_ == KR_IMPORT_RUNNING)
        return KR_ERR_INVALID_ARGUMENT;
      mock.import_source_ = source;
      mock.import_state_ = KR_IMPORT_RUNNING;
      mock.import_step_ = 0;
      mock.import_fraction_ = 0;
      mock.PushImportProgress();
      if (!mock.callbacks_.import_requested ||
          !mock.callbacks_.import_requested(source)) {
        while (mock.AdvanceImport()) {
        }
      }
      return KR_OK;
    });
  };
  table_.request_default_browser = [](void* self) noexcept {
    return AtBoundary([&] {
      auto& mock = Self(self);
      if (!(mock.default_browser_flags_ & KR_DEFAULT_BROWSER_CAN_SET) ||
          (mock.default_browser_flags_ & KR_DEFAULT_BROWSER_IS_DEFAULT))
        return KR_ERR_INVALID_ARGUMENT;
      mock.default_browser_flags_ |= KR_DEFAULT_BROWSER_PENDING;
      mock.PushDefaultBrowser();
      if (!mock.callbacks_.default_browser_requested ||
          !mock.callbacks_.default_browser_requested())
        mock.CompleteDefaultBrowserRequest(true);
      return KR_OK;
    });
  };
  table_.set_default_search_engine = [](void* self,
                                        KrSearchEngineId id) noexcept {
    return AtBoundary([&] {
      auto& mock = Self(self);
      if (std::ranges::find(kEngines, id, &SampleEngine::id) == kEngines.end())
        return KR_ERR_INVALID_ARGUMENT;
      mock.default_engine_ = id;
      if (mock.onboarding_visible_)
        mock.PushOnboarding();
      return KR_OK;
    });
  };
  table_.onboarding_finished = [](void* self,
                                  const KrOnboardingResult* result) noexcept {
    AtBoundary([&] {
      if (!result || !KR_HAS_FIELD(result, KrOnboardingResult, last_step))
        return;
      auto& mock = Self(self);
      mock.onboarding_result_ = *result;
      if (mock.callbacks_.onboarding_finished)
        mock.callbacks_.onboarding_finished(*result);
    });
  };
}

bool MockBrowser::ShowOnboarding(std::uint32_t step) {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, show_onboarding) ||
      !ui_.show_onboarding)
    return false;
  const KrOnboardingRequest request{sizeof(KrOnboardingRequest),
                                    KR_ONBOARDING_FIRST_RUN, step};
  return ui_.show_onboarding(ui_.self, &request) == KR_OK;
}

bool MockBrowser::TryOpenOnboarding(std::string_view url) {
  constexpr std::string_view address = "krypton://onboarding";
  return (url == address || url == "krypton://welcome") && ShowOnboarding();
}

bool MockBrowser::AdvanceImport() {
  if (import_state_ != KR_IMPORT_RUNNING || import_step_ >= kImportSteps.size())
    return false;
  import_fraction_ = kImportSteps[import_step_++];
  if (import_fraction_ >= 1)
    import_state_ = KR_IMPORT_SUCCEEDED;
  PushImportProgress();
  return import_state_ == KR_IMPORT_RUNNING;
}

void MockBrowser::CompleteDefaultBrowserRequest(bool made_default) {
  if (!(default_browser_flags_ & KR_DEFAULT_BROWSER_PENDING))
    return;
  default_browser_flags_ &= ~KR_DEFAULT_BROWSER_PENDING;
  if (made_default)
    default_browser_flags_ |= KR_DEFAULT_BROWSER_IS_DEFAULT;
  PushDefaultBrowser();
}

void MockBrowser::PushOnboarding() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, remove_search_engine) ||
      !ui_.upsert_import_source || !ui_.upsert_search_engine)
    return;
  for (std::size_t i = 0; i < kSources.size(); ++i) {
    const auto& source = kSources[i];
    const KrImportSource record{
        sizeof(KrImportSource),        source.id,           source.items,
        static_cast<std::uint32_t>(i), Borrow(source.name), 0};
    (void)ui_.upsert_import_source(ui_.self, &record);
  }
  PushBrowserServices();
  if (import_state_ != KR_IMPORT_IDLE)
    PushImportProgress();
}

void MockBrowser::PushImportProgress() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, set_import_progress) ||
      !ui_.set_import_progress)
    return;
  const KrImportProgress value{sizeof(KrImportProgress), import_source_,
                               import_state_, import_fraction_};
  (void)ui_.set_import_progress(ui_.self, &value);
}

void MockBrowser::PushBrowserServices() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, remove_search_engine) ||
      !ui_.upsert_search_engine)
    return;
  for (std::size_t i = 0; i < kEngines.size(); ++i) {
    const auto& engine = kEngines[i];
    const KrSearchEngine record{
        sizeof(KrSearchEngine),
        engine.id,
        engine.id == default_engine_ ? KR_SEARCH_ENGINE_DEFAULT : 0u,
        static_cast<std::uint32_t>(i),
        Borrow(engine.name),
        Borrow(engine.keyword),
        0};
    (void)ui_.upsert_search_engine(ui_.self, &record);
  }
  PushDefaultBrowser();
}
void MockBrowser::PushDefaultBrowser() const {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, set_default_browser_state) ||
      !ui_.set_default_browser_state)
    return;
  const KrDefaultBrowserState value{sizeof(KrDefaultBrowserState),
                                    default_browser_flags_};
  (void)ui_.set_default_browser_state(ui_.self, &value);
}
}  // namespace kr::fixtures
