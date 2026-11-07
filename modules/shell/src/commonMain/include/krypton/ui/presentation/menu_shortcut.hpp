#pragma once

#include <string>
#include <string_view>

namespace kr {
// Presentation only: Chromium's shortcut text remains the accessible key name
// and the host still owns dispatch. The primary modifier uses the requested
// Mac Command symbol, including for Windows Ctrl bindings.
[[nodiscard]] std::string MenuShortcutSymbols(std::string_view shortcut);
}  // namespace kr
