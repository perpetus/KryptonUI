#include "krypton/ui/shell/shell.hpp"

namespace kr {
bool BrowserPort::CanRequestGlassBackdrop() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, request_glass_backdrop) &&
         table_.request_glass_backdrop;
}
void BrowserPort::RequestGlassBackdrop(
    const GlassBackdropRequest& request) const {
  if (!CanRequestGlassBackdrop())
    return;
  // Rect is a fixed ABI value type, but convert rather than aliasing C++ types.
  std::vector<KrRect> regions;
  regions.reserve(request.regions.size());
  for (const auto& r : request.regions)
    regions.push_back({r.x, r.y, r.width, r.height});
  const KrGlassBackdropRequest value{
      sizeof(value), request.revision, regions.data(), regions.size(),
      request.scale, request.margin,   request.adapter};
  table_.request_glass_backdrop(table_.self, &value);
}
bool Shell::CanRequestGlassBackdrop() const {
  return browser_.CanRequestGlassBackdrop();
}
void Shell::OnGlassBackdropRequested(const GlassBackdropRequest& request) {
  browser_.RequestGlassBackdrop(request);
}
}  // namespace kr
