#include "krypton/ui/shell/render_stats.hpp"

#include <algorithm>

#include "krypton/ui/core/log.hpp"

namespace kr {

void RenderStats::Record(std::chrono::steady_clock::duration elapsed) {
  using std::chrono::duration_cast;
  using std::chrono::microseconds;
  const auto us = duration_cast<microseconds>(elapsed);
  ++count_;

  if (us > kBudget) {
    Log(LogLevel::kWarning, "render #{} took {:.2f} ms (budget {:.1f} ms)",
        count_, us.count() / 1000.0, kBudget.count() / 1000.0);
  }
  if (!periodic_)
    return;

  ++window_count_;
  window_total_ += us;
  window_max_ = std::max(window_max_, us);
  if (window_count_ == kSummaryEvery) {
    Log(LogLevel::kInfo, "renders {}..{}: avg {:.3f} ms, max {:.3f} ms",
        count_ - window_count_ + 1, count_,
        window_total_.count() / 1000.0 / window_count_,
        window_max_.count() / 1000.0);
    window_count_ = 0;
    window_total_ = microseconds{0};
    window_max_ = microseconds{0};
  }
}

}  // namespace kr
