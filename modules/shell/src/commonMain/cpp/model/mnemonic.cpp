#include "krypton/ui/model/mnemonic.hpp"

#include <algorithm>

namespace kr {
namespace {
// Byte length of the UTF-8 sequence starting with `lead`. Invalid leads count
// as one byte so a malformed label still advances.
std::size_t SequenceLength(unsigned char lead) noexcept {
  if (lead >= 0xF0 && lead <= 0xF4)
    return 4;
  if (lead >= 0xE0)
    return lead <= 0xEF ? 3 : 1;
  if (lead >= 0xC2)
    return 2;
  return 1;
}
}  // namespace

MnemonicLabel ParseMnemonic(std::string_view label) {
  MnemonicLabel result;
  result.text.reserve(label.size());
  for (std::size_t index = 0; index < label.size(); ++index) {
    if (label[index] != '&') {
      result.text.push_back(label[index]);
      continue;
    }
    if (++index == label.size())
      break;
    if (label[index] == '&') {
      result.text.push_back('&');
      continue;
    }
    const auto length =
        std::min(SequenceLength(static_cast<unsigned char>(label[index])),
                 label.size() - index);
    if (result.access_key.empty() && label[index] != ' ')
      result.access_key = label.substr(index, length);
    result.text.append(label.substr(index, length));
    index += length - 1;
  }
  return result;
}
}  // namespace kr
