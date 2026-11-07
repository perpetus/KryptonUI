#include "menu_item_style.hpp"

#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <string>

namespace kr::win {
void SetMenuItemResources(const controls::Control& control) {
  const auto& t = kTokens.app_menu;
  control.Padding({t.row_padding_inline, t.row_padding_block,
                   t.row_padding_inline, t.row_padding_block});
  control.FontFamily(
      xaml::Media::FontFamily(winrt::to_hstring(kTokens.type.family)));
  control.FontSize(kTokens.type.size);
}

xaml::Controls::ControlTemplate MenuItemTemplate(bool submenu, bool checkable) {
  std::wstring source =
      LR"(<ControlTemplate
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType=")";
  source += submenu ? L"MenuFlyoutSubItem\">" : L"MenuFlyoutItem\">";
  source +=
      LR"(
    <Grid x:Name="LayoutRoot" Padding="{TemplateBinding Padding}"
      Background="{TemplateBinding Background}" BorderBrush="{TemplateBinding BorderBrush}"
      BorderThickness="{TemplateBinding BorderThickness}" CornerRadius="{TemplateBinding CornerRadius}">
      <VisualStateManager.VisualStateGroups>
        <VisualStateGroup x:Name="CommonStates">
          <VisualState x:Name="Normal"/>
          <VisualState x:Name="PointerOver"><VisualState.Setters>
            <Setter Target="LayoutRoot.Background" Value="{ThemeResource MenuFlyoutItemBackgroundPointerOver}"/>
          </VisualState.Setters></VisualState>
          <VisualState x:Name="Pressed"><VisualState.Setters>
            <Setter Target="LayoutRoot.Background" Value="{ThemeResource MenuFlyoutItemBackgroundPressed}"/>
          </VisualState.Setters></VisualState>
          <VisualState x:Name="SubMenuOpened"><VisualState.Setters>
            <Setter Target="LayoutRoot.Background" Value="{ThemeResource MenuFlyoutItemBackgroundPointerOver}"/>
          </VisualState.Setters></VisualState>
          <VisualState x:Name="Disabled"><VisualState.Setters>
            <Setter Target="TextBlock.Foreground" Value="{ThemeResource MenuFlyoutItemForegroundDisabled}"/>
            <Setter Target="IconContent.Foreground" Value="{ThemeResource MenuFlyoutItemForegroundDisabled}"/>
            <Setter Target="CheckGlyph.Foreground" Value="{ThemeResource MenuFlyoutItemForegroundDisabled}"/>
            <Setter Target="KeyboardAcceleratorTextBlock.Foreground" Value="{ThemeResource MenuFlyoutItemForegroundDisabled}"/>
          </VisualState.Setters></VisualState>
        </VisualStateGroup>
        <VisualStateGroup x:Name="CheckPlaceholderStates">
          <VisualState x:Name="NoPlaceholder"/>
          <VisualState x:Name="CheckPlaceholder"/>
          <VisualState x:Name="IconPlaceholder"><VisualState.Setters>
            <Setter Target="IconRoot.Visibility" Value="Visible"/>
            <Setter Target="TextBlock.Margin" Value="$ICON_GAP"/>
          </VisualState.Setters></VisualState>
          <VisualState x:Name="CheckAndIconPlaceholder"><VisualState.Setters>
            <Setter Target="IconRoot.Visibility" Value="Visible"/>
            <Setter Target="TextBlock.Margin" Value="$ICON_GAP"/>
          </VisualState.Setters></VisualState>
        </VisualStateGroup>)";
  if (!submenu) {
    source +=
        LR"(
        <VisualStateGroup x:Name="CheckStates">
          <VisualState x:Name="Unchecked"/>
          <VisualState x:Name="Checked"><VisualState.Setters>
            <Setter Target="CheckGlyph.Opacity" Value="1"/>
          </VisualState.Setters></VisualState>
          <VisualState x:Name="UncheckedWithIcon"><VisualState.Setters>
            <Setter Target="IconRoot.Visibility" Value="Visible"/>
            <Setter Target="TextBlock.Margin" Value="$ICON_GAP"/>
          </VisualState.Setters></VisualState>
          <VisualState x:Name="CheckedWithIcon"><VisualState.Setters>
            <Setter Target="CheckGlyph.Opacity" Value="1"/>
            <Setter Target="IconRoot.Visibility" Value="Visible"/>
            <Setter Target="TextBlock.Margin" Value="$ICON_GAP"/>
          </VisualState.Setters></VisualState>
        </VisualStateGroup>
        <VisualStateGroup x:Name="KeyboardAcceleratorTextVisibility">
          <VisualState x:Name="KeyboardAcceleratorTextCollapsed"/>
          <VisualState x:Name="KeyboardAcceleratorTextVisible"><VisualState.Setters>
            <Setter Target="KeyboardAcceleratorTextBlock.Visibility" Value="Visible"/>
          </VisualState.Setters></VisualState>
        </VisualStateGroup>)";
  }
  source +=
      LR"(
      </VisualStateManager.VisualStateGroups>
      <Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/>
        <ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/>
      </Grid.ColumnDefinitions>
      <Viewbox x:Name="IconRoot" Width="$ICON_SIZE"
        Height="$ICON_SIZE" Visibility="Collapsed" VerticalAlignment="Center">
        <ContentPresenter x:Name="IconContent" Content="{TemplateBinding Icon}"
          Foreground="{TemplateBinding Foreground}"/>
      </Viewbox>
      <TextBlock x:Name="TextBlock" Grid.Column="1" Text="{TemplateBinding Text}"
        Foreground="{TemplateBinding Foreground}" FontFamily="{TemplateBinding FontFamily}"
        FontSize="{TemplateBinding FontSize}" TextTrimming="CharacterEllipsis" VerticalAlignment="Center"/>)";
  if (!submenu) {
    source +=
        LR"(
      <TextBlock x:Name="KeyboardAcceleratorTextBlock" Grid.Column="2"
        Text="{TemplateBinding KeyboardAcceleratorTextOverride}" Margin="$ICON_GAP"
        Foreground="{TemplateBinding Foreground}" FontFamily="$SHORTCUT_FAMILY" FontSize="$SHORTCUT_SIZE"
        Opacity="$SHORTCUT_OPACITY" FlowDirection="LeftToRight"
        VerticalAlignment="Center" Visibility="Collapsed"
        AutomationProperties.AccessibilityView="Raw"/>)";
  } else {
    source +=
        LR"(
      <TextBlock x:Name="KeyboardAcceleratorTextBlock" Grid.Column="2"
        Text="{TemplateBinding Tag}" Margin="$ICON_GAP"
        Foreground="{TemplateBinding Foreground}" FontFamily="$SHORTCUT_FAMILY" FontSize="$SHORTCUT_SIZE"
        Opacity="$SHORTCUT_OPACITY" FlowDirection="LeftToRight" VerticalAlignment="Center"
        AutomationProperties.AccessibilityView="Raw"/>)";
  }
  source +=
      LR"(
      <FontIcon x:Name="CheckGlyph" Grid.Column="3" FontFamily="{ThemeResource SymbolThemeFontFamily}"
        FontSize="$ICON_SIZE" Foreground="{TemplateBinding Foreground}"
        VerticalAlignment="Center" Margin="$ICON_GAP"
        AutomationProperties.AccessibilityView="Raw" )";
  if (submenu) {
    source += LR"(Glyph="&#xE974;" MirroredWhenRightToLeft="True"/>)";
  } else if (checkable) {
    source += LR"(Glyph="&#xE73E;" Opacity="0"/>)";
  } else {
    // Ordinary commands have no selection mark. Collapsing its placeholder
    // lets the shortcut align with the menu's trailing content edge.
    source += LR"(Glyph="&#xE73E;" Opacity="0" Visibility="Collapsed"/>)";
  }
  source += L"</Grid></ControlTemplate>";
  // XamlReader resolves resources before this detached template belongs to a
  // menu item. Keep token metrics in the markup; per-item resource references
  // can prevent every menu using this template from opening.
  const auto replace = [&source](std::wstring_view key,
                                 std::wstring_view value) {
    for (std::size_t offset = 0;
         (offset = source.find(key, offset)) != std::wstring::npos;
         offset += value.size())
      source.replace(offset, key.size(), value);
  };
  replace(L"$ICON_SIZE", std::to_wstring(kTokens.app_menu.icon_size));
  replace(L"$ICON_GAP", std::to_wstring(kTokens.app_menu.icon_gap) + L",0,0,0");
  replace(L"$SHORTCUT_FAMILY",
          winrt::to_hstring(kTokens.app_menu.shortcut_family));
  replace(L"$SHORTCUT_SIZE", std::to_wstring(kTokens.app_menu.shortcut_size));
  replace(L"$SHORTCUT_OPACITY",
          std::to_wstring(kTokens.app_menu.accelerator_opacity));
  if (submenu) {
    constexpr std::wstring_view original = L"CheckGlyph";
    constexpr std::wstring_view native = L"SubItemChevron";
    for (std::size_t offset = 0;
         (offset = source.find(original, offset)) != std::wstring::npos;
         offset += native.size())
      source.replace(offset, original.size(), native);
  }
  return xaml::Markup::XamlReader::Load(source).as<controls::ControlTemplate>();
}
}  // namespace kr::win
