#include "xaml_factory.hpp"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <format>
#include <limits>

#include "krypton/ui/platform/platform.hpp"
#include "native_theme.hpp"

namespace kr::win {
namespace media = xaml::Media;

winrt::Windows::UI::Color NativeColor(Color color) {
  return {color.a(), color.r(), color.g(), color.b()};
}

void Accessible(const xaml::DependencyObject& element, std::wstring_view name) {
  xaml::Automation::AutomationProperties::SetName(element,
                                                  winrt::hstring(name));
}

void SetToolTip(const xaml::DependencyObject& element, std::wstring_view text) {
  controls::ToolTipService::SetToolTip(element,
                                       winrt::box_value(winrt::hstring(text)));
}

winrt::hstring NativeText(UiText id, UiTextView text) {
  return winrt::to_hstring(text[id]);
}

void SetButtonText(NativeButton& button, const winrt::hstring& text) {
  if (!button.text_initialized || button.accessible_name != text) {
    Accessible(button.control, text);
    SetToolTip(button.control, text);
    button.accessible_name = text;
    button.text_initialized = true;
  }
  if (button.label && button.label.Text() != text)
    button.label.Text(text);
}

controls::ColumnDefinition AddColumn(const controls::Grid& grid,
                                     double width,
                                     xaml::GridUnitType unit) {
  controls::ColumnDefinition result;
  result.Width({width, unit});
  grid.ColumnDefinitions().Append(result);
  return result;
}

controls::RowDefinition AddRow(const controls::Grid& grid,
                               double height,
                               xaml::GridUnitType unit) {
  controls::RowDefinition result;
  result.Height({height, unit});
  grid.RowDefinitions().Append(result);
  return result;
}

controls::TextBlock Label(std::wstring_view text, double size) {
  controls::TextBlock result;
  result.Text(winrt::hstring(text));
  result.FontFamily(media::FontFamily(winrt::to_hstring(kTokens.type.family)));
  result.FontSize(size);
  result.VerticalAlignment(xaml::VerticalAlignment::Center);
  result.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
  return result;
}

controls::TextBlock Glyph(std::wstring_view text, double size) {
  auto result = Label(text, size);
  result.FontFamily(
      media::FontFamily(winrt::to_hstring(kTokens.type.icon_family)));
  result.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  return result;
}

controls::Viewbox SourceIcon(const Brushes& brushes,
                             const design::icons::Icon& icon) {
  return SourceIcon(brushes.icon, icon);
}

controls::Viewbox SourceIcon(const xaml::Media::Brush& fill,
                             const design::icons::Icon& icon) {
  const auto markup = std::format(
      LR"(<Path xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Data="{}" />)",
      icon.data);
  const auto path =
      xaml::Markup::XamlReader::Load(markup).as<xaml::Shapes::Path>();
  if (icon.stroke > 0) {
    path.Stroke(fill);
    path.StrokeThickness(icon.stroke);
    path.StrokeStartLineCap(media::PenLineCap::Round);
    path.StrokeEndLineCap(media::PenLineCap::Round);
    path.StrokeLineJoin(media::PenLineJoin::Round);
  } else {
    path.StrokeThickness(0);
    path.Fill(fill);
  }
  controls::Canvas canvas;
  canvas.Width(icon.width);
  canvas.Height(icon.height);
  canvas.Children().Append(path);
  controls::Viewbox result;
  result.Width(kTokens.spacing.icon_size);
  result.Height(kTokens.spacing.icon_size);
  result.Stretch(media::Stretch::Uniform);
  result.Child(canvas);
  return result;
}

std::unique_ptr<NativeButton> IconButton(const Brushes& brushes,
                                         std::wstring_view glyph,
                                         std::wstring_view name,
                                         Task action) {
  auto result = std::make_unique<NativeButton>();
  auto& button = result->control = controls::Button();
  result->glyph = Glyph(glyph);
  button.Content(result->glyph);
  button.Width(kTokens.spacing.button_size);
  button.Height(kTokens.spacing.button_size);
  button.Margin(UniformThickness(kTokens.spacing.button_margin));
  button.MinWidth(0);
  button.MinHeight(0);
  button.Padding(UniformThickness(0));
  button.BorderThickness(UniformThickness(0));
  button.CornerRadius(UniformCorners(kTokens.radius.button));
  button.Foreground(brushes.icon);
  Accessible(button, name);
  SetToolTip(button, name);
  result->accessible_name = winrt::hstring(name);
  result->text_initialized = true;
  result->click = button.Click(
      winrt::auto_revoke, [action = std::move(action)](auto&&, auto&&) mutable {
        try {
          action();
        } catch (...) {
          platform::WriteDefaultLog(LogLevel::kError,
                                    "Native button callback failed");
        }
      });
  return result;
}

std::unique_ptr<NativeButton> SourceButton(const Brushes& brushes,
                                           const design::icons::Icon& icon,
                                           std::wstring_view name,
                                           Task action) {
  auto result = IconButton(brushes, L"", name, std::move(action));
  result->control.Content(SourceIcon(brushes, icon));
  return result;
}

std::unique_ptr<NativeButton> CaptionButton(const Brushes& brushes,
                                            std::wstring_view glyph,
                                            std::wstring_view name,
                                            Task action) {
  auto result = IconButton(brushes, glyph, name, std::move(action));
  result->control.Width(kTokens.caption.button_width);
  result->control.Height(kTokens.spacing.toolbar_height);
  result->control.Margin(UniformThickness(0));
  result->control.CornerRadius(UniformCorners(0));
  result->glyph.FontSize(kTokens.caption.glyph_size);
  return result;
}

std::unique_ptr<NativeButton> RowButton(const Brushes& brushes,
                                        const xaml::UIElement& lead,
                                        std::wstring_view label,
                                        std::wstring_view name,
                                        Task action) {
  auto result = IconButton(brushes, L"", name, std::move(action));
  controls::Grid content;
  AddColumn(content, 0, xaml::GridUnitType::Auto);
  AddColumn(content, 1, xaml::GridUnitType::Star);
  content.ColumnSpacing(kTokens.spacing.tab_icon_gap);
  content.Children().Append(lead);
  result->label = Label(label, kTokens.type.size);
  result->label.Foreground(brushes.muted);
  controls::Grid::SetColumn(result->label, 1);
  content.Children().Append(result->label);
  result->control.Content(content);
  result->control.Width(std::numeric_limits<double>::quiet_NaN());
  result->control.Height(kTokens.spacing.tab_height);
  result->control.Margin(UniformThickness(0));
  result->control.Padding(xaml::Thickness{kTokens.spacing.tab_padding, 0,
                                          kTokens.spacing.tab_padding, 0});
  result->control.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  result->control.HorizontalContentAlignment(
      xaml::HorizontalAlignment::Stretch);
  return result;
}

}  // namespace kr::win
