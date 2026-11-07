#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Windowing.h>

#include <array>
#include <memory>
#include <optional>
#include <unordered_map>

#include "island_hide.hpp"
#include "native_input.hpp"
#include "view_context.hpp"

namespace kr::win {
// Both windows use virtualized, keyed native collections. Records retain only
// owned native strings; no borrowed view-model data survives Render.
class BrowserToolWindow {
 public:
  BrowserToolWindow(HWND owner, ViewContext& context, bool tasks);
  ~BrowserToolWindow();
  void Render(const ShellViewModel& vm);

 private:
  struct Row;
  struct Record {
    IInspectable item{nullptr};
    std::array<winrt::hstring, 6> text;
    std::array<std::string, 6> utf8;
    media::ImageSource image{nullptr};
    ImageId image_id;
    winrt::hstring glyph;
    TaskCategory category = TaskCategory::kTab;
    PermissionKind kind = PermissionKind::kOther;
    std::array<PermissionSetting, 5> choices{};
    std::size_t choice_count = 0;
    PermissionSetting setting = PermissionSetting::kDefault;
    bool editable = false, selected = false;
    bool selection_present = false;
    std::uint64_t seen = 0;
    Row* row = nullptr;
  };
  struct Row {
    std::uint64_t id = 0;
    controls::ListViewItem container{nullptr};
    controls::Grid root{nullptr};
    std::array<controls::TextBlock, 5> cells{nullptr, nullptr, nullptr, nullptr,
                                             nullptr};
    controls::ComboBox choices{nullptr};
    controls::TextBlock icon{nullptr};
    controls::Image image{nullptr};
    bool updating = false;
    std::array<PermissionSetting, 5> choice_values{};
    std::size_t choice_count = 0;
    winrt::hstring tooltip;
    winrt::hstring accessible_name;
    controls::ComboBox::SelectionChanged_revoker changed;
  };
  void Build();
  void Dismiss();
  void ReleaseImages();
  void Reconcile(const BrowserToolsVm& vm);
  void UpdateRow(Record& record);
  void Realize(const controls::ContainerContentChangingEventArgs& args);
  void UpdateSelection();
  bool ReconcileItems();
  HWND owner_;
  NativeInputTarget input_target_;
  IslandHide hide_;
  ViewContext& context_;
  bool tasks_, visible_ = false, binding_ = false;
  std::uint32_t serial_ = 0;
  std::uint64_t revision_ = ~std::uint64_t{0}, generation_ = 0;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  xaml::Window window_{nullptr};
  winrt::Microsoft::UI::Windowing::AppWindow app_window_{nullptr};
  controls::Grid root_{nullptr};
  controls::TextBlock title_{nullptr}, subtitle_{nullptr}, status_{nullptr},
      empty_{nullptr};
  controls::TextBox search_{nullptr};
  controls::ComboBox filter_{nullptr};
  controls::ListView list_{nullptr};
  controls::InfoBar error_{nullptr};
  controls::Button action_{nullptr};
  std::array<std::unique_ptr<NativeButton>, 5> headers_;
  std::unique_ptr<NativeButton> action_button_;
  std::unordered_map<std::uint64_t, Record> records_;
  std::unordered_map<void*, std::unique_ptr<Row>> rows_;
  foundation::Collections::IObservableVector<IInspectable> items_{nullptr};
  std::vector<IInspectable> displayed_, desired_;
  std::vector<TaskId> selected_;
  std::string error_text_, search_text_, subtitle_text_, status_text_;
  std::array<winrt::hstring, 5> setting_labels_;
  std::optional<std::pair<TaskColumn, bool>> sort_;
  controls::TextBox::TextChanged_revoker searched_;
  controls::ComboBox::SelectionChanged_revoker filtered_;
  controls::ListViewBase::SelectionChanged_revoker selected_event_;
  controls::ListViewBase::ContainerContentChanging_revoker realized_;
  xaml::UIElement::DoubleTapped_revoker activated_;
  xaml::UIElement::KeyDown_revoker key_;
  winrt::event_token closing_{};
  bool closing_registered_ = false;
  std::optional<winrt::Microsoft::UI::Windowing::TitleBarTheme> title_theme_;
};
}  // namespace kr::win
