#include <algorithm>
#include <utility>

#include "krypton/ui/model/browser_services.hpp"
namespace kr {
namespace {
template <class R, class I>
auto FindById(R& range, I id) noexcept {
  return std::ranges::find_if(
      range, [id](const auto& value) { return value.id == id; });
}
}  // namespace
Result<void> BrowserServicesState::SetDefaultBrowser(
    Flags<DefaultBrowserFlag> flags) {
  constexpr auto known =
      DefaultBrowserFlag::kIsDefault | DefaultBrowserFlag::kCanSet |
      DefaultBrowserFlag::kManaged | DefaultBrowserFlag::kPending;
  if (flags.bits() & ~known.bits())
    return Fail(ErrorCode::kInvalidArgument, "Invalid default browser state");
  default_browser = flags;
  default_browser_known = true;
  ++revision;
  return {};
}
const SearchEngine* BrowserServicesState::FindSearchEngine(
    SearchEngineId id) const noexcept {
  const auto it = FindById(engines, id);
  return it == engines.end() ? nullptr : &*it;
}
Result<void> BrowserServicesState::UpsertSearchEngine(SearchEngine value) {
  constexpr auto known =
      SearchEngineFlag::kDefault | SearchEngineFlag::kManaged;
  if (!value.id || value.name.empty() || (value.flags.bits() & ~known.bits()))
    return Fail(ErrorCode::kInvalidArgument, "Invalid search engine");
  if (value.flags.Has(SearchEngineFlag::kDefault)) {
    for (auto& engine : engines)
      engine.flags.Set(SearchEngineFlag::kDefault, false);
  }
  const auto it = FindById(engines, value.id);
  if (it == engines.end())
    engines.push_back(std::move(value));
  else
    *it = std::move(value);
  std::ranges::stable_sort(engines, {}, &SearchEngine::order);
  ++revision;
  return {};
}
Result<void> BrowserServicesState::RemoveSearchEngine(SearchEngineId id) {
  if (!FindSearchEngine(id))
    return Fail(ErrorCode::kNotFound, "Unknown search engine");
  std::erase_if(engines, [id](const auto& value) { return value.id == id; });
  ++revision;
  return {};
}
SearchEngineId BrowserServicesState::DefaultEngine() const noexcept {
  const auto it = std::ranges::find_if(engines, [](const auto& engine) {
    return engine.flags.Has(SearchEngineFlag::kDefault);
  });
  return it == engines.end() ? SearchEngineId{} : it->id;
}
bool BrowserServicesState::SearchManaged() const noexcept {
  return std::ranges::any_of(engines, [](const auto& engine) {
    return engine.flags.Has(SearchEngineFlag::kManaged);
  });
}
}  // namespace kr
