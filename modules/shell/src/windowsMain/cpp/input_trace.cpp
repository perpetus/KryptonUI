#include "input_trace.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace kr::win {

InputTrace::InputTrace() {
  const DWORD size = GetEnvironmentVariableW(L"KR_UI_INPUT_TRACE", nullptr, 0);
  if (!size)
    return;
  path_.resize(size);
  const DWORD written =
      GetEnvironmentVariableW(L"KR_UI_INPUT_TRACE", path_.data(), size);
  path_.resize(written < size ? written : 0);
}

void InputTrace::Record(InputTraceEvent event,
                        HWND window,
                        std::uint64_t generation,
                        std::uint64_t detail) noexcept {
  if (path_.empty())
    return;
  entries_[count_++ % entries_.size()] = {
      GetTickCount64(), reinterpret_cast<std::uintptr_t>(window), generation,
      event, detail};
}

void InputTrace::Dump() const noexcept try {
  if (path_.empty() || !count_)
    return;
  auto snapshot = std::make_unique<InputTrace>(*this);
  if (TrySubmitThreadpoolCallback(
          [](PTP_CALLBACK_INSTANCE, void* context) noexcept {
            const std::unique_ptr<InputTrace> data(
                static_cast<InputTrace*>(context));
            FILE* file = nullptr;
            if (_wfopen_s(&file, data->path_.c_str(), L"w") != 0 || !file)
              return;
            std::fputs("time_ms,event,hwnd,generation,detail\n", file);
            const auto first = data->count_ > data->entries_.size()
                                   ? data->count_ - data->entries_.size()
                                   : 0;
            for (auto i = first; i < data->count_; ++i) {
              const auto& entry = data->entries_[i % data->entries_.size()];
              std::fprintf(file, "%llu,%u,%llu,%llu,%llu\n", entry.time,
                           static_cast<unsigned>(entry.event),
                           static_cast<unsigned long long>(entry.window),
                           static_cast<unsigned long long>(entry.generation),
                           static_cast<unsigned long long>(entry.detail));
            }
            std::fclose(file);
          },
          snapshot.get(), nullptr))
    (void)snapshot.release();
} catch (...) {
  OutputDebugStringW(L"KryptonUI input trace snapshot could not be queued\n");
}

}  // namespace kr::win
