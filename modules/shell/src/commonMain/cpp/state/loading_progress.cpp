#include "krypton/ui/model/loading_progress.hpp"

#include <algorithm>
#include <cmath>

#include "krypton/ui/design/tokens.hpp"

namespace kr {
bool IsProgressPage(std::string_view url) noexcept {
  const auto starts = [url](std::string_view prefix) {
    if (url.size() < prefix.size())
      return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
      const char value = url[i] >= 'A' && url[i] <= 'Z'
                             ? static_cast<char>(url[i] + ('a' - 'A'))
                             : url[i];
      if (value != prefix[i])
        return false;
    }
    return true;
  };
  return starts("http://") || starts("https://");
}

void LoadingProgress::Reset(TabId tab) noexcept {
  state_ = {.tab = tab};
  host_navigation_ = 0;
  host_terminal_ = false;
  detailed_ = false;
}

void LoadingProgress::Begin() noexcept {
  if (++next_session_ == 0)
    ++next_session_;
  state_.session = next_session_;
  state_.progress = design::kTokens.loading_progress.minimum_progress;
  state_.phase = LoadPhase::kLoading;
}

void LoadingProgress::SyncNavigation(TabId tab,
                                     bool loading,
                                     bool new_navigation,
                                     bool eligible) noexcept {
  if (state_.tab != tab)
    Reset(tab);
  if (!tab || !eligible) {
    state_.phase = LoadPhase::kIdle;
    return;
  }
  if (loading && ((new_navigation &&
                   (!detailed_ || state_.phase != LoadPhase::kLoading)) ||
                  (state_.phase == LoadPhase::kIdle && !detailed_))) {
    Begin();
    detailed_ = false;
  } else if (!loading && state_.phase == LoadPhase::kLoading) {
    state_.phase = LoadPhase::kComplete;
    state_.progress = 1;
    if (detailed_)
      host_terminal_ = true;
  }
}

bool LoadingProgress::Update(const LoadProgressUpdate& update,
                             bool eligible) noexcept {
  if (update.tab != state_.tab || !update.navigation_id ||
      !std::isfinite(update.progress) || update.progress < 0 ||
      update.progress > 1 || update.navigation_id < host_navigation_)
    return false;
  if (update.navigation_id == host_navigation_ && host_terminal_ &&
      update.loading)
    return false;
  const auto before = state_;
  const bool next = update.navigation_id != host_navigation_;
  state_.enabled = update.enabled;
  if (!eligible || update.same_document) {
    host_navigation_ = update.navigation_id;
    host_terminal_ = !update.loading;
    state_.phase = LoadPhase::kIdle;
    detailed_ = true;
    return state_ != before;
  }
  if (next && update.loading &&
      (detailed_ || state_.phase != LoadPhase::kLoading))
    Begin();
  host_navigation_ = update.navigation_id;
  detailed_ = true;
  if (update.loading) {
    state_.phase = LoadPhase::kLoading;
    state_.progress =
        std::max({state_.progress, update.progress,
                  design::kTokens.loading_progress.minimum_progress});
    host_terminal_ = false;
  } else {
    if (state_.phase == LoadPhase::kLoading)
      state_.phase = LoadPhase::kComplete;
    state_.progress = 1;
    host_terminal_ = true;
  }
  return state_ != before;
}
}  // namespace kr
