#pragma once

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <array>
#include <memory>
#include <vector>

#include "krypton/ui/design/icon_packs.generated.hpp"
#include "xaml_factory.hpp"

namespace kr::win {
namespace media = xaml::Media;
using design::icon_packs::IconRole;
class NativeMotion;
class IconView;

// The chrome's current icon pack. Every IconView registers here, so a new
// pack reaches each one directly; nothing is rebuilt.
class IconSet {
 public:
  IconSet(const NativeMotion& motion, const Brushes& brushes)
      : motion_(motion), brushes_(brushes) {}
  IconSet(const IconSet&) = delete;
  IconSet& operator=(const IconSet&) = delete;

  // Swaps every icon; `animate` is false for the first render.
  void SetPack(IconPack pack, bool animate);
  [[nodiscard]] IconPack pack() const noexcept { return pack_; }
  [[nodiscard]] bool Animates() const noexcept;

 private:
  friend class IconView;
  const NativeMotion& motion_;
  const Brushes& brushes_;
  IconPack pack_ = IconPack::kFluent;
  std::vector<IconView*> icons_;
};

// One pack icon: a path in a Viewbox, stroked for outline packs and filled
// otherwise. A pack or role change crossfades with a short scale, on the
// compositor; the second layer is created on the first animated swap.
class IconView {
 public:
  IconView(IconSet& set, IconRole role, double size, const media::Brush& brush);
  ~IconView();
  IconView(const IconView&) = delete;
  IconView& operator=(const IconView&) = delete;

  void SetRole(IconRole role);
  void SetBrush(const media::Brush& brush);
  [[nodiscard]] IconRole role() const noexcept { return role_; }
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

 private:
  friend class IconSet;
  struct Layer {
    controls::Viewbox box{nullptr};
    controls::Canvas canvas{nullptr};
    xaml::Shapes::Path path{nullptr};
    winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation fade{nullptr};
    winrt::Microsoft::UI::Composition::Vector3KeyFrameAnimation scale{nullptr};
    const design::icon_packs::PackIcon* icon = nullptr;
  };
  struct CachedPath {
    const design::icon_packs::PackIcon* icon;
    xaml::Shapes::Path path{nullptr};
  };
  void Apply(bool animate);
  void Paint(Layer& layer);
  void PaintBrush(Layer& layer) const;
  void Settle();
  Layer MakeLayer() const;

  IconSet& set_;
  IconRole role_;
  double size_;
  media::Brush brush_;
  controls::Grid root_;
  std::array<Layer, 2> layers_;
  std::vector<CachedPath> paths_;
  std::size_t front_ = 0;
};

// An icon button whose glyph follows the icon pack.
[[nodiscard]] std::unique_ptr<NativeButton> PackButton(IconSet& icons,
                                                       const Brushes& brushes,
                                                       IconRole role,
                                                       std::wstring_view name,
                                                       Task action);
}  // namespace kr::win
