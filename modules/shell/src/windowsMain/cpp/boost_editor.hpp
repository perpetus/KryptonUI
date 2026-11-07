#pragma once

#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <memory>
#include <optional>
#include <vector>

#include "krypton/ui/presentation/boost.hpp"
#include "theme_pattern.hpp"
#include "transient_surface.hpp"
#include "view_context.hpp"

namespace kr::win {
class BoostEditor {
 public:
  BoostEditor(HWND parent, ViewContext& context);
  ~BoostEditor();
  void Render(const BoostEditorState& state,
              Rect client,
              float scale,
              xaml::ElementTheme theme,
              Color workspace_accent);
  void Close() noexcept;
  [[nodiscard]] bool OwnsInput() const noexcept {
    return requested_ || closing_;
  }

 private:
  void Build();
  void Open();
  void QueueHide(std::uint64_t generation) noexcept;
  bool Send(BoostEdit kind);
  controls::Button Action(std::wstring_view label,
                          const design::icons::Icon* icon,
                          std::move_only_function<void()> action);
  void Colors(int role);
  void ColorSettings();
  void PrepareFlyout();
  void Details();
  void Code();
  void Size();
  void Case();
  void Appearance();
  void Refresh();
  void BuildPaletteInput();
  void MovePalette(const xaml::Input::PointerRoutedEventArgs& args);
  void CancelPaletteDrag() noexcept;
  void ArrangePalette();
  void BuildHeaderInput();
  void MoveHeader(const xaml::Input::PointerRoutedEventArgs& args);
  void CancelHeaderDrag() noexcept;
  void ApplyPlacement();
  void ShowFlyout(const controls::Button& target,
                  const xaml::UIElement& content);
  ViewContext& context_;
  TransientSurface island_;
  controls::Primitives::Popup popup_{nullptr};
  controls::Border surface_{nullptr}, palette_{nullptr}, footer_{nullptr};
  controls::Grid root_{nullptr}, fonts_{nullptr}, header_{nullptr};
  controls::Border font_surface_{nullptr};
  controls::Canvas palette_canvas_{nullptr};
  controls::TextBlock dots_{nullptr}, title_{nullptr};
  controls::Button title_button_{nullptr}, close_{nullptr}, shuffle_{nullptr},
      background_{nullptr}, foreground_{nullptr}, accent_{nullptr};
  xaml::Shapes::Ellipse orbit_{nullptr};
  BoostPaletteState palette_state_;
  Color palette_background_, palette_foreground_;
  std::optional<std::uint32_t> palette_pointer_;
  BoostColorPoint palette_target_ = BoostColorPoint::kBackground;
  Point palette_offset_;
  foundation::Point palette_start_{}, palette_last_{};
  bool palette_dragged_ = false, palette_hit_ = false;
  bool palette_synced_ = false;
  xaml::Input::PointerEventHandler palette_pressed_{nullptr};
  xaml::UIElement::PointerMoved_revoker palette_moved_;
  xaml::UIElement::PointerReleased_revoker palette_released_;
  xaml::UIElement::PointerCaptureLost_revoker palette_lost_;
  xaml::UIElement::PointerCanceled_revoker palette_cancelled_;
  Rect panel_client_, panel_bounds_;
  std::optional<Point> panel_position_;
  std::optional<std::uint32_t> header_pointer_;
  foundation::Point header_start_{}, header_last_{};
  Point header_origin_;
  bool header_dragged_ = false, header_title_hit_ = false;
  xaml::Input::PointerEventHandler header_pressed_{nullptr};
  xaml::UIElement::PointerMoved_revoker header_moved_;
  xaml::UIElement::PointerReleased_revoker header_released_;
  xaml::UIElement::PointerCaptureLost_revoker header_lost_;
  xaml::UIElement::PointerCanceled_revoker header_cancelled_;
  controls::Button size_{nullptr}, case_{nullptr}, code_{nullptr},
      appearance_{nullptr}, adjust_{nullptr}, enabled_{nullptr};
  controls::Flyout flyout_{nullptr};
  controls::ColorPicker color_{nullptr};
  controls::ComboBox match_{nullptr};
  controls::TextBox name_{nullptr}, pattern_{nullptr}, code_text_{nullptr};
  controls::Slider size_slider_{nullptr};
  std::array<controls::Slider, 3> color_sliders_{nullptr, nullptr, nullptr};
  std::array<controls::Primitives::RangeBase::ValueChanged_revoker, 3>
      color_settings_changed_;
  Color workspace_accent_;
  std::vector<std::unique_ptr<NativeButton>> buttons_;
  std::vector<controls::Button> font_buttons_;
  std::size_t fixed_buttons_ = 0;
  xaml::Media::SolidColorBrush text_{nullptr}, tile_{nullptr}, muted_{nullptr};
  xaml::Media::SolidColorBrush surface_fill_{nullptr}, header_fill_{nullptr},
      dots_fill_{nullptr}, palette_fill_{nullptr}, accent_fill_{nullptr},
      accent_text_{nullptr};
  xaml::Media::ImageBrush palette_frame_{nullptr};
  ThemePattern palette_pattern_;
  xaml::Media::SolidColorBrush foreground_fill_{nullptr},
      background_fill_{nullptr};
  BoostDraft draft_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  std::uint64_t generation_ = 0, flyout_generation_ = 0, hide_queued_ = 0;
  bool closing_ = false, visible_ = false, flyout_open_ = false;
  bool requested_ = false, changing_ = false, dark_ = true,
       focus_pending_ = false;
  xaml::FrameworkElement::Loaded_revoker loaded_;
  xaml::FrameworkElement::SizeChanged_revoker palette_sized_;
  controls::Primitives::Popup::Closed_revoker closed_;
  controls::Primitives::FlyoutBase::Opened_revoker flyout_opened_;
  controls::Primitives::FlyoutBase::Closed_revoker flyout_closed_;
  xaml::UIElement::KeyDown_revoker key_;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker fallback_;
  controls::TextBox::TextChanged_revoker code_changed_;
  controls::ColorPicker::ColorChanged_revoker color_changed_;
  controls::Primitives::RangeBase::ValueChanged_revoker size_changed_;
};
}  // namespace kr::win
