#include "krypton/ui/presentation/menu_shortcut.hpp"

#include <array>
#include <utility>

namespace kr {
namespace {
using Symbol = std::pair<std::string_view, std::string_view>;
constexpr std::array kKeys{
    Symbol{"Backspace", "\u232B"}, Symbol{"Delete", "\u2326"},
    Symbol{"Del", "\u2326"},       Symbol{"Enter", "\u21A9"},
    Symbol{"Return", "\u21A9"},    Symbol{"Escape", "\u238B"},
    Symbol{"Esc", "\u238B"},       Symbol{"Tab", "\u21E5"},
    Symbol{"Page Up", "\u21DE"},   Symbol{"Page Down", "\u21DF"},
    Symbol{"PgUp", "\u21DE"},      Symbol{"PgDn", "\u21DF"},
    Symbol{"Home", "\u2196"},      Symbol{"End", "\u2198"},
    Symbol{"Left", "\u2190"},      Symbol{"Right", "\u2192"},
    Symbol{"Up", "\u2191"},        Symbol{"Down", "\u2193"},
};
}  // namespace

std::string MenuShortcutSymbols(std::string_view shortcut) {
  const auto original = shortcut;
  bool primary = false, option = false, shift = false, windows = false;
  while (true) {
    const auto separator = shortcut.find('+');
    if (separator == std::string_view::npos)
      break;
    const auto modifier = shortcut.substr(0, separator);
    if (modifier == "Ctrl" || modifier == "Control" || modifier == "Cmd" ||
        modifier == "Command") {
      primary = true;
    } else if (modifier == "Alt" || modifier == "Option") {
      option = true;
    } else if (modifier == "Shift") {
      shift = true;
    } else if (modifier == "Win" || modifier == "Windows" ||
               modifier == "Super") {
      windows = true;
    } else {
      // A plus key is the remaining key, rather than another separator.
      // Preserve unfamiliar/localized combinations instead of guessing.
      if (shortcut != "+" && shortcut != "Num +")
        return std::string(original);
      break;
    }
    shortcut.remove_prefix(separator + 1);
  }
  if (shortcut.empty())
    return std::string(original);

  std::string result;
  result.reserve(original.size());
  if (windows)
    result += "\u229E";
  if (option)
    result += "\u2325";
  if (shift)
    result += "\u21E7";
  if (primary)
    result += "\u2318";
  if (!result.empty())
    result += "\u2009";
  for (const auto& [key, symbol] : kKeys) {
    if (shortcut == key) {
      result += symbol;
      return result;
    }
  }
  result += shortcut;
  return result;
}
}  // namespace kr
