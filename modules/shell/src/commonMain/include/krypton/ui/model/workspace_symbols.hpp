#pragma once

#include <algorithm>
#include <span>
#include <string_view>

namespace kr {
inline constexpr std::string_view kDefaultWorkspaceSymbol = "\uE80F";
struct WorkspaceSymbol {
  std::string_view value, label;
  bool icon;
};
// Zen's picker separates simple icons from emoji. Fluent symbols are stored as
// ordinary UTF-8 glyphs, preserving the existing workspace/profile contract.
extern const std::span<const WorkspaceSymbol> kWorkspaceSymbols;

[[nodiscard]] inline bool WorkspaceSymbolMatches(const WorkspaceSymbol& symbol,
                                                 std::string_view query) {
  const auto fold = [](unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
  };
  return query.empty() || symbol.value.find(query) != std::string_view::npos ||
         std::search(symbol.label.begin(), symbol.label.end(), query.begin(),
                     query.end(), [&](unsigned char a, unsigned char b) {
                       return fold(a) == fold(b);
                     }) != symbol.label.end();
}
}  // namespace kr
