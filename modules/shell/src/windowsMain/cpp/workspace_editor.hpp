#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "transient_surface.hpp"
#include "view_context.hpp"
#include "workspace_icon_picker.hpp"

namespace kr::win {
// Uses the platform's adaptive ContentDialog rather than a fixed-size form.
class WorkspaceEditor {
 public:
  WorkspaceEditor(HWND parent,
                  ViewContext& context,
                  std::move_only_function<void(bool)> focus_blocked)
      : context_(context),
        island_(parent),
        focus_blocked_(std::move(focus_blocked)) {}
  ~WorkspaceEditor();
  WorkspaceEditor(const WorkspaceEditor&) = delete;
  WorkspaceEditor& operator=(const WorkspaceEditor&) = delete;
  void Render(const WorkspaceEditorVm& vm,
              UiTextView text,
              Rect client,
              float scale,
              xaml::ElementTheme theme,
              bool text_changed);

 private:
  void Build();
  void TryOpen();
  void FinishClose();
  void Close() noexcept;
  ViewContext& context_;
  TransientSurface island_;
  std::move_only_function<void(bool)> focus_blocked_;
  controls::ContentDialog dialog_{nullptr};
  controls::TextBox name_{nullptr};
  std::unique_ptr<WorkspaceIconPicker> glyph_;
  controls::TextBlock error_{nullptr};
  winrt::Windows::Foundation::IAsyncOperation<controls::ContentDialogResult>
      operation_{nullptr};
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  winrt::hstring required_text_;
  std::uint32_t serial_ = 0;
  bool requested_ = false, showing_ = false, closing_ = false,
       text_ready_ = false;
  xaml::FrameworkElement::Loaded_revoker loaded_;
  controls::ContentDialog::PrimaryButtonClick_revoker save_;
  controls::ContentDialog::SecondaryButtonClick_revoker remove_;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker fallback_;
};
}  // namespace kr::win
