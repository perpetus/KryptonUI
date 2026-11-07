#include "krypton/ui/core/log.hpp"

#include "krypton/ui/platform/platform.hpp"

namespace kr {

namespace {
// The UI lives on one thread, so plain statics are enough.
LogSink g_sink = nullptr;
void* g_context = nullptr;
}  // namespace

void SetLogSink(LogSink sink, void* context) noexcept {
  // First window wins; later windows share its sink until it goes away.
  if (g_sink == nullptr) {
    g_sink = sink;
    g_context = context;
  }
}

void ClearLogSink(void* context) noexcept {
  if (g_context == context) {
    g_sink = nullptr;
    g_context = nullptr;
  }
}

void Log(LogLevel level, std::string_view text) noexcept {
  if (g_sink != nullptr) {
    g_sink(g_context, level, text);
  } else {
    platform::WriteDefaultLog(level, text);
  }
}

}  // namespace kr
