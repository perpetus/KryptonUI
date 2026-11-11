#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/model/settings.hpp"

namespace kr::win {
[[nodiscard]] winrt::Microsoft::UI::Xaml::Media::SystemBackdrop
MakeMenuBackdrop(MenuMaterialKind selected);

[[nodiscard]] inline MenuMaterialKind ResolveMenuMaterial(
    MenuMaterialKind selected) noexcept {
  if (selected != MenuMaterialKind::kDefault)
    return selected;
  const auto material = design::kTokens.menu.material;
  return material == "mica"           ? MenuMaterialKind::kMica
         : material == "acrylic"      ? MenuMaterialKind::kAcrylic
         : material == "acrylic-thin" ? MenuMaterialKind::kAcrylicThin
                                      : MenuMaterialKind::kSolid;
}

// Each popup retains its native backdrop. Its material stays active without
// activating the island; WinUI owns theme, accessibility and transparency
// policy.
class MenuMaterial {
 public:
  [[nodiscard]] winrt::Microsoft::UI::Xaml::Media::SystemBackdrop For(
      bool enabled,
      MenuMaterialKind selected) {
    selected = ResolveMenuMaterial(selected);
    if (!enabled || selected == MenuMaterialKind::kSolid)
      return nullptr;
    if (selected_ != selected) {
      backdrop_ = nullptr;
      selected_ = selected;
    }
    if (!backdrop_)
      backdrop_ = MakeMenuBackdrop(selected);
    return backdrop_;
  }

 private:
  MenuMaterialKind selected_ = MenuMaterialKind::kDefault;
  winrt::Microsoft::UI::Xaml::Media::SystemBackdrop backdrop_{nullptr};
};
}  // namespace kr::win
