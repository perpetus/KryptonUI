#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
  void RenderProfiles(const WorkspaceEditorVm& vm, UiTextView text);
  void Submit(const controls::ContentDialogButtonClickEventArgs& args);
  [[nodiscard]] bool CreatingProfile() const;
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
  // Profile choice: existing persistent profiles, then "New profile".
  controls::ComboBox profile_{nullptr};
  controls::TextBox new_profile_{nullptr};
  controls::TextBlock profile_note_{nullptr};
  std::vector<ProfileId> profile_ids_;
  std::vector<std::string> profile_names_;
  std::uint32_t profile_serial_ = 0;
  bool profiles_shown_ = false;
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
  controls::ComboBox::SelectionChanged_revoker profile_changed_;
};
}  // namespace kr::win
