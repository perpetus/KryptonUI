#include "krypton/ui/model/address.hpp"

#include <limits>

namespace kr {
std::optional<std::uint32_t> AddressTextLength(
    std::string_view text,
    std::optional<AddressSelection> selection) noexcept {
  std::uint64_t length = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto first = static_cast<unsigned char>(text[index++]);
    std::uint32_t value = first;
    unsigned remaining = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xC2 && first <= 0xDF) {
      value &= 0x1F;
      remaining = 1;
      minimum = 0x80;
    } else if (first >= 0xE0 && first <= 0xEF) {
      value &= 0x0F;
      remaining = 2;
      minimum = 0x800;
    } else if (first >= 0xF0 && first <= 0xF4) {
      value &= 0x07;
      remaining = 3;
      minimum = 0x10000;
    } else if (first >= 0x80 || first == 0) {
      return {};
    }
    if (remaining > text.size() - index)
      return {};
    for (unsigned part = 0; part < remaining; ++part) {
      const auto next = static_cast<unsigned char>(text[index++]);
      if ((next & 0xC0) != 0x80)
        return {};
      value = (value << 6) | (next & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF))
      return {};
    if (value > 0xFFFF) {
      if (selection &&
          (selection->start == length + 1 || selection->end == length + 1))
        return {};
      length += 2;
    } else {
      ++length;
    }
    if (length > std::numeric_limits<std::uint32_t>::max())
      return {};
  }
  if (selection &&
      (selection->start > selection->end || selection->end > length))
    return {};
  return static_cast<std::uint32_t>(length);
}
}  // namespace kr
