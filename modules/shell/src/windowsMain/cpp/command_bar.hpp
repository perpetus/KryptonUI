#pragma once

#include <winrt/Windows.UI.ViewManagement.h>

#include <memory>

#include "menu_material.hpp"
#include "suggestion_list.hpp"
#include "transient_surface.hpp"
#include "view_context.hpp"

namespace kr::win {

class CommandBar {
 public:
  CommandBar(HWND parent, ViewContext& context)
      : context_(context), island_(parent) {}
  ~CommandBar();
  void Render(const CommandBarVm& vm,
              Rect client,
              float scale,
              xaml::ElementTheme theme,
              const ImageStore* images);
  void Close() noexcept;
  void SetFocusBlocked(bool blocked) noexcept;
  void SetAddressAnchor(Rect anchor) noexcept;
  [[nodiscard]] bool IsOpen() const noexcept { return requested_open_; }
  void Raise() noexcept { island_.Raise(); }
  void SetText(UiTextView text);

 private:
  void EnsureCommandBar();
  void RenderCommandBar(const CommandBarVm& vm,
                        Rect client,
                        float scale,
                        xaml::ElementTheme theme,
                        const ImageStore* images);
  void Reposition() noexcept;
  void OpenWhenLoaded() noexcept;
  void FocusInput() noexcept;
  void ReportEdit();
  void QueueEdit();
  ViewContext& context_;
  ContentFade fade_;
  TransientSurface island_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::Primitives::Popup command_popup_{nullptr};
  MenuMaterial material_;
  controls::Grid owner_{nullptr};
  controls::Border surface_{nullptr};
  controls::Grid input_row_{nullptr};
  controls::ScrollViewer input_scroll_{nullptr};
  controls::TextBox command_input_{nullptr};
  std::unique_ptr<SuggestionList> suggestions_;
  winrt::Windows::UI::ViewManagement::UISettings text_settings_{nullptr};
  Rect client_;
  Rect address_anchor_;
  bool floating_ = true;
  bool menu_backdrop_ = false;
  std::uint32_t command_serial_ = 0;
  bool rendering_ = false;
  bool edit_pending_ = false;
  bool positioning_ = false;
  bool requested_open_ = false;
  bool focus_pending_ = false;
  bool focus_blocked_ = false;
  bool composing_ = false;
  AddressSelection selection_pending_;
  winrt::hstring placeholder_ = NativeText(UiText::kAddress);
  xaml::FlowDirection direction_ = xaml::FlowDirection::LeftToRight;
  xaml::UIElement::PreviewKeyDown_revoker command_key_;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker fallback_key_;
  controls::TextBox::TextChanged_revoker command_text_;
  controls::TextBox::SelectionChanged_revoker command_selection_;
  controls::TextBox::TextCompositionStarted_revoker composition_started_;
  controls::TextBox::TextCompositionChanged_revoker composition_changed_;
  controls::TextBox::TextCompositionEnded_revoker composition_ended_;
  xaml::UIElement::GotFocus_revoker focused_;
  xaml::UIElement::LostFocus_revoker blurred_;
  controls::Primitives::Popup::Closed_revoker popup_closed_;
  controls::Primitives::Popup::Opened_revoker popup_opened_;
  xaml::FrameworkElement::Loaded_revoker owner_loaded_, input_loaded_;
  xaml::FrameworkElement::SizeChanged_revoker surface_size_, owner_size_,
      input_size_;
  winrt::Windows::UI::ViewManagement::UISettings::TextScaleFactorChanged_revoker
      text_scale_changed_;
};

}  // namespace kr::win
