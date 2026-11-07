#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.h>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/model/settings.hpp"

namespace kr::win {
[[nodiscard]] winrt::Microsoft::UI::Xaml::Media::SystemBackdrop
MakeAcrylicThinBackdrop();
[[nodiscard]] winrt::Microsoft::UI::Xaml::Media::SystemBackdrop
MakeActiveMenuBackdrop(MenuMaterialKind selected);

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

// Each popup retains its native backdrop. WinUI owns activation, transparency
// availability and theme transitions; no composition polling or custom blur.
class MenuMaterial {
 public:
  [[nodiscard]] winrt::Microsoft::UI::Xaml::Media::SystemBackdrop
  For(bool enabled, MenuMaterialKind selected, bool active_popup = false) {
    if (!enabled)
      return nullptr;
    if (selected_ != selected || active_popup_ != active_popup) {
      active_popup_ = active_popup;
      backdrop_ = nullptr;
      selected_ = selected;
    }
    if (!backdrop_) {
      namespace media = winrt::Microsoft::UI::Xaml::Media;
      if (active_popup)
        backdrop_ = MakeActiveMenuBackdrop(selected);
      else if (selected == MenuMaterialKind::kMica)
        backdrop_ = media::MicaBackdrop();
      else if (selected == MenuMaterialKind::kAcrylic)
        backdrop_ = media::DesktopAcrylicBackdrop();
      else if (selected == MenuMaterialKind::kAcrylicThin)
        backdrop_ = MakeAcrylicThinBackdrop();
    }
    return backdrop_;
  }

 private:
  bool active_popup_ = false;
  MenuMaterialKind selected_ = MenuMaterialKind::kDefault;
  winrt::Microsoft::UI::Xaml::Media::SystemBackdrop backdrop_{nullptr};
};
}  // namespace kr::win
