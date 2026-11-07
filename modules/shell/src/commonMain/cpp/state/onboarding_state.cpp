#include <algorithm>
#include <cmath>
#include <utility>

#include "krypton/ui/model/onboarding.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr {
namespace {
template <typename Range, typename IdType>
auto FindById(Range& range, IdType id) noexcept {
  return std::ranges::find_if(
      range, [id](const auto& value) { return value.id == id; });
}
}  // namespace

const ImportSource* OnboardingState::FindImportSource(
    ImportSourceId id) const noexcept {
  const auto it = FindById(import_sources, id);
  return it == import_sources.end() ? nullptr : &*it;
}
Result<void> OnboardingState::UpsertImportSource(ImportSource value) {
  if (!value.id || value.name.empty() || value.items.none() ||
      (value.items.bits() & ~kAllImportData.bits()))
    return Fail(ErrorCode::kInvalidArgument, "Invalid import source");
  const auto it = FindById(import_sources, value.id);
  if (it == import_sources.end())
    import_sources.push_back(std::move(value));
  else
    *it = std::move(value);
  std::ranges::stable_sort(import_sources, {}, &ImportSource::order);
  ++revision;
  return {};
}
Result<void> OnboardingState::RemoveImportSource(ImportSourceId id) {
  if (!FindImportSource(id))
    return Fail(ErrorCode::kNotFound, "Unknown import source");
  std::erase_if(import_sources,
                [id](const auto& value) { return value.id == id; });
  if (draft.import_source == id)
    draft.import_source = {};
  ++revision;
  return {};
}
Result<void> OnboardingState::SetImportProgress(const ImportProgress& value) {
  if (value.phase > ImportPhase::kFailed || !std::isfinite(value.fraction) ||
      value.fraction > 1 ||
      (value.phase != ImportPhase::kIdle && !value.source))
    return Fail(ErrorCode::kInvalidArgument, "Invalid import progress");
  import = value;
  import_requested = false;
  ++revision;
  return {};
}
bool OnboardingState::SetLanding(const Anchor& anchor) noexcept {
  if (anchor.kind != AnchorKind::kElement)
    return false;
  for (std::size_t i = 0; i < kOnboardingLandingElements.size(); ++i) {
    if (std::to_underlying(kOnboardingLandingElements[i]) != anchor.id)
      continue;
    const OnboardingLanding value{kOnboardingLandingElements[i], anchor.bounds,
                                  anchor.visible && !anchor.bounds.empty()};
    if (landings[i] == value)
      return false;
    landings[i] = value;
    return true;
  }
  return false;
}
const OnboardingLanding* OnboardingState::Landing(
    Element element) const noexcept {
  for (const auto& landing : landings) {
    if (landing.element == element && landing.visible)
      return &landing;
  }
  return nullptr;
}
}  // namespace kr
