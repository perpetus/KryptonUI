#pragma once

#include "krypton/ui/core/result.hpp"

namespace kr::windows {

[[nodiscard]] Result<void> CheckRuntimeSupport() noexcept;
[[nodiscard]] Result<void> EnsureRuntime() noexcept;
[[nodiscard]] Result<void> ShutdownRuntime() noexcept;
[[nodiscard]] bool PreTranslateMessage(const void* message) noexcept;

}  // namespace kr::windows
