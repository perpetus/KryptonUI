#pragma once

#include <cstdint>
#include <string_view>

#include "krypton/ui/core/ids.hpp"

namespace kr {
enum class LoadPhase { kIdle, kLoading, kComplete };

struct LoadProgressUpdate {
  TabId tab;
  std::uint64_t navigation_id = 0;
  float progress = 0;
  bool loading = false;
  bool enabled = true;
  bool same_document = false;
};

struct LoadingProgressState {
  TabId tab;
  std::uint64_t session = 0;
  float progress = 0;
  LoadPhase phase = LoadPhase::kIdle;
  bool enabled = true;
  friend bool operator==(const LoadingProgressState&,
                         const LoadingProgressState&) = default;
};

[[nodiscard]] bool IsProgressPage(std::string_view url) noexcept;

// Active-page state only. The host resends a snapshot after activating a tab.
// No clock, platform animation state, or per-frame work belongs here.
class LoadingProgress {
 public:
  void Reset(TabId tab) noexcept;
  void SyncNavigation(TabId tab,
                      bool loading,
                      bool new_navigation,
                      bool eligible) noexcept;
  [[nodiscard]] bool Update(const LoadProgressUpdate& update,
                            bool eligible) noexcept;
  [[nodiscard]] const LoadingProgressState& state() const noexcept {
    return state_;
  }

 private:
  void Begin() noexcept;
  LoadingProgressState state_;
  std::uint64_t next_session_ = 0;
  std::uint64_t host_navigation_ = 0;
  bool host_terminal_ = false;
  bool detailed_ = false;
};
}  // namespace kr
