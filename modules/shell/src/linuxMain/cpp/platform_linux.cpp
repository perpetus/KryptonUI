// Linux actuals: no native UI yet. CheckSupport() reports "unsupported", so the
// host keeps its own (Views) UI.

#include <cstdio>
#include <print>

#include "krypton/ui/platform/platform.hpp"

namespace kr::platform {
Result<void> ShutdownRuntime() noexcept {
  return {};
}

std::string_view Name() noexcept {
  return "linux-unimplemented";
}

Result<void> CheckSupport() noexcept {
  return Fail(ErrorCode::kUnsupported, "there is no Linux implementation yet");
}

Result<std::unique_ptr<ShellView>> CreateShellView(
    const ShellViewParams& /*params*/,
    ShellViewDelegate& /*delegate*/) noexcept {
  return Fail(ErrorCode::kUnsupported, "there is no Linux implementation yet");
}

void WriteDefaultLog(LogLevel /*level*/, std::string_view text) noexcept {
  std::println(stderr, "[krypton-ui] {}", text);
}

}  // namespace kr::platform
