#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Windows.Graphics.h>

#include <array>
#include <memory>

#include "krypton/ui/presentation/theme_editor.hpp"
#include "theme_canvas.hpp"
#include "theme_controls.hpp"
#include "transient_surface.hpp"

namespace kr::win {

class ThemeEditor {
 public:
  ThemeEditor(HWND parent, ViewContext& context)
      : context_(context), island_(parent) {}
  ~ThemeEditor();
  void Render(const ShellViewModel& vm);
  void Close() noexcept;
  void Raise() noexcept { island_.Raise(); }
  [[nodiscard]] bool IsOpen() const noexcept { return requested_open_; }

 private:
  void Build();
  void OpenWhenLoaded() noexcept;
  void SetText(UiTextView text);
  std::unique_ptr<NativeButton> Button(const design::icons::Icon& icon,
                                       ThemeEdit edit);
  ViewContext& context_;
  TransientSurface island_;
  ContentFade fade_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::Grid owner_{nullptr};
  controls::Primitives::Popup popup_{nullptr};
  controls::Border surface_{nullptr};
  controls::Border controls_card_{nullptr};
  media::LinearGradientBrush controls_card_fill_{nullptr};
  std::array<media::GradientStop, 2> controls_card_stops_{nullptr, nullptr};
  controls::ScrollViewer scroll_{nullptr};
  controls::StackPanel content_{nullptr}, schemes_{nullptr}, actions_{nullptr};
  controls::Grid board_{nullptr}, presets_{nullptr};
  std::unique_ptr<ThemeCanvas> canvas_;
  std::unique_ptr<ThemeControls> controls_;
  std::array<std::unique_ptr<NativeButton>, 3> scheme_buttons_;
  std::unique_ptr<NativeButton> add_, remove_, harmony_, previous_, next_;
  struct Swatch {
    std::unique_ptr<NativeButton> button;
    std::array<controls::Border, 3> layers;
    std::array<media::RadialGradientBrush, 2> brushes;
    std::array<media::GradientStop, 4> stops;
    media::SolidColorBrush base;
  };
  std::array<Swatch, kMaxThemePresetsPerPage> swatches_;
  std::size_t first_preset_ = 0, preset_count_ = 0, preset_columns_ = 0;
  bool requested_open_ = false, text_ready_ = false;
  xaml::FrameworkElement::Loaded_revoker loaded_;
  xaml::FrameworkElement::Loaded_revoker canvas_loaded_;
  xaml::FrameworkElement::SizeChanged_revoker owner_sized_;
  controls::Primitives::Popup::Closed_revoker closed_;
  xaml::UIElement::KeyDown_revoker key_;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker fallback_key_;
};
}  // namespace kr::win
