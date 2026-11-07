#include "krypton/ui/shell/shell.hpp"

namespace kr {
Result<void> Shell::SetLoadProgress(const LoadProgressUpdate& update) {
  auto result = store_.SetLoadProgress(update);
  if (result && !store_.dirty().none())
    ScheduleRender();
  return result;
}

void Shell::OnAddressFocusChanged(bool focused) {
  if (store_.ui().address_focused == focused)
    return;
  store_.SetAddressFocused(focused);
  NotifyAddressState();
  if (!store_.dirty().none())
    ScheduleRender();
}
}  // namespace kr
