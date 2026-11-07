#include "onboarding_abi.hpp"

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "krypton/ui/core/log.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr {
// The C enums and the C++ enums must never drift apart.
static_assert(std::to_underlying(OnboardingStep::kIgnition) ==
              KR_ONBOARDING_STEP_IGNITION);
static_assert(std::to_underlying(OnboardingStep::kDefaultBrowser) ==
              KR_ONBOARDING_STEP_DEFAULT_BROWSER);
static_assert(std::to_underlying(OnboardingStep::kReady) ==
              KR_ONBOARDING_STEP_READY);
static_assert(std::to_underlying(OnboardingOutcome::kCompleted) ==
              KR_ONBOARDING_COMPLETED);
static_assert(std::to_underlying(OnboardingOutcome::kClosed) ==
              KR_ONBOARDING_CLOSED);
static_assert(std::to_underlying(ImportData::kBookmarks) ==
              KR_IMPORT_BOOKMARKS);
static_assert(std::to_underlying(ImportData::kExtensions) ==
              KR_IMPORT_EXTENSIONS);
static_assert(kAllImportData.bits() ==
              (KR_IMPORT_BOOKMARKS | KR_IMPORT_HISTORY | KR_IMPORT_PASSWORDS |
               KR_IMPORT_SEARCH_ENGINES | KR_IMPORT_AUTOFILL |
               KR_IMPORT_EXTENSIONS));
static_assert(std::to_underlying(ImportPhase::kFailed) == KR_IMPORT_FAILED);
static_assert(std::to_underlying(DefaultBrowserFlag::kIsDefault) ==
              KR_DEFAULT_BROWSER_IS_DEFAULT);
static_assert(std::to_underlying(DefaultBrowserFlag::kPending) ==
              KR_DEFAULT_BROWSER_PENDING);
static_assert(std::to_underlying(SearchEngineFlag::kManaged) ==
              KR_SEARCH_ENGINE_MANAGED);
}  // namespace kr

namespace kr::abi {
namespace {
template <typename Function>
KrResult Call(std::string_view name, Function&& function) noexcept {
  try {
    const auto result = function();
    if (result)
      return KR_OK;
    Log(LogLevel::kWarning, "{}: {}", name, result.error().message);
    return result.error().code == ErrorCode::kUnsupported
               ? KR_ERR_UNSUPPORTED
               : KR_ERR_INVALID_ARGUMENT;
  } catch (const std::exception& error) {
    Log(LogLevel::kError, "{}: {}", name, error.what());
  } catch (...) {
    Log(LogLevel::kError, "{} failed", name);
  }
  return KR_ERR_PLATFORM;
}
Shell& From(void* self) {
  return *static_cast<Shell*>(self);
}
bool Valid(KrStr value) noexcept {
  return value.data || value.size == 0;
}
std::string Copy(KrStr value) {
  return value.data ? std::string(value.data, value.size) : std::string{};
}
}  // namespace

void FillOnboardingUi(KrShellUI& ui) noexcept {
  ui.show_onboarding = [](void* self,
                          const KrOnboardingRequest* request) noexcept {
    return Call("show_onboarding", [&]() -> Result<void> {
      if (!request || !KR_HAS_FIELD(request, KrOnboardingRequest, step) ||
          request->step > KR_ONBOARDING_STEP_READY)
        return Fail(ErrorCode::kInvalidArgument, "Incomplete onboarding");
      return From(self).ShowOnboarding(
          (request->flags & KR_ONBOARDING_FIRST_RUN) != 0,
          static_cast<OnboardingStep>(request->step));
    });
  };
  ui.upsert_import_source = [](void* self,
                               const KrImportSource* source) noexcept {
    return Call("upsert_import_source", [&]() -> Result<void> {
      if (!source || !KR_HAS_FIELD(source, KrImportSource, icon) ||
          !Valid(source->name))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete import source");
      return From(self).UpsertImportSource(
          {ImportSourceId{source->id},
           Flags<ImportData>::FromBits(source->items), source->order,
           Copy(source->name), ImageId{source->icon}});
    });
  };
  ui.remove_import_source = [](void* self, KrImportSourceId id) noexcept {
    return Call("remove_import_source", [&] {
      return From(self).RemoveImportSource(ImportSourceId{id});
    });
  };
  ui.set_import_progress = [](void* self,
                              const KrImportProgress* progress) noexcept {
    return Call("set_import_progress", [&]() -> Result<void> {
      if (!progress || !KR_HAS_FIELD(progress, KrImportProgress, fraction))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete import progress");
      return From(self).SetImportProgress(
          {ImportSourceId{progress->source},
           static_cast<ImportPhase>(progress->state), progress->fraction});
    });
  };
  ui.set_default_browser_state =
      [](void* self, const KrDefaultBrowserState* state) noexcept {
        return Call("set_default_browser_state", [&]() -> Result<void> {
          if (!state || !KR_HAS_FIELD(state, KrDefaultBrowserState, flags))
            return Fail(ErrorCode::kInvalidArgument,
                        "Incomplete default browser state");
          return From(self).SetDefaultBrowserState(
              Flags<DefaultBrowserFlag>::FromBits(state->flags));
        });
      };
  ui.upsert_search_engine = [](void* self,
                               const KrSearchEngine* engine) noexcept {
    return Call("upsert_search_engine", [&]() -> Result<void> {
      if (!engine || !KR_HAS_FIELD(engine, KrSearchEngine, icon) ||
          !Valid(engine->name) || !Valid(engine->keyword))
        return Fail(ErrorCode::kInvalidArgument, "Incomplete search engine");
      return From(self).UpsertSearchEngine(
          {SearchEngineId{engine->id},
           Flags<SearchEngineFlag>::FromBits(engine->flags), engine->order,
           Copy(engine->name), Copy(engine->keyword), ImageId{engine->icon}});
    });
  };
  ui.remove_search_engine = [](void* self, KrSearchEngineId id) noexcept {
    return Call("remove_search_engine", [&] {
      return From(self).RemoveSearchEngine(SearchEngineId{id});
    });
  };
}
}  // namespace kr::abi
