#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>

namespace kr::win {

enum class InputTraceEvent {
  kFocus,
  kPress,
  kRelease,
  kCaptureLost,
  kSurface,
  kReturn,
  kActivation,
  kSwipe,
  kWorkspaceFromGesture,
  kWorkspaceFromHost,
  kPointerRoute,
  kNavigationExit,
  kPointerTarget
};

// Opt-in, bounded, UI-thread-only recording. Only Dump allocates, and file I/O
// runs on a thread-pool worker. No page text or URLs are recorded.
class InputTrace {
 public:
  InputTrace();
  void Record(InputTraceEvent event,
              HWND window,
              std::uint64_t generation,
              std::uint64_t detail = 0) noexcept;
  void Dump() const noexcept;

 private:
  struct Entry {
    ULONGLONG time = 0;
    std::uintptr_t window = 0;
    std::uint64_t generation = 0;
    InputTraceEvent event{};
    std::uint64_t detail = 0;
  };
  std::array<Entry, 512> entries_{};
  std::wstring path_;
  std::uint64_t count_ = 0;
};

}  // namespace kr::win
