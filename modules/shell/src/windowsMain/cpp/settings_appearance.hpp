#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <array>
#include <string_view>
#include <vector>

#include "settings_style.hpp"
#include "view_context.hpp"

namespace kr::win {
// One of the illustrated layouts copied beside the module (assets/settings).
[[nodiscard]] winrt::Windows::Foundation::Uri LayoutImage(
    std::string_view file);

class AppearanceSettings {
 public:
  AppearanceSettings(ViewContext& context, const SettingsStyle& style);
  AppearanceSettings(const AppearanceSettings&) = delete;
  AppearanceSettings& operator=(const AppearanceSettings&) = delete;
  void Render(const ShellViewModel::SettingsVm& vm,
              UiTextView text,
              const ThemeVm& theme,
              bool refresh_text);
  [[nodiscard]] const controls::StackPanel& Root() const noexcept {
    return root_;
  }
  bool Focus(UiText key);
  void InvalidateText() noexcept { text_ready_ = false; }

 private:
  struct LabelEntry {
    controls::TextBlock label{nullptr};
    UiText key;
  };
  struct SettingRow {
    controls::Grid grid{nullptr};
    controls::TextBlock label{nullptr};
    xaml::FrameworkElement control{nullptr};
    bool stacked = false;
    xaml::FrameworkElement::SizeChanged_revoker resized;
  };
  struct ToggleRow {
    controls::Grid row{nullptr};
    controls::ToggleSwitch control{nullptr};
    LayoutOption option{};
    controls::ToggleSwitch::Toggled_revoker changed;
  };
  struct LayoutTile {
    LayoutChoice choice{};
    controls::RadioButton control{nullptr};
    controls::Border frame{nullptr};
    controls::Image image{nullptr};
    controls::TextBlock label{nullptr};
    media::Imaging::BitmapImage bitmap{nullptr};
    controls::Primitives::ToggleButton::Checked_revoker checked;
  };
  controls::TextBlock Text(UiText key, bool heading = false);
  controls::Grid Row(UiText key, const xaml::UIElement& control);
  void BuildLayouts(bool horizontal, controls::Grid group);
  void Reflow();
  void ReflowRows();
  ViewContext& context_;
  const SettingsStyle& style_;
  std::array<controls::StackPanel, 4> sections_, groups_;
  controls::StackPanel root_;
  controls::ComboBox scheme_, material_, icon_pack_, glass_, search_bar_;
  std::array<controls::ComboBoxItem, 3> search_bar_choices_{nullptr, nullptr,
                                                            nullptr};
  controls::Primitives::Selector::SelectionChanged_revoker search_bar_changed_;
  controls::Grid horizontal_, vertical_, sidebar_width_row_;
  controls::NumberBox sidebar_width_;
  controls::ToggleSwitch hover_images_, hover_memory_,
      search_bar_menu_backdrop_;
  std::vector<LabelEntry> labels_;
  std::vector<SettingRow> rows_;
  std::vector<ToggleRow> toggles_;
  std::vector<LayoutTile> tiles_;
  std::array<controls::ComboBoxItem, 3> schemes_{nullptr, nullptr, nullptr};
  std::array<controls::ComboBoxItem, 5> materials_{nullptr, nullptr, nullptr,
                                                   nullptr, nullptr};
  std::array<controls::ComboBoxItem, kIconPackCount> icon_packs_{
      nullptr, nullptr, nullptr, nullptr, nullptr};
  std::array<controls::ComboBoxItem, 3> glass_modes_{nullptr, nullptr, nullptr};
  bool reconciling_ = false, reflowing_ = false, reflowing_rows_ = false,
       text_ready_ = false;
  controls::Primitives::Selector::SelectionChanged_revoker scheme_changed_,
      material_changed_, icon_pack_changed_, glass_changed_;
  controls::NumberBox::ValueChanged_revoker width_changed_;
  controls::ToggleSwitch::Toggled_revoker images_changed_, memory_changed_,
      search_bar_backdrop_changed_;
  xaml::FrameworkElement::SizeChanged_revoker horizontal_sized_,
      vertical_sized_;
};
}  // namespace kr::win
