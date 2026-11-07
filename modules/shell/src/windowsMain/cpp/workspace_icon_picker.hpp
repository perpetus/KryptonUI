#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "view_context.hpp"

namespace kr::win {
class WorkspaceIconPicker {
 public:
  explicit WorkspaceIconPicker(ViewContext& context, bool allow_empty = false);
  ~WorkspaceIconPicker();
  void SetValue(std::string_view value);
  void Close() noexcept;
  [[nodiscard]] std::string Value() const { return value_; }
  [[nodiscard]] const controls::StackPanel& Root() const { return root_; }
  // Header, choose, search, icons, emoji, custom emoji.
  void SetLabels(std::array<winrt::hstring, 6> labels);
  void SetText(UiTextView text);

 private:
  void EnsurePopup();
  void Filter();
  ViewContext& context_;
  controls::StackPanel root_;
  controls::TextBlock header_, preview_glyph_;
  controls::Button preview_;
  controls::Flyout flyout_{nullptr};
  controls::RadioButtons mode_{nullptr};
  controls::TextBox search_{nullptr}, custom_{nullptr};
  controls::GridView grid_{nullptr};
  winrt::Windows::Foundation::Collections::IObservableVector<IInspectable>
      items_{nullptr};
  std::vector<IInspectable> filtered_, boxed_;
  std::array<winrt::hstring, 6> labels_;
  std::string value_;
  bool changing_ = false;
  bool allow_empty_ = false;
  controls::Button::Click_revoker opened_;
  controls::TextBox::TextChanged_revoker searched_, edited_;
  controls::RadioButtons::SelectionChanged_revoker mode_changed_;
  controls::ListViewBase::ContainerContentChanging_revoker containers_;
  foundation::TypedEventHandler<controls::ListViewBase,
                                controls::ContainerContentChangingEventArgs>
      realize_{nullptr};
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::ListViewBase::ItemClick_revoker selected_;
};
}  // namespace kr::win
