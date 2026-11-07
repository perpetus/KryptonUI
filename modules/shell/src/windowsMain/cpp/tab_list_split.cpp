#include <algorithm>

#include "krypton/ui/presentation/multi_page.hpp"
#include "tab_list.hpp"

namespace kr::win {
void TabList::UpdateSplitRow(TabRow& row) {
  const auto model = context_.delegate.OnSplitRowRequested(row.id);
  if (!model) {
    if (row.split &&
        row.split->root.Visibility() != xaml::Visibility::Collapsed) {
      row.split->root.Visibility(xaml::Visibility::Collapsed);
      row.split->count = 0;
      row.configured = false;
      ApplyRowContent(row);
    }
    return;
  }
  if (!row.split) {
    row.split = std::make_unique<SplitRowChrome>();
    auto& split = *row.split;
    split.root = controls::Grid();
    split.root.VerticalAlignment(xaml::VerticalAlignment::Center);
    for (std::size_t i = 0; i < kMaxSplitMembers; ++i)
      AddColumn(split.root, 0, xaml::GridUnitType::Auto);
    for (std::size_t i = 0; i < 2; ++i) {
      controls::RowDefinition definition;
      definition.Height({0, xaml::GridUnitType::Auto});
      split.root.RowDefinitions().Append(definition);
    }
    for (std::size_t i = 0; i < kMaxSplitMembers; ++i) {
      auto button = controls::Button();
      button.Padding(UniformThickness(0));
      button.MinWidth(0);
      button.MinHeight(0);
      button.Width(kTokens.split_view.member_icon_size +
                   kTokens.split_view.member_gap);
      button.Height(kTokens.split_view.member_icon_size +
                    kTokens.split_view.active_indicator);
      auto indicator = controls::Border();
      auto icon = controls::Grid();
      auto image = controls::Image();
      image.Width(kTokens.split_view.member_icon_size);
      image.Height(kTokens.split_view.member_icon_size);
      auto glyph = controls::TextBlock();
      glyph.Text(L"\uE774");
      glyph.FontFamily(
          media::FontFamily(winrt::to_hstring(kTokens.type.icon_family)));
      glyph.FontSize(kTokens.split_view.member_icon_size);
      glyph.IsTextScaleFactorEnabled(false);
      glyph.HorizontalAlignment(xaml::HorizontalAlignment::Center);
      icon.Children().Append(image);
      icon.Children().Append(glyph);
      indicator.Child(icon);
      button.Content(indicator);
      split.root.Children().Append(button);
      split.buttons[i] = button;
      split.indicators[i] = indicator;
      split.images[i] = image;
      split.glyphs[i] = glyph;
      split.clicks[i] =
          button.Click(winrt::auto_revoke, [this, &row, i](auto&&, auto&&) {
            const auto tab = row.split->tabs[i];
            if (tab)
              context_.delegate.OnSplitPaneActivated(tab);
          });
    }
    controls::Grid::SetColumn(split.root, 0);
    row.grid.Children().Append(split.root);
  }
  auto& split = *row.split;
  const bool compact = icon_only_ || row.content.centered;
  row.preview->SetTarget({}, {}, {});
  split.count = model->count;
  if (split.root.Visibility() != xaml::Visibility::Visible)
    split.root.Visibility(xaml::Visibility::Visible);
  const auto alignment = compact ? xaml::HorizontalAlignment::Center
                                 : xaml::HorizontalAlignment::Left;
  if (split.root.HorizontalAlignment() != alignment)
    split.root.HorizontalAlignment(alignment);
  for (std::size_t i = 0; i < kMaxSplitMembers; ++i) {
    const bool visible = i < model->count;
    const auto visibility =
        visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    if (split.buttons[i].Visibility() != visibility)
      split.buttons[i].Visibility(visibility);
    split.tabs[i] = visible ? model->members[i].id : TabId{};
    if (!visible)
      continue;
    const auto& member = model->members[i];
    const auto column = static_cast<int>(compact ? i % 2 : i);
    const auto row_index = static_cast<int>(compact ? i / 2 : 0);
    if (controls::Grid::GetColumn(split.buttons[i]) != column)
      controls::Grid::SetColumn(split.buttons[i], column);
    if (controls::Grid::GetRow(split.buttons[i]) != row_index)
      controls::Grid::SetRow(split.buttons[i], row_index);
    const auto bitmap = Bitmap(member.favicon);
    if (split.images[i].Source() != bitmap)
      split.images[i].Source(bitmap);
    const auto image_visibility =
        bitmap ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    const auto glyph_visibility =
        bitmap ? xaml::Visibility::Collapsed : xaml::Visibility::Visible;
    if (split.images[i].Visibility() != image_visibility)
      split.images[i].Visibility(image_visibility);
    if (split.glyphs[i].Visibility() != glyph_visibility)
      split.glyphs[i].Visibility(glyph_visibility);
    if (split.glyphs[i].Foreground() != context_.brushes.text)
      split.glyphs[i].Foreground(context_.brushes.text);
    if (split.indicators[i].BorderBrush() != context_.brushes.text)
      split.indicators[i].BorderBrush(context_.brushes.text);
    const xaml::Thickness thickness{
        0, 0, 0, member.active ? kTokens.split_view.active_indicator : 0};
    if (split.indicators[i].BorderThickness() != thickness)
      split.indicators[i].BorderThickness(thickness);
    if (split.titles[i] != member.title) {
      split.titles[i] = member.title;
      const auto native_title = winrt::to_hstring(member.title);
      Accessible(split.buttons[i], native_title);
      controls::ToolTipService::SetToolTip(split.buttons[i],
                                           winrt::box_value(native_title));
    }
  }
  if (row.glyph.Visibility() != xaml::Visibility::Collapsed)
    row.glyph.Visibility(xaml::Visibility::Collapsed);
  if (row.image.Visibility() != xaml::Visibility::Collapsed)
    row.image.Visibility(xaml::Visibility::Collapsed);
  if (!row.content.centered) {
    const double width = static_cast<double>(model->count) *
                             (kTokens.split_view.member_icon_size +
                              kTokens.split_view.member_gap) +
                         kTokens.spacing.tab_icon_gap;
    const auto column = row.grid.ColumnDefinitions().GetAt(0);
    if (column.Width().Value != width)
      column.Width({width, xaml::GridUnitType::Pixel});
  }
}
}  // namespace kr::win
