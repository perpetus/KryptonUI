#pragma once

#include "krypton/ui/model/model.hpp"

namespace kr {

enum class AddressKeyRoute { kAddress, kText, kBrowser };

// Modified tab/page keys belong to the browser; selection keys stay with the
// native text editor instead of changing the autocomplete selection.
[[nodiscard]] constexpr AddressKeyRoute RouteAddressKey(
    AddressKey key,
    Flags<AddressModifier> modifiers) noexcept {
  if (modifiers.Has(AddressModifier::kControl) &&
      (key == AddressKey::kTab || key == AddressKey::kPageUp ||
       key == AddressKey::kPageDown || key == AddressKey::kDeleteMatch))
    return AddressKeyRoute::kBrowser;
  if (modifiers.Has(AddressModifier::kShift) &&
      (key == AddressKey::kUp || key == AddressKey::kDown ||
       key == AddressKey::kPageUp || key == AddressKey::kPageDown))
    return AddressKeyRoute::kText;
  return AddressKeyRoute::kAddress;
}

}  // namespace kr
