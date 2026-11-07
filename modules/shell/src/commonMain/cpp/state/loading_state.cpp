#include <cmath>

#include "krypton/ui/state/store.hpp"

namespace kr {
Result<void> Store::SetLoadProgress(const LoadProgressUpdate& update) {
  if (!update.tab || !update.navigation_id || !std::isfinite(update.progress) ||
      update.progress < 0 || update.progress > 1)
    return Fail(ErrorCode::kInvalidArgument, "invalid load progress");
  if (update.tab != active_tab_)
    return {};
  if (loading_.Update(update, IsProgressPage(nav_.full_url)))
    MarkDirty(Dirty::kLoading);
  return {};
}

void Store::SetAddressFocused(bool focused) {
  if (ui_.address_focused == focused)
    return;
  ui_.address_focused = focused;
  MarkDirty(Dirty::kLoading);
}
}  // namespace kr
