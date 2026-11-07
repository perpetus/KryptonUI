#pragma once

#include <windows.h>

#include "krypton/fixtures/mock_browser.hpp"

namespace kr::preview {

// Finite input/lifecycle check, enabled only by --smoke-input. It posts only to
// this preview's own HWNDs; it never injects input into the user's desktop.
class InputSmoke {
 public:
  [[nodiscard]] bool Observe(const MSG& message) noexcept;
  void ObservePage(UINT message, WPARAM wparam, LPARAM lparam) noexcept;
  [[nodiscard]] bool Advance(unsigned frame,
                             HWND parent,
                             fixtures::MockBrowser& browser);
  [[nodiscard]] bool Complete() const noexcept {
    return phase_ == Phase::kDone;
  }
  [[nodiscard]] const char* Failure() const noexcept { return failure_; }
  [[nodiscard]] unsigned PendingStep() const noexcept {
    return static_cast<unsigned>(phase_);
  }

 private:
  enum class Phase {
    kOpenWorkspace,
    kWaitWorkspace,
    kOpenSearch,
    kWaitFocus,
    kWaitSelection,
    kWaitKey,
    kWaitToolbarFocus,
    kWaitPagePress,
    kWaitPageFocusReturn,
    kWaitPageMenu,
    kWaitPageMenuClosed,
    kWaitPageInput,
    kWaitFullscreen,
    kWaitWindowed,
    kWaitMaximized,
    kWaitRestored,
    kDone
  };
  Phase phase_ = Phase::kOpenWorkspace;
  HWND target_ = nullptr;
  HWND chrome_target_ = nullptr;
  LPARAM key_data_ = 0;
  unsigned keys_ = 0;
  unsigned page_keys_ = 0, page_releases_ = 0, page_characters_ = 0;
  ULONGLONG ready_after_ = 0;
  const char* failure_ = nullptr;
};

}  // namespace kr::preview
