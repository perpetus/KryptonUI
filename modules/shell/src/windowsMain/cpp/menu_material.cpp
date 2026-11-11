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

// Transient islands can remain input-inactive while their browser is active.
// Follow WinUI's live theme/accessibility policy in a separate configuration,
// so island activation cannot put the material into its inactive fallback.
// This never changes native or XAML keyboard focus.
struct MenuBackdrop : media::SystemBackdropT<MenuBackdrop> {
  explicit MenuBackdrop(MenuMaterialKind selected) : selected(selected) {}

  struct Entry {
    Entry(const Target& target,
          const backdrops::SystemBackdropConfiguration& defaults)
        : target(target), defaults(defaults) {
      configuration.IsInputActive(true);
      Refresh();
    }
    ~Entry() { Close(); }
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;

    void Connect(MenuMaterialKind selected) {
      if (selected == MenuMaterialKind::kMica) {
        mica = backdrops::MicaController();
        mica.SetSystemBackdropConfiguration(configuration);
        mica.AddSystemBackdropTarget(target);
      } else {
        acrylic = backdrops::DesktopAcrylicController();
        acrylic.Kind(selected == MenuMaterialKind::kAcrylicThin
                         ? backdrops::DesktopAcrylicKind::Thin
                         : backdrops::DesktopAcrylicKind::Base);
        acrylic.SetSystemBackdropConfiguration(configuration);
        acrylic.AddSystemBackdropTarget(target);
      }
    }

    void Refresh() {
      configuration.Theme(defaults.Theme());
      configuration.HighContrastBackgroundColor(
          defaults.HighContrastBackgroundColor());
      configuration.IsHighContrast(defaults.IsHighContrast());
    }

    void Close() noexcept {
      if (mica) {
        try {
          mica.RemoveSystemBackdropTarget(target);
        } catch (...) {
          platform::WriteDefaultLog(LogLevel::kWarning,
                                    "Menu mica target could not detach");
        }
        try {
          mica.Close();
        } catch (...) {
          platform::WriteDefaultLog(LogLevel::kWarning,
                                    "Menu mica controller could not close");
        }
      }
      if (acrylic) {
        try {
          acrylic.RemoveSystemBackdropTarget(target);
        } catch (...) {
          platform::WriteDefaultLog(LogLevel::kWarning,
                                    "Menu acrylic target could not detach");
        }
        try {
          acrylic.Close();
        } catch (...) {
          platform::WriteDefaultLog(LogLevel::kWarning,
                                    "Menu acrylic controller could not close");
        }
      }
    }

    Target target{nullptr};
    backdrops::SystemBackdropConfiguration defaults{nullptr};
    backdrops::SystemBackdropConfiguration configuration;
    backdrops::MicaController mica{nullptr};
    backdrops::DesktopAcrylicController acrylic{nullptr};
  };

  void OnTargetConnected(const Target& target, const xaml::XamlRoot& root) {
    SystemBackdropT::OnTargetConnected(target, root);
    auto entry = std::make_unique<Entry>(
        target, GetDefaultSystemBackdropConfiguration(target, root));
    entry->Connect(selected);
    entries.push_back(std::move(entry));
  }

  void OnDefaultSystemBackdropConfigurationChanged(const Target& target,
                                                   const xaml::XamlRoot& root) {
    SystemBackdropT::OnDefaultSystemBackdropConfigurationChanged(target, root);
    // Copy theme/accessibility changes without forwarding the transient
    // island's inactive state to a visible menu's controller.
    for (const auto& entry : entries)
      if (entry->target == target) {
        entry->Refresh();
        break;
      }
  }

  void OnTargetDisconnected(const Target& target) {
    std::erase_if(entries,
                  [&](const auto& entry) { return entry->target == target; });
    SystemBackdropT::OnTargetDisconnected(target);
  }

  MenuMaterialKind selected;
  std::vector<std::unique_ptr<Entry>> entries;
};
}  // namespace

winrt::Microsoft::UI::Xaml::Media::SystemBackdrop MakeMenuBackdrop(
    MenuMaterialKind selected) {
  return winrt::make<MenuBackdrop>(selected)
      .as<winrt::Microsoft::UI::Xaml::Media::SystemBackdrop>();
}
}  // namespace kr::win
