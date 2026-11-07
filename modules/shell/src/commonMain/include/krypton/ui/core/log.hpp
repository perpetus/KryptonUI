#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace kr {

enum class LogLevel { kVerbose, kInfo, kWarning, kError };

// Where log lines go. The Shell routes them to the host's KrBrowser::log;
// before that (or without a host) they go to platform::WriteDefaultLog.
using LogSink = void (*)(void* context, LogLevel level, std::string_view text);

void SetLogSink(LogSink sink, void* context) noexcept;
void ClearLogSink(void* context) noexcept;

void Log(LogLevel level, std::string_view text) noexcept;

template <typename... Args>
void Log(LogLevel level, std::format_string<Args...> format, Args&&... args) {
  Log(level,
      std::string_view(std::format(format, std::forward<Args>(args)...)));
}

}  // namespace kr
