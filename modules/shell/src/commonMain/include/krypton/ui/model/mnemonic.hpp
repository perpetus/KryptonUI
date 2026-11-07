#pragma once

#include <string>
#include <string_view>

namespace kr {
// A host label in which '&' marks the next character as the access key and
// "&&" is a literal ampersand. Only the first marked character is the key.
struct MnemonicLabel {
  std::string text;
  std::string access_key;  // UTF-8; empty when the label marks none
};

[[nodiscard]] MnemonicLabel ParseMnemonic(std::string_view label);
}  // namespace kr
