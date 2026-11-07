#pragma once

#include <array>
#include <vector>

#include "view_context.hpp"

namespace kr::win {
// Inline Library editor: it shares the Library's island and focus ownership.
class RoutingForm {
 public:
  explicit RoutingForm(ViewContext& context);
  void Render(const RoutingEditorState& state, const ShellViewModel& vm);
  void Close() noexcept;
  [[nodiscard]] const controls::ScrollViewer& Root() const { return root_; }
  [[nodiscard]] bool IsOpen() const noexcept { return open_; }

 private:
  void Submit();
  ViewContext& context_;
  controls::ScrollViewer root_;
  controls::StackPanel content_;
  controls::TextBlock title_, when_, destination_, error_;
  controls::TextBox pattern_;
  controls::ComboBox workspace_;
  controls::CheckBox enabled_, new_only_;
  controls::Button cancel_, save_;
  std::array<controls::Primitives::ToggleButton, 4> matches_;
  std::array<controls::Button::Click_revoker, 4> match_clicks_;
  controls::Button::Click_revoker cancelled_, saved_;
  xaml::UIElement::KeyDown_revoker key_;
  std::vector<SpaceId> choices_;
  RoutingDraft draft_;
  bool open_ = false;
};
}  // namespace kr::win
