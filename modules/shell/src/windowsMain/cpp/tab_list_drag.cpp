#include <windows.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

#include "krypton/ui/platform/platform.hpp"
#include "tab_list.hpp"

// Sidebar drags follow Zen's ZenDragAndDrop.js and ZenPinnedTabManager.mjs:
// rows show a drop line between them, folders take drops over their middle,
// Essentials open a gap, and the section a tab lands in pins, unpins, adds or
// removes it as an Essential (commonMain's TabDropFlags). The list captures
// the pointer itself. It requests no focus, opens no popup and adds no timer;
// releasing the button or losing capture ends the drag through the shell's
// usual focus hand-back.

namespace kr::win {
namespace {
constexpr const auto& kDrag = kTokens.tab_drag;
using winrt::Microsoft::UI::Input::PointerDeviceType;
using winrt::Microsoft::UI::Input::PointerUpdateKind;

[[nodiscard]] Point ToPoint(winrt::Windows::Foundation::Point point) noexcept {
  return {point.X, point.Y};
}

[[nodiscard]] TabId TabOf(const IInspectable& item) {
  const auto value = item.try_as<foundation::IPropertyValue>();
  return value && value.Type() == foundation::PropertyType::UInt64
             ? TabId{value.GetUInt64()}
             : TabId{};
}

[[nodiscard]] FolderId FolderOf(const IInspectable& item) {
  const auto value = item.try_as<foundation::IPropertyValue>();
  return value && value.Type() == foundation::PropertyType::Int64
             ? FolderId{std::bit_cast<std::uint64_t>(value.GetInt64())}
             : FolderId{};
}

[[nodiscard]] Color ColorOf(const media::SolidColorBrush& brush) {
  const auto color = brush.Color();
  return Color::FromArgb(color.A, color.R, color.G, color.B);
}

[[nodiscard]] Point OriginIn(const xaml::UIElement& element,
                             const xaml::UIElement& target) {
  return ToPoint(element.TransformToVisual(target).TransformPoint({0, 0}));
}
}  // namespace

void TabList::InstallDrag() {
  using Handler = void (TabList::*)(const xaml::Input::PointerRoutedEventArgs&);
  const auto guarded = [this](Handler handler) {
    return xaml::Input::PointerEventHandler(
        [this, handler](const IInspectable&,
                        const xaml::Input::PointerRoutedEventArgs& args) {
          try {
            (this->*handler)(args);
          } catch (...) {
            platform::WriteDefaultLog(LogLevel::kError, "Tab drag failed");
            CancelDrag();
          }
        });
  };
  // Rows and Essentials handle their own presses (activation, close, menus).
  // The list observes those events too, without changing them, until the
  // pointer passes the system drag threshold.
  drag_pressed_ = guarded(&TabList::OnDragPressed);
  drag_moved_ = guarded(&TabList::OnDragMoved);
  drag_released_ = guarded(&TabList::OnDragReleased);
  drag_lost_ = xaml::Input::PointerEventHandler(
      [this](const IInspectable&,
             const xaml::Input::PointerRoutedEventArgs& args) {
        // A pressed row's own capture ends when the list takes the pointer.
        // Only the list's loss ends a drag.
        try {
          if (drag_.phase == DragPhase::kDragging &&
              args.Pointer().PointerId() == drag_.pointer &&
              args.OriginalSource() == tabs_)
            CancelDrag();
        } catch (...) {
          CancelDrag();
        }
      });
  tabs_.AddHandler(xaml::UIElement::PointerPressedEvent(),
                   winrt::box_value(drag_pressed_), true);
  tabs_.AddHandler(xaml::UIElement::PointerMovedEvent(),
                   winrt::box_value(drag_moved_), true);
  tabs_.AddHandler(xaml::UIElement::PointerReleasedEvent(),
                   winrt::box_value(drag_released_), true);
  tabs_.AddHandler(xaml::UIElement::PointerCaptureLostEvent(),
                   winrt::box_value(drag_lost_), true);
  tabs_.AddHandler(xaml::UIElement::PointerCanceledEvent(),
                   winrt::box_value(drag_lost_), true);
}

void TabList::RemoveDrag() noexcept {
  if (!drag_pressed_)
    return;
  try {
    tabs_.RemoveHandler(xaml::UIElement::PointerPressedEvent(),
                        winrt::box_value(drag_pressed_));
    tabs_.RemoveHandler(xaml::UIElement::PointerMovedEvent(),
                        winrt::box_value(drag_moved_));
    tabs_.RemoveHandler(xaml::UIElement::PointerReleasedEvent(),
                        winrt::box_value(drag_released_));
    tabs_.RemoveHandler(xaml::UIElement::PointerCaptureLostEvent(),
                        winrt::box_value(drag_lost_));
    tabs_.RemoveHandler(xaml::UIElement::PointerCanceledEvent(),
                        winrt::box_value(drag_lost_));
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Tab drag handlers could not detach");
  }
}

void TabList::OnDragPressed(const xaml::Input::PointerRoutedEventArgs& args) {
  if (drag_.phase == DragPhase::kDragging)
    return;
  drag_ = {};
  const auto pointer = args.Pointer();
  const auto device = pointer.PointerDeviceType();
  if (device != PointerDeviceType::Mouse && device != PointerDeviceType::Pen &&
      device != PointerDeviceType::Touchpad)
    return;
  const auto point = args.GetCurrentPoint(tabs_);
  const auto buttons = point.Properties();
  if (buttons.PointerUpdateKind() != PointerUpdateKind::LeftButtonPressed ||
      buttons.IsRightButtonPressed() || buttons.IsMiddleButtonPressed())
    return;
  // An Essential, or a row's own surface. Buttons inside rows (close, mute,
  // Clear, New tab) keep their clicks.
  for (auto element = args.OriginalSource().try_as<xaml::DependencyObject>();
       element && element != tabs_;
       element = media::VisualTreeHelper::GetParent(element)) {
    if (const auto essential = favorites_.TileOf(element)) {
      if (horizontal_)
        return;
      drag_.source = DragSource::kEssential;
      drag_.tab = essential;
      drag_.element = favorites_.TileElement(essential);
      break;
    }
    if (element.try_as<controls::Primitives::ButtonBase>())
      return;
    if (const auto container = element.try_as<controls::ListViewItem>()) {
      const auto found = rows_.find(winrt::get_abi(container));
      if (found == rows_.end())
        return;
      auto& row = *found->second;
      if (row.id) {
        drag_.source = DragSource::kTab;
        drag_.tab = row.id;
      } else if (row.folder && !horizontal_) {
        drag_.source = DragSource::kFolder;
        drag_.folder = row.folder;
      } else {
        return;
      }
      drag_.row = &row;
      drag_.element = row.background;
      break;
    }
  }
  if (!drag_.element) {
    drag_ = {};
    return;
  }
  drag_.pointer = pointer.PointerId();
  drag_.origin = ToPoint(point.Position());
  drag_.phase = DragPhase::kPressed;
}

void TabList::OnDragMoved(const xaml::Input::PointerRoutedEventArgs& args) {
  if (drag_.phase == DragPhase::kIdle ||
      args.Pointer().PointerId() != drag_.pointer)
    return;
  const auto point = args.GetCurrentPoint(tabs_);
  if (!point.Properties().IsLeftButtonPressed()) {
    // The release went elsewhere.
    CancelDrag();
    return;
  }
  if (drag_.phase == DragPhase::kPressed) {
    const auto root = tabs_.XamlRoot();
    const auto scale = root ? root.RasterizationScale() : 1.0;
    const auto position = ToPoint(point.Position());
    if (std::abs(position.x - drag_.origin.x) * scale <
            GetSystemMetrics(SM_CXDRAG) &&
        std::abs(position.y - drag_.origin.y) * scale <
            GetSystemMetrics(SM_CYDRAG))
      return;
    if (!BeginDrag(args)) {
      drag_ = {};
      return;
    }
  }
  UpdateDrag(args);
  args.Handled(true);
}

void TabList::OnDragReleased(const xaml::Input::PointerRoutedEventArgs& args) {
  if (drag_.phase == DragPhase::kIdle ||
      args.Pointer().PointerId() != drag_.pointer)
    return;
  if (drag_.phase == DragPhase::kPressed) {
    // An ordinary click; the row or Essential handles it.
    drag_ = {};
    return;
  }
  args.Handled(true);
  const auto drag = drag_;
  const auto& target = drag.target;
  // Read the drop from the state shown, then end the drag before the host
  // answers.
  std::pair<TabId, TabId> neighbours;
  auto section = TabDropSection::kKeep;
  std::optional<FolderCommand> folder_command;
  if (target.kind == DropTarget::Kind::kEssentials) {
    neighbours = favorites_.Neighbours(target.slot);
    section = TabDropSection::kEssentials;
  } else if (target.kind == DropTarget::Kind::kList) {
    if (drag.source == DragSource::kFolder) {
      if (const auto folder = context_.delegate.OnFolderRequested(drag.folder))
        folder_command = FolderCommand{
            .kind = FolderCommandKind::kMove,
            .folder = drag.folder,
            .parent = folder->parent,
            .index = FolderOrderAt(target.position, folder->parent)};
    } else {
      neighbours = NeighboursAt(target.position);
      section = SectionAt(target.position);
    }
  } else if (target.kind == DropTarget::Kind::kFolder) {
    folder_command = drag.source == DragSource::kFolder
                         ? FolderCommand{.kind = FolderCommandKind::kMove,
                                         .folder = drag.folder,
                                         .parent = target.folder}
                         : FolderCommand{.kind = FolderCommandKind::kMoveTab,
                                         .folder = target.folder,
                                         .tab = drag.tab};
  }
  EndDrag(target.kind != DropTarget::Kind::kNone);
  if (folder_command)
    (void)context_.delegate.OnFolderCommand(*folder_command);
  else if (target.kind == DropTarget::Kind::kEssentials ||
           target.kind == DropTarget::Kind::kList)
    context_.delegate.OnTabDropped(drag.tab, neighbours.first,
                                   neighbours.second, section);
}

bool TabList::BeginDrag(const xaml::Input::PointerRoutedEventArgs& args) {
  // A render can re-bind the pressed row to another item before the drag.
  if (drag_.source == DragSource::kTab &&
      (!drag_.row || drag_.row->id != drag_.tab))
    return false;
  if (drag_.source == DragSource::kFolder &&
      (!drag_.row || drag_.row->folder != drag_.folder))
    return false;
  if (drag_.source == DragSource::kEssential &&
      !favorites_.TileElement(drag_.tab))
    return false;
  const auto pointer = args.Pointer();
  // The pressed row or Essential holds the pointer for its click. Taking it
  // ends that press without a click.
  if (drag_.row)
    drag_.row->container.ReleasePointerCapture(pointer);
  else
    drag_.element.ReleasePointerCapture(pointer);
  if (!tabs_.CapturePointer(pointer))
    return false;
  drag_.phase = DragPhase::kDragging;
  drag_slots_dirty_ = true;
  WatchScroll();
  // Hover cards close after this input event rather than inside its routing:
  // closing native popups can restore focus.
  tabs_.DispatcherQueue().TryEnqueue(
      [this, weak = std::weak_ptr<int>(lifetime_)] {
        if (!weak.expired())
          DismissHover();
      });
  if (drag_.source != DragSource::kFolder)
    favorites_.BeginDrag(
        drag_.source == DragSource::kEssential ? drag_.tab : TabId{},
        drag_.source == DragSource::kTab && !horizontal_);
  if (!drag_visuals_)
    drag_visuals_ = std::make_unique<TabDragVisuals>(tabs_);
  TabDragVisuals::Row row;
  TabDragVisuals::Tile tile{.icon_size = kTokens.spacing.icon_size,
                            .fill = ColorOf(context_.brushes.favorite),
                            .radius = kTokens.radius.favorite};
  Point tile_grab{-kDrag.tile_cursor_offset, -kDrag.tile_cursor_offset};
  const auto at = OriginIn(drag_.element, tabs_);
  const Point grab{drag_.origin.x - at.x, drag_.origin.y - at.y};
  if (drag_.source == DragSource::kEssential) {
    tile.icon = favorites_.IconOf(drag_.tab);
    tile.width = static_cast<float>(drag_.element.ActualWidth());
    tile.height = static_cast<float>(drag_.element.ActualHeight());
    tile_grab = grab;
  } else {
    auto& source = *drag_.row;
    row = {.element = source.background,
           .width = static_cast<float>(source.background.ActualWidth()),
           .height = static_cast<float>(source.background.ActualHeight()),
           .grab = grab,
           .fill = ColorOf(context_.brushes.active),
           .radius = kTokens.radius.tab};
    if (drag_.source == DragSource::kTab)
      tile.icon = source.image.Source() ? xaml::UIElement(source.image)
                                        : xaml::UIElement(source.glyph);
  }
  drag_visuals_->Begin(row, tile, tile_grab,
                       ToPoint(args.GetCurrentPoint(tabs_).Position()));
  return true;
}

void TabList::UpdateDrag(const xaml::Input::PointerRoutedEventArgs& args) {
  const auto pointer = ToPoint(args.GetCurrentPoint(tabs_).Position());
  drag_visuals_->Move(pointer);
  if (drag_.row &&
      (drag_.source == DragSource::kTab ? drag_.row->id != drag_.tab
                                        : drag_.row->folder != drag_.folder)) {
    // Scrolling recycled the dragged row for another item.
    drag_.row = nullptr;
    drag_visuals_->DetachRow();
  }
  ScrollForDrag(pointer);
  const bool refresh = drag_slots_dirty_;
  DropTarget target;
  if (pointer.x >= 0 && pointer.y >= 0 && pointer.x < tabs_.ActualWidth() &&
      pointer.y < tabs_.ActualHeight()) {
    if (favorites_.Dragging())
      if (const auto slot = favorites_.SlotAt(
              ToPoint(args.GetCurrentPoint(favorites_.Root()).Position())))
        target = {.kind = DropTarget::Kind::kEssentials, .slot = *slot};
    if (target.kind == DropTarget::Kind::kNone)
      if (const auto panel = tabs_.ItemsPanelRoot())
        target = ListTarget(ToPoint(args.GetCurrentPoint(panel).Position()));
  }
  ShowDropTarget(target, refresh);
}

void TabList::WatchScroll() {
  if (tab_scroll_)
    return;
  tab_scroll_ = FindScrollViewer(tabs_);
  if (tab_scroll_)
    tab_scrolled_ =
        tab_scroll_.ViewChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
          DismissHover();
          anchors_dirty_ = true;
          drag_slots_dirty_ = true;
        });
}

void TabList::ScrollForDrag(Point pointer) {
  // The horizontal strip fits its tabs and does not scroll.
  if (horizontal_ || !tab_scroll_)
    return;
  const float edge = kTokens.spacing.tab_height;
  const auto height = static_cast<float>(tabs_.ActualHeight());
  float delta = 0;
  if (pointer.y < edge)
    delta = -kDrag.edge_scroll;
  else if (pointer.y > height - edge)
    delta = kDrag.edge_scroll;
  if (delta == 0)
    return;
  const auto offset = std::clamp(tab_scroll_.VerticalOffset() + delta, 0.0,
                                 tab_scroll_.ScrollableHeight());
  if (offset != tab_scroll_.VerticalOffset())
    (void)tab_scroll_.ChangeView(
        nullptr, foundation::IReference<double>(offset), nullptr, true);
}

void TabList::MeasureDragSlots() {
  drag_slots_.clear();
  drag_slots_dirty_ = false;
  const auto panel = tabs_.ItemsPanelRoot();
  if (!panel)
    return;
  for (const auto& [key, row] : rows_) {
    const auto& container = row->container;
    const auto index = tabs_.IndexFromContainer(container);
    if (index < 0 || static_cast<std::size_t>(index) >= displayed_.size() ||
        container.Visibility() != xaml::Visibility::Visible)
      continue;
    try {
      const auto origin = OriginIn(container, panel);
      const Rect bounds{origin.x, origin.y,
                        static_cast<float>(container.ActualWidth()),
                        static_cast<float>(container.ActualHeight())};
      drag_slots_.push_back(
          {.index = static_cast<std::size_t>(index),
           .start = horizontal_ ? bounds.x : bounds.y,
           .end = horizontal_ ? bounds.right() : bounds.bottom(),
           .bounds = bounds});
    } catch (const winrt::hresult_error&) {
      // A container leaving the tree mid-drag has no position to offer.
    }
  }
  std::ranges::sort(drag_slots_, {}, &DragSlot::index);
  // Adjacent rows share the gap between them, so every point between two
  // rows belongs to one of them.
  for (std::size_t i = 1; i < drag_slots_.size(); ++i) {
    auto& previous = drag_slots_[i - 1];
    auto& current = drag_slots_[i];
    if (current.index != previous.index + 1)
      continue;
    const float middle = (previous.end + current.start) / 2;
    previous.end = middle;
    current.start = middle;
  }
}

TabList::DropTarget TabList::ListTarget(Point point) {
  if (drag_slots_dirty_)
    MeasureDragSlots();
  if (drag_slots_.empty())
    return {};
  const float at = horizontal_ ? point.x : point.y;
  const DragSlot* hit = nullptr;
  for (const auto& slot : drag_slots_)
    if (at < slot.end) {
      hit = &slot;
      break;
    }
  float fraction = 1;
  if (!hit)
    hit = &drag_slots_.back();
  else
    fraction = std::clamp(
        (at - hit->start) / std::max(1.0f, hit->end - hit->start), 0.0f, 1.0f);
  const auto before = hit->index;
  const auto after = hit->index + 1;
  auto position = fraction < kDrag.move_threshold ? before : after;
  if (const auto folder = FolderOf(displayed_[hit->index])) {
    // A folder takes the drop except near its edges. Below an open folder
    // are its own rows, so only a closed one has an edge after it.
    const auto value = context_.delegate.OnFolderRequested(folder);
    const bool collapsed = value && value->collapsed;
    if (fraction < kDrag.folder_threshold)
      position = before;
    else if (collapsed && fraction > 1 - kDrag.folder_threshold)
      position = after;
    else if (AcceptsFolderDrop(folder))
      return {.kind = DropTarget::Kind::kFolder, .folder = folder};
  }
  if (drag_.source == DragSource::kFolder) {
    // Folders stay above the Pinned header, among the other folders.
    const auto pinned = DisplayedIndex(pinned_header_);
    const auto end = pinned ? pinned : DisplayedIndex(today_header_);
    if (horizontal_ || !end || position > *end)
      return {};
  }
  return {.kind = DropTarget::Kind::kList, .position = position};
}

bool TabList::AcceptsFolderDrop(FolderId target) const {
  if (drag_.source != DragSource::kFolder)
    return true;
  // A folder cannot enter itself or one of its subfolders.
  auto id = target;
  for (std::size_t depth = 0; id && depth <= folder_records_.size(); ++depth) {
    if (id == drag_.folder)
      return false;
    const auto folder = context_.delegate.OnFolderRequested(id);
    if (!folder)
      break;
    id = folder->parent;
  }
  return true;
}

void TabList::ShowDropTarget(const DropTarget& target, bool refresh) {
  if (target == drag_.target && !refresh)
    return;
  drag_.target = target;
  favorites_.ShowGap(target.kind == DropTarget::Kind::kEssentials
                         ? std::optional<std::size_t>(target.slot)
                         : std::nullopt);
  HighlightFolder(target.kind == DropTarget::Kind::kFolder ? target.folder
                                                           : FolderId{});
  ShowDragTile(target.kind == DropTarget::Kind::kEssentials ||
               drag_.source == DragSource::kEssential);
  const auto panel = tabs_.ItemsPanelRoot();
  if (target.kind != DropTarget::Kind::kList || !panel) {
    drag_visuals_->HideIndicator();
    return;
  }
  // The line sits in the gap before displayed_[position].
  const DragSlot* previous = nullptr;
  const DragSlot* next = nullptr;
  for (const auto& slot : drag_slots_) {
    if (slot.index + 1 == target.position)
      previous = &slot;
    if (slot.index == target.position)
      next = &slot;
  }
  if (!previous && !next) {
    drag_visuals_->HideIndicator();
    return;
  }
  const float pad =
      horizontal_ ? kTokens.spacing.tab_inset : kTokens.spacing.tab_gap;
  const float at = previous && next ? previous->end
                   : previous       ? previous->end + pad
                                    : next->start - pad;
  const auto offset = OriginIn(panel, tabs_);
  const auto& reference = previous ? *previous : *next;
  drag_visuals_->ShowIndicator(reference.bounds.Offset(offset.x, offset.y),
                               at + (horizontal_ ? offset.x : offset.y),
                               horizontal_, IndicatorColor());
}

Color TabList::IndicatorColor() const {
  if (context_.brushes.high_contrast())
    return ColorOf(context_.brushes.text);
  // Zen mixes its primary colour half and half with near-black or near-white.
  return Mix(
      folder_theme_.accent,
      Color{folder_theme_.dark ? kDrag.indicator_dark : kDrag.indicator_light},
      kDrag.indicator_mix);
}

void TabList::HighlightFolder(FolderId folder) {
  if (drag_.highlighted == folder)
    return;
  const auto paint = [this](FolderId id, bool highlighted) {
    const auto record = folder_records_.find(id);
    if (record == folder_records_.end() || !record->second.row ||
        record->second.row->folder != id)
      return;
    // Zen fills a folder that takes the drop with its primary colour.
    record->second.row->background.Background(
        !highlighted ? context_.brushes.transparent
        : context_.brushes.high_contrast()
            ? context_.brushes.active
            : context_.brushes.For(folder_theme_.accent));
  };
  paint(drag_.highlighted, false);
  drag_.highlighted = folder;
  paint(folder, true);
}

void TabList::ShowDragTile(bool tile) {
  auto size = favorites_.CellSize();
  if (size.x <= 0)
    size.x = size.y;
  drag_visuals_->ShowTile(tile, size.x, size.y);
}

void TabList::EndDrag(bool dropped) {
  const bool dragging = drag_.phase == DragPhase::kDragging;
  if (dragging) {
    HighlightFolder({});
    if (drag_visuals_)
      drag_visuals_->End();
    favorites_.EndDrag(dropped &&
                       drag_.target.kind == DropTarget::Kind::kEssentials);
  }
  drag_ = {};
  drag_slots_.clear();
  drag_slots_dirty_ = true;
  if (dragging)
    tabs_.ReleasePointerCaptures();
}

void TabList::CancelDrag() noexcept {
  if (drag_.phase == DragPhase::kIdle)
    return;
  const bool dragging = drag_.phase == DragPhase::kDragging;
  try {
    EndDrag(false);
    return;
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError, "Tab drag did not end cleanly");
  }
  // EndDrag stopped partway. Leave no drag state, image or capture behind.
  drag_ = {};
  drag_slots_.clear();
  drag_slots_dirty_ = true;
  if (!dragging)
    return;
  try {
    if (drag_visuals_)
      drag_visuals_->End();
  } catch (...) {
  }
  try {
    favorites_.EndDrag(false);
  } catch (...) {
  }
  try {
    tabs_.ReleasePointerCaptures();
  } catch (...) {
  }
}

TabDropSection TabList::SectionAt(std::size_t position) const {
  if (horizontal_)
    return TabDropSection::kKeep;
  const auto today = DisplayedIndex(today_header_);
  return !today || position <= *today ? TabDropSection::kPinned
                                      : TabDropSection::kRegular;
}

std::pair<TabId, TabId> TabList::NeighboursAt(std::size_t position) const {
  // Neighbours come from the drop's own part of the list: folders, pinned
  // tabs below the Pinned header, or unpinned tabs below Today.
  std::size_t begin = 0;
  std::size_t end = displayed_.size();
  if (!horizontal_) {
    const auto today = DisplayedIndex(today_header_);
    const auto pinned = DisplayedIndex(pinned_header_);
    if (today && position > *today) {
      begin = *today + 1;
    } else {
      if (today)
        end = *today;
      if (pinned && position > *pinned)
        begin = *pinned + 1;
      else if (pinned)
        end = *pinned;
    }
  }
  TabId previous, next;
  for (auto i = std::min(position, end); !previous && i > begin;)
    if (const auto tab = TabOf(displayed_[--i]); tab && tab != drag_.tab)
      previous = tab;
  for (auto i = std::max(position, begin); !next && i < end; ++i)
    if (const auto tab = TabOf(displayed_[i]); tab && tab != drag_.tab)
      next = tab;
  return {previous, next};
}

std::size_t TabList::FolderOrderAt(std::size_t position,
                                   FolderId parent) const {
  std::size_t order = 0;
  for (std::size_t i = 0; i < std::min(position, displayed_.size()); ++i) {
    const auto folder = FolderOf(displayed_[i]);
    if (!folder || folder == drag_.folder)
      continue;
    if (const auto value = context_.delegate.OnFolderRequested(folder);
        value && value->parent == parent)
      ++order;
  }
  return order;
}

std::optional<std::size_t> TabList::DisplayedIndex(
    const IInspectable& item) const {
  for (std::size_t i = 0; i < displayed_.size(); ++i)
    if (SameItem(displayed_[i], item))
      return i;
  return std::nullopt;
}

}  // namespace kr::win
