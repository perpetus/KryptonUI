// macOS actuals: not written yet. The SwiftUI implementation will live in this
// source set (Swift imports abi.h through a module map; see README.md). Until
// then CheckSupport() reports "unsupported", so the host keeps its own UI.

#include <cstdio>
#include <print>

#include "krypton/ui/platform/platform.hpp"

namespace kr::platform {
Result<void> ShutdownRuntime() noexcept {
  return {};
}

std::string_view Name() noexcept {
  return "macos-unimplemented";
}

Result<void> CheckSupport() noexcept {
  return Fail(ErrorCode::kUnsupported,
              "the macOS (SwiftUI) implementation hasn't been written yet");
}

Result<std::unique_ptr<ShellView>> CreateShellView(
    const ShellViewParams& /*params*/,
    ShellViewDelegate& /*delegate*/) noexcept {
  return Fail(ErrorCode::kUnsupported,
              "the macOS (SwiftUI) implementation hasn't been written yet");
}

void WriteDefaultLog(LogLevel /*level*/, std::string_view text) noexcept {
  std::println(stderr, "[krypton-ui] {}", text);
}

}  // namespace kr::platform
