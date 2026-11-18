#pragma once

#include <array>
#include <memory>
#include <unordered_map>

#include "settings_style.hpp"
#include "view_context.hpp"

namespace kr::win {
class WorkspaceIconPicker;

class LocalModelsPanel {
 public:
  LocalModelsPanel(ViewContext& context, const SettingsStyle& style);
  ~LocalModelsPanel();
  void Render(const LocalModelsState& state,
              UiTextView text,
              bool refresh_text);
  [[nodiscard]] bool DialogOpen() const noexcept { return dialog_ != nullptr; }
  void DismissDialog();
  [[nodiscard]] const controls::StackPanel& Root() const noexcept {
    return root_;
  }

 private:
  struct InstalledRow {
    controls::Grid root;
    controls::TextBlock name;
    controls::Button remove;
    controls::Primitives::ButtonBase::Click_revoker clicked;
  };
  struct Group {
    controls::Border card{nullptr};
    controls::StackPanel content;
    controls::TextBlock status, active;
    controls::ComboBox picker;
    controls::Button transfer, select;
    controls::TextBlock downloaded, empty;
    controls::StackPanel installed;
    std::unordered_map<std::uint32_t, std::unique_ptr<InstalledRow>>
        installed_rows;
    std::vector<LocalModelRow> rows;
    controls::Primitives::Selector::SelectionChanged_revoker changed;
    controls::Primitives::ButtonBase::Click_revoker transferred, selected;
  };
  struct VoiceChoice {
    std::string key;
    winrt::hstring label;
    friend bool operator==(const VoiceChoice&, const VoiceChoice&) = default;
  };
  void ReconcileVoicePickers();
  [[nodiscard]] const LocalModelRow* Selected(const Group& group) const;
  void UpdateSelected(Group& group);
  void ReconcileInstalled(Group& group);
  void Invoke(Group& group, LocalModelCommand command);
  ViewContext& context_;
  controls::StackPanel root_;
  controls::TextBlock resources_, consent_;
  std::array<Group, 2> groups_;
  controls::TextBlock voice_heading_{nullptr};
  std::array<controls::ComboBox, 3> voice_pickers_;
  std::array<std::vector<VoiceChoice>, 3> voice_choices_;
  std::array<controls::Primitives::Selector::SelectionChanged_revoker, 3>
      voice_changed_;
  std::array<winrt::hstring, 3> voice_sizes_;
  std::optional<std::uint32_t> voice_pack_;
  LocalModelsState state_;
  winrt::hstring checking_, installed_, missing_, download_, downloading_,
      remove_, active_voice_;
  std::array<winrt::hstring, 2> first_use_titles_, first_use_text_;
  winrt::hstring continue_, cancel_;
  winrt::hstring downloaded_, no_downloads_;
  controls::ContentDialog dialog_{nullptr};
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  bool reconciling_ = false;
  bool structured_voice_ = false;
};
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
