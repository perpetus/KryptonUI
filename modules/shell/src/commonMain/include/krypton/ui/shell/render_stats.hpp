#pragma once

#include <chrono>
#include <cstdint>

namespace kr {

// Times every render against the budget in README.md. Over-budget renders
// are always logged; KR_UI_STATS=1 adds a summary every kSummaryEvery renders.
class RenderStats {
 public:
  static constexpr std::chrono::microseconds kBudget{4000};
  static constexpr std::uint32_t kSummaryEvery = 120;

  explicit RenderStats(bool periodic_summary) : periodic_(periodic_summary) {}

  void Record(std::chrono::steady_clock::duration elapsed);

  [[nodiscard]] std::uint64_t count() const { return count_; }

 private:
  bool periodic_;
  std::uint64_t count_ = 0;
  std::uint32_t window_count_ = 0;
  std::chrono::microseconds window_total_{0};
  std::chrono::microseconds window_max_{0};
};

}  // namespace kr
