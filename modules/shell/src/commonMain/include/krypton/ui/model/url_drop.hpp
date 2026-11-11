#pragma once

#include <algorithm>
#include <string_view>

namespace kr {
// Drop text must be a single web URL, never a search, script or local file.
// Chromium performs full URL and navigation-policy validation at the host seam.
[[nodiscard]] inline bool IsDroppedWebUrl(std::string_view text) noexcept {
  const auto equal = [](char a, char b) {
    return (a >= 'A' && a <= 'Z' ? a + ('a' - 'A') : a) == b;
  };
  const auto starts = [&](std::string_view prefix) {
    return text.size() > prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin(), equal);
  };
  return (starts("https://") || starts("http://")) &&
         std::ranges::none_of(
             text, [](unsigned char c) { return c <= ' ' || c == 0x7f; });
}
}  // namespace kr
