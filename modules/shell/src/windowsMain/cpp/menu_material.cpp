#include "menu_material.hpp"

#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {
namespace composition = winrt::Microsoft::UI::Composition;
namespace backdrops = composition::SystemBackdrops;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace media = winrt::Microsoft::UI::Xaml::Media;
using Target = composition::ICompositionSupportsSystemBackdrop;

// DesktopAcrylicBackdrop does not expose Kind. Follow its upstream target
// lifecycle, using WinUI's default configuration for activation/theme policy.
struct AcrylicThinBackdrop : media::SystemBackdropT<AcrylicThinBackdrop> {
  struct Entry {
    Entry(const Target& value,
          const backdrops::SystemBackdropConfiguration& configuration)
        : target(value) {
      try {
        controller.Kind(backdrops::DesktopAcrylicKind::Thin);
        controller.AddSystemBackdropTarget(target);
        controller.SetSystemBackdropConfiguration(configuration);
      } catch (...) {
        Close();
        throw;
      }
    }
    ~Entry() { Close(); }
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;

    void Close() noexcept {
      try {
        controller.RemoveSystemBackdropTarget(target);
      } catch (...) {
        platform::WriteDefaultLog(LogLevel::kWarning,
                                  "Menu acrylic target could not detach");
      }
      try {
        controller.Close();
      } catch (...) {
        platform::WriteDefaultLog(LogLevel::kWarning,
                                  "Menu acrylic controller could not close");
      }
    }

    Target target{nullptr};
    backdrops::DesktopAcrylicController controller;
  };

  void OnTargetConnected(const Target& target,
                         const winrt::Microsoft::UI::Xaml::XamlRoot& root) {
    SystemBackdropT::OnTargetConnected(target, root);
    const auto configuration =
        GetDefaultSystemBackdropConfiguration(target, root);
    entries.push_back(std::make_unique<Entry>(target, configuration));
  }

  void OnTargetDisconnected(const Target& target) {
    SystemBackdropT::OnTargetDisconnected(target);
    std::erase_if(entries,
                  [&](const auto& entry) { return entry->target == target; });
  }

  std::vector<std::unique_ptr<Entry>> entries;
};
// Owned popup targets do not receive top-level activation. Their lifetime is
// already scoped to an active browser interaction, so use an active material
// configuration rather than the island input site's inactive fallback color.
struct ActiveMenuBackdrop : media::SystemBackdropT<ActiveMenuBackdrop> {
  explicit ActiveMenuBackdrop(MenuMaterialKind selected) : selected(selected) {}
  struct Entry {
    Entry(const Target& value,
          const xaml::XamlRoot& root,
          MenuMaterialKind selected)
        : target(value) {
      configuration.IsInputActive(true);
      if (const auto element =
              root.Content().try_as<xaml::FrameworkElement>()) {
        Theme(element);
        themed = element.ActualThemeChanged(
            winrt::auto_revoke, [this](const xaml::FrameworkElement& element,
                                       auto&&) { Theme(element); });
      }
      if (selected == MenuMaterialKind::kMica) {
        mica = backdrops::MicaController();
        mica.AddSystemBackdropTarget(target);
        mica.SetSystemBackdropConfiguration(configuration);
      } else {
        acrylic = backdrops::DesktopAcrylicController();
        acrylic.Kind(selected == MenuMaterialKind::kAcrylicThin
                         ? backdrops::DesktopAcrylicKind::Thin
                         : backdrops::DesktopAcrylicKind::Base);
        acrylic.AddSystemBackdropTarget(target);
        acrylic.SetSystemBackdropConfiguration(configuration);
      }
    }
    ~Entry() {
      themed.revoke();
      try {
        if (mica) {
          mica.RemoveSystemBackdropTarget(target);
          mica.Close();
        }
      } catch (...) {
      }
      try {
        if (acrylic) {
          acrylic.RemoveSystemBackdropTarget(target);
          acrylic.Close();
        }
      } catch (...) {
      }
    }
    void Theme(const xaml::FrameworkElement& element) {
      configuration.Theme(element.ActualTheme() == xaml::ElementTheme::Dark
                              ? backdrops::SystemBackdropTheme::Dark
                              : backdrops::SystemBackdropTheme::Light);
    }
    Target target{nullptr};
    backdrops::SystemBackdropConfiguration configuration;
    backdrops::MicaController mica{nullptr};
    backdrops::DesktopAcrylicController acrylic{nullptr};
    xaml::FrameworkElement::ActualThemeChanged_revoker themed;
  };
  void OnTargetConnected(const Target& target, const xaml::XamlRoot& root) {
    SystemBackdropT::OnTargetConnected(target, root);
    entries.push_back(std::make_unique<Entry>(target, root, selected));
  }
  void OnTargetDisconnected(const Target& target) {
    SystemBackdropT::OnTargetDisconnected(target);
    std::erase_if(entries,
                  [&](const auto& entry) { return entry->target == target; });
  }
  MenuMaterialKind selected;
  std::vector<std::unique_ptr<Entry>> entries;
};
}  // namespace

winrt::Microsoft::UI::Xaml::Media::SystemBackdrop MakeAcrylicThinBackdrop() {
  return winrt::make<AcrylicThinBackdrop>()
      .as<winrt::Microsoft::UI::Xaml::Media::SystemBackdrop>();
}
winrt::Microsoft::UI::Xaml::Media::SystemBackdrop MakeActiveMenuBackdrop(
    MenuMaterialKind selected) {
  return winrt::make<ActiveMenuBackdrop>(selected)
      .as<winrt::Microsoft::UI::Xaml::Media::SystemBackdrop>();
}
}  // namespace kr::win
