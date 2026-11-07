#include "delayed_tasks.hpp"

#include <winrt/Windows.Foundation.h>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {

struct DelayedTasks::Entry {
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
  winrt::event_token tick{};
  Task task;

  ~Entry() {
    if (!timer)
      return;
    timer.Tick(tick);
    try {
      timer.Stop();
    } catch (...) {
      // The dispatcher may already be shutting down.
    }
  }
};

DelayedTasks::DelayedTasks() = default;

DelayedTasks::~DelayedTasks() {
  Reset();
}

void DelayedTasks::Reset() noexcept {
  lifetime_.reset();
  entries_.clear();
}

void DelayedTasks::Post(
    const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
    Task task,
    std::chrono::milliseconds delay) {
  Entry* slot = nullptr;
  for (const auto& entry : entries_) {
    if (!entry->task) {
      slot = entry.get();
      break;
    }
  }
  if (!slot) {
    auto entry = std::make_unique<Entry>();
    entry->timer = queue.CreateTimer();
    entry->timer.IsRepeating(false);
    const std::weak_ptr<int> weak = lifetime_;
    entry->tick = entry->timer.Tick([weak, pending = entry.get()](auto&&,
                                                                  auto&&) {
      if (weak.expired())
        return;
      auto ready = std::move(pending->task);
      try {
        if (ready)
          ready();
      } catch (...) {
        platform::WriteDefaultLog(LogLevel::kError, "Delayed UI task failed");
      }
    });
    entries_.push_back(std::move(entry));
    slot = entries_.back().get();
  }
  slot->timer.Interval(delay);
  slot->task = std::move(task);
  try {
    slot->timer.Start();
  } catch (...) {
    slot->task = nullptr;
    throw;
  }
}

}  // namespace kr::win
