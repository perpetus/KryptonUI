#pragma once

#include <winrt/Microsoft.UI.Dispatching.h>

#include <memory>
#include <vector>

#include "krypton/ui/platform/shell_view.hpp"

namespace kr::win {

// Reusable one-shot timers. No timer runs while its task slot is empty.
class DelayedTasks {
 public:
  DelayedTasks();
  ~DelayedTasks();
  void Post(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
            Task task,
            std::chrono::milliseconds delay);
  void Reset() noexcept;

 private:
  struct Entry;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  std::vector<std::unique_ptr<Entry>> entries_;
};

}  // namespace kr::win
