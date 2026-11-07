#pragma once

// expect declarations: every <platform>Main source set defines these
// (the "actual" side). Only one platform is compiled into a build, so a
// missing actual is a link error.

#include <memory>
#include <string_view>

#include "krypton/ui/core/log.hpp"
#include "krypton/ui/core/result.hpp"
#include "krypton/ui/platform/shell_view.hpp"

namespace kr::platform {

// Short, stable platform name, e.g. "windows-winui3".
[[nodiscard]] std::string_view Name() noexcept;

// Cheap check that this machine can run the native UI: OS version, runtime
// presence. Must not create any UI.
[[nodiscard]] Result<void> CheckSupport() noexcept;
// Final UI-thread teardown after all shells are destroyed.
[[nodiscard]] Result<void> ShutdownRuntime() noexcept;

// Creates the native UI inside params.parent_window. `delegate` outlives the
// returned view.
[[nodiscard]] Result<std::unique_ptr<ShellView>> CreateShellView(
    const ShellViewParams& params,
    ShellViewDelegate& delegate) noexcept;

// Where log lines go when the host hasn't provided a sink.
void WriteDefaultLog(LogLevel level, std::string_view text) noexcept;

}  // namespace kr::platform
