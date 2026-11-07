#pragma once

#include <unordered_map>

#include "settings_style.hpp"
#include "view_context.hpp"

namespace kr::win {
class WorkspaceIconPicker;
// Native controls are retained by semantic label/workspace id. State and
// visibility decisions arrive from the common presenter.
class BrowserSettingsPanel {
 public:
  BrowserSettingsPanel(ViewContext& context, const SettingsStyle& style);
  ~BrowserSettingsPanel();
  void Render(const ShellViewModel::SettingsVm& vm,
              UiTextView text,
              bool refresh_text,
              bool refresh_workspaces);
  [[nodiscard]] const controls::StackPanel& Root() const noexcept {
    return root_;
  }
  [[nodiscard]] bool Focus(UiText label);
  [[nodiscard]] bool DialogOpen() const {
    return dialog_ && dialog_.IsLoaded();
  }
  void DismissDialog();

 private:
  struct Row {
    SettingsEntry entry;
    controls::Grid grid;
    controls::StackPanel labels;
    controls::TextBlock title, description, state;
    xaml::FrameworkElement control{nullptr};
    controls::Primitives::ButtonBase button{nullptr};
    controls::ComboBox choice{nullptr};
    controls::ToggleSwitch toggle{nullptr};
    controls::TextBlock value{nullptr};
    std::vector<SearchEngineId> engines;
    controls::Primitives::ButtonBase::Click_revoker clicked;
    controls::Primitives::Selector::SelectionChanged_revoker selected;
    controls::ToggleSwitch::Toggled_revoker toggled;
    xaml::FrameworkElement::SizeChanged_revoker sized;
  };
  struct WorkspaceRow {
    Space space;
    controls::StackPanel root;
    controls::TextBlock name, state, icon;
    controls::CommandBar commands;
    controls::AppBarButton activate, edit, remove;
    controls::Primitives::ButtonBase::Click_revoker activated, edited, removed;
  };
  Row& Build(const SettingsEntry& entry);
  void Reflow(Row& row);
  void Workspaces(const ShellViewModel::SettingsVm& vm);
  void EditWorkspace(Space value, bool remove);
  [[nodiscard]] const winrt::hstring& Text(UiText key) const;
  ViewContext& context_;
  const SettingsStyle& style_;
  std::array<controls::StackPanel, 2> groups_;
  std::array<controls::Border, 2> cards_;
  controls::StackPanel root_, workspaces_;
  std::unordered_map<UiText, std::unique_ptr<Row>> rows_;
  std::unordered_map<SpaceId, std::unique_ptr<WorkspaceRow>> spaces_;
  std::array<winrt::hstring, static_cast<std::size_t>(UiText::kCount)> text_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::ContentDialog dialog_{nullptr};
  std::shared_ptr<WorkspaceIconPicker> workspace_picker_;
  std::optional<SettingsSection> section_;
  std::optional<LayoutPrefs> layout_;
  std::uint64_t browser_revision_ = ~UINT64_C(0),
                service_revision_ = ~UINT64_C(0);
  bool reconciling_ = false, text_ready_ = false;
};
}  // namespace kr::win
