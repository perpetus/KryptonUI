#include "favorites_grid.hpp"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include <algorithm>
#include <cmath>

#include "krypton/ui/platform/platform.hpp"
#include "native_motion.hpp"

namespace kr::win {
using namespace controls;
namespace hosting = xaml::Hosting;
using winrt::Windows::Foundation::Numerics::float2;

FavoritesGrid::FavoritesGrid(ViewContext& context) : context_(context) {
  root_.ColumnSpacing(kTokens.spacing.favorite_gap);
  root_.RowSpacing(kTokens.spacing.favorite_gap);
}

bool FavoritesGrid::Render(const ShellViewModel& vm) {
  const bool order_changed =
      order_.size() != vm.favorites.size() ||
      !std::equal(order_.begin(), order_.end(), vm.favorites.begin(),
                  [](TabId id, const TabItemVm& tab) { return id == tab.id; });
  if (order_changed) {
    order_.clear();
    for (const auto& tab : vm.favorites)
      order_.push_back(tab.id);
  }
  const auto columns = std::max(std::size_t{1}, vm.favorite_columns);
  const bool reflow = columns_ != columns;
  if (reflow) {
    root_.ColumnDefinitions().Clear();
    for (std::size_t column = 0; column < columns; ++column)
      AddColumn(root_, 1, xaml::GridUnitType::Star);
    columns_ = columns;
  }
  LayoutRows();
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
    if (dragging_)
      tile.preview->Close();
    else
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
  if (dragging_ && dragged_ && !tiles_.contains(dragged_)) {
    // The dragged Essential closed; the list cancels its drag on this render.
    dragged_ = {};
    gap_.reset();
  }
  if (settling_) {
    // The host has answered the drop. Tiles it moved are already in their
    // cells, so their offsets go at once; a declined drop slides back.
    settling_ = false;
    Settle(order_changed);
  }
  return order_changed;
}

void FavoritesGrid::LayoutRows() {
  auto cells = order_.size();
  // An added tab takes one more cell; an empty grid offers one to drop into.
  if (dragging_ && accepts_tabs_ && !dragged_ && (gap_ || cells == 0))
    ++cells;
  else if (settling_ && !dragged_ && gap_)
    ++cells;
  const auto columns = std::max(std::size_t{1}, columns_);
  const auto rows = (cells + columns - 1) / columns;
  while (root_.RowDefinitions().Size() > rows)
    root_.RowDefinitions().RemoveAtEnd();
  while (root_.RowDefinitions().Size() < rows)
    AddRow(root_, kTokens.spacing.favorite_height, xaml::GridUnitType::Pixel);
  const auto visibility =
      cells == 0 ? xaml::Visibility::Collapsed : xaml::Visibility::Visible;
  if (root_.Visibility() != visibility)
    root_.Visibility(visibility);
}

TabId FavoritesGrid::TileOf(const IInspectable& element) const {
  for (const auto& [id, tile] : tiles_)
    if (tile.button->control == element)
      return id;
  return {};
}

xaml::FrameworkElement FavoritesGrid::TileElement(TabId id) const {
  const auto tile = tiles_.find(id);
  if (tile == tiles_.end())
    return nullptr;
  return tile->second.button->control;
}

xaml::FrameworkElement FavoritesGrid::IconOf(TabId id) const {
  const auto tile = tiles_.find(id);
  if (tile == tiles_.end())
    return nullptr;
  if (tile->second.image &&
      tile->second.button->control.Content() == tile->second.image)
    return tile->second.image;
  return tile->second.button->glyph;
}

Point FavoritesGrid::CellSize() const {
  const auto columns = static_cast<float>(std::max(std::size_t{1}, columns_));
  const float gap = kTokens.spacing.favorite_gap;
  const float width =
      (static_cast<float>(root_.ActualWidth()) - gap * (columns - 1)) / columns;
  return {std::max(0.0f, width), kTokens.spacing.favorite_height};
}

Point FavoritesGrid::CellOrigin(std::size_t index) const {
  const auto columns = std::max(std::size_t{1}, columns_);
  const auto cell = CellSize();
  const float gap = kTokens.spacing.favorite_gap;
  return {static_cast<float>(index % columns) * (cell.x + gap),
          static_cast<float>(index / columns) * (cell.y + gap)};
}

void FavoritesGrid::BeginDrag(TabId dragged, bool accepts_tabs) {
  if (settling_) {
    settling_ = false;
    Settle(true);
  }
  ClosePreviews();
  dragging_ = true;
  dragged_ = tiles_.contains(dragged) ? dragged : TabId{};
  accepts_tabs_ = accepts_tabs;
  gap_.reset();
  motion_ = context_.motion.Enabled(context_.brushes.high_contrast());
  // Zen hides a dragged Essential; its image follows the pointer. Its icon
  // keeps its own visual for that image.
  if (dragged_)
    VisualOf(tiles_.at(dragged_)).Opacity(0);
  LayoutRows();
}

// Tiles move and hide on their composition visuals, never through the
// Translation facade: XAML refuses a handoff visual for an element that uses
// facades (and the reverse) with E_ACCESSDENIED, and the glass rim already
// takes each tile's visual.
const winrt::Microsoft::UI::Composition::Visual& FavoritesGrid::VisualOf(
    Tile& tile) {
  if (!tile.visual) {
    const auto& control = tile.button->control;
    hosting::ElementCompositionPreview::SetIsTranslationEnabled(control, true);
    tile.visual = hosting::ElementCompositionPreview::GetElementVisual(control);
  }
  return tile.visual;
}

void FavoritesGrid::Shift(Tile& tile, float2 offset, bool animate) {
  if (tile.shift == offset)
    return;
  tile.shift = offset;
  const auto& visual = VisualOf(tile);
  const winrt::Windows::Foundation::Numerics::float3 to{offset.x, offset.y, 0};
  if (!animate) {
    visual.StopAnimation(L"Translation");
    visual.Properties().InsertVector3(L"Translation", to);
    return;
  }
  if (!shift_) {
    const auto compositor = visual.Compositor();
    shift_ = compositor.CreateVector3KeyFrameAnimation();
    shift_.InsertExpressionKeyFrame(0, L"this.StartingValue");
    shift_.InsertExpressionKeyFrame(
        1, L"to", Bezier(compositor, kTokens.tab_drag.shift_easing));
    shift_.Duration(Ms(kTokens.tab_drag.shift_ms));
  }
  // Starting an animation copies it, so one serves every tile.
  shift_.SetVector3Parameter(L"to", to);
  visual.StartAnimation(L"Translation", shift_);
}

std::optional<std::size_t> FavoritesGrid::SlotAt(Point point) const {
  if (!dragging_ || root_.Visibility() != xaml::Visibility::Visible)
    return std::nullopt;
  const auto width = static_cast<float>(root_.ActualWidth());
  const auto height = static_cast<float>(root_.ActualHeight());
  if (point.x < 0 || point.y < 0 || point.x >= width || point.y >= height)
    return std::nullopt;
  const auto cell = CellSize();
  const float gap = kTokens.spacing.favorite_gap;
  const auto columns = std::max(std::size_t{1}, columns_);
  const auto column = std::min(
      columns - 1,
      static_cast<std::size_t>(std::max(0.0f, point.x) / (cell.x + gap)));
  const auto row =
      static_cast<std::size_t>(std::max(0.0f, point.y) / (cell.y + gap));
  // A moved Essential lands among the others; an added tab among all of them.
  const auto last = dragged_ ? order_.size() - 1 : order_.size();
  return std::min(row * columns + column, last);
}

std::pair<TabId, TabId> FavoritesGrid::Neighbours(std::size_t slot) const {
  TabId previous, next;
  std::size_t rank = 0;
  for (const auto id : order_) {
    if (id == dragged_)
      continue;
    if (rank + 1 == slot)
      previous = id;
    if (rank == slot) {
      next = id;
      break;
    }
    ++rank;
  }
  return {previous, next};
}

void FavoritesGrid::ShowGap(std::optional<std::size_t> slot) {
  if (!dragging_ || gap_ == slot)
    return;
  gap_ = slot;
  LayoutRows();
  const auto dragged = tiles_.find(dragged_);
  const auto from =
      dragged == tiles_.end() ? order_.size() : dragged->second.index;
  for (auto& [id, tile] : tiles_) {
    if (id == dragged_)
      continue;
    auto destination = tile.index;
    if (slot) {
      const auto rank = tile.index > from ? tile.index - 1 : tile.index;
      destination = rank >= *slot ? rank + 1 : rank;
    }
    const auto at = CellOrigin(tile.index);
    const auto to = CellOrigin(destination);
    Shift(tile, {to.x - at.x, to.y - at.y}, motion_);
  }
}

void FavoritesGrid::EndDrag(bool dropped) {
  if (!dragging_)
    return;
  dragging_ = false;
  accepts_tabs_ = false;
  if (dropped && gap_) {
    settling_ = true;
    return;
  }
  Settle(false);
}

void FavoritesGrid::Settle(bool snap) {
  gap_.reset();
  for (auto& [id, tile] : tiles_)
    Shift(tile, {}, motion_ && !snap);
  if (const auto dragged = tiles_.find(dragged_); dragged != tiles_.end())
    VisualOf(dragged->second).Opacity(1);
  dragged_ = {};
  LayoutRows();
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

void FavoritesGrid::ResetPointerState() {
  ClosePreviews();
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
