#include "favorites_grid.hpp"

#include <winrt/Microsoft.UI.Xaml.Input.h>

#include <algorithm>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
using namespace controls;

FavoritesGrid::FavoritesGrid(ViewContext& context) : context_(context) {
  root_.ColumnSpacing(kTokens.spacing.favorite_gap);
  root_.RowSpacing(kTokens.spacing.favorite_gap);
}

void FavoritesGrid::Render(const ShellViewModel& vm) {
  root_.Visibility(vm.favorites.empty() ? xaml::Visibility::Collapsed
                                        : xaml::Visibility::Visible);
  const auto columns = std::max(std::size_t{1}, vm.favorite_columns);
  const bool reflow = columns_ != columns;
  if (reflow) {
    root_.ColumnDefinitions().Clear();
    for (std::size_t column = 0; column < columns; ++column)
      AddColumn(root_, 1, xaml::GridUnitType::Star);
    columns_ = columns;
  }
  const auto rows = (vm.favorites.size() + columns - 1) / columns;
  while (root_.RowDefinitions().Size() > rows)
    root_.RowDefinitions().RemoveAtEnd();
  while (root_.RowDefinitions().Size() < rows)
    AddRow(root_, kTokens.spacing.favorite_height, xaml::GridUnitType::Pixel);
  ++generation_;
  std::size_t index = 0;
  for (const auto& tab : vm.favorites) {
    const auto [entry, inserted] = tiles_.try_emplace(tab.id);
    auto& tile = entry->second;
    if (inserted) {
      // A favorite needs just a native button and its current icon. Avoid a
      // toolbar glyph, content grid and plain tooltip that would immediately
      // be replaced by the favicon and hover preview.
      tile.button = std::make_unique<NativeButton>();
      tile.button->control = Button();
      tile.button->control.MinWidth(0);
      tile.button->control.MinHeight(0);
      tile.button->control.Padding(UniformThickness(0));
      tile.button->control.BorderThickness(
          UniformThickness(kTokens.spacing.favorite_active_border_width));
      tile.button->control.Foreground(context_.brushes.icon);
      // Tabs activate on press rather than release.
      tile.button->control.ClickMode(ClickMode::Press);
      tile.button->click = tile.button->control.Click(
          winrt::auto_revoke, [this, id = tab.id](auto&&, auto&&) {
            try {
              context_.delegate.OnTabActivated(id);
            } catch (...) {
              platform::WriteDefaultLog(LogLevel::kError,
                                        "Favorite activation failed");
            }
          });
      tile.button->control.HorizontalAlignment(
          xaml::HorizontalAlignment::Stretch);
      tile.button->control.HorizontalContentAlignment(
          xaml::HorizontalAlignment::Center);
      tile.button->control.VerticalContentAlignment(
          xaml::VerticalAlignment::Center);
      tile.button->control.Height(kTokens.spacing.favorite_height);
      tile.button->control.Margin(UniformThickness(0));
      tile.button->control.CornerRadius(
          UniformCorners(kTokens.radius.favorite));
      tile.button->control.Resources().Insert(
          winrt::box_value(L"ButtonBackgroundPointerOver"),
          context_.brushes.favorite_hover);
      tile.button->right_tapped = tile.button->control.RightTapped(
          winrt::auto_revoke,
          [this, id = tab.id, native = tile.button.get()](
              auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
            context_.request_menu(MenuKind::kTab, id.value,
                                  context_.bounds(native->control));
            args.Handled(true);
          });
      tile.preview =
          std::make_unique<TabHoverCard>(context_, tile.button->control);
    }
    if (inserted || reflow || tile.index != index) {
      Grid::SetRow(tile.button->control, static_cast<int>(index / columns));
      Grid::SetColumn(tile.button->control, static_cast<int>(index % columns));
      tile.index = index;
    }
    tile.seen = generation_;
    const auto border = tab.active ? context_.brushes.favorite_active_border
                                   : context_.brushes.transparent;
    if (inserted || tile.button->control.BorderBrush() != border) {
      tile.button->control.BorderBrush(border);
      for (const auto key :
           {L"ButtonBorderBrush", L"ButtonBorderBrushPointerOver",
            L"ButtonBorderBrushPressed", L"ButtonBorderBrushDisabled"})
        tile.button->control.Resources().Insert(winrt::box_value(key), border);
    }
    const auto fallback =
        tab.active ? context_.brushes.active : context_.brushes.favorite;
    const auto background =
        context_.glass.For(tile.button->control, GlassRole::kEssential,
                           fallback, kTokens.radius.favorite);
    if (tile.button->control.Background() != background) {
      tile.button->control.Background(background);
      const bool glass = background != fallback;
      tile.button->control.Resources().Insert(
          winrt::box_value(L"ButtonBackgroundPointerOver"),
          glass ? background : context_.brushes.favorite_hover);
      tile.button->control.Resources().Insert(
          winrt::box_value(L"ButtonBackgroundPressed"),
          glass ? background : context_.brushes.active);
    }
    if (tile.title != tab.title || tile.url != tab.url) {
      tile.title = tab.title;
      tile.url = tab.url;
      Accessible(tile.button->control, winrt::to_hstring(tab.title));
      tile.preview->Configure(MeasureTabPreviewForLayout(vm.client, vm.layout));
      tile.preview->SetTarget(tab.id, tab.title, tab.url);
    }
    if (inserted || vm.changed.Has(Dirty::kText))
      tile.preview->SetText(vm.text);
    tile.preview->Render(vm);
    const auto bitmap = context_.images.Get(tab.favicon, vm.images);
    if (bitmap) {
      if (!tile.image) {
        tile.image = controls::Image();
        tile.image.Width(kTokens.spacing.icon_size);
        tile.image.Height(kTokens.spacing.icon_size);
      }
      if (tile.image.Source() != bitmap)
        tile.image.Source(bitmap);
      if (tile.button->control.Content() != tile.image)
        tile.button->control.Content(tile.image);
    } else {
      if (tile.image && tile.image.Source())
        tile.image.Source(nullptr);
      if (!tile.button->glyph)
        tile.button->glyph = Glyph(glyphs::kGlobe);
      if (tile.button->control.Content() != tile.button->glyph)
        tile.button->control.Content(tile.button->glyph);
    }
    if (inserted)
      root_.Children().Append(tile.button->control);
    ++index;
  }
  std::erase_if(tiles_, [&](auto& entry) {
    if (entry.second.seen == generation_)
      return false;
    std::uint32_t position;
    if (root_.Children().IndexOf(entry.second.button->control, position))
      root_.Children().RemoveAt(position);
    return true;
  });
}

void FavoritesGrid::AppendAnchors(std::vector<Anchor>& anchors,
                                  bool visible) const {
  for (const auto& [id, tile] : tiles_)
    anchors.push_back({AnchorKind::kTab, id.value,
                       context_.bounds(tile.button->control), visible});
}

void FavoritesGrid::ClosePreviews() {
  for (const auto& [id, tile] : tiles_)
    tile.preview->Close();
}

void FavoritesGrid::SetText(UiTextView text) {
  for (const auto& [id, tile] : tiles_)
    tile.preview->SetText(text);
}

bool FavoritesGrid::IsPreviewOpen() const {
  return std::ranges::any_of(
      tiles_, [](const auto& entry) { return entry.second.preview->IsOpen(); });
}

}  // namespace kr::win
