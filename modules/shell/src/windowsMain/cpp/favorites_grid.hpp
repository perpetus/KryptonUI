#pragma once

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "tab_hover_card.hpp"
#include "view_context.hpp"

namespace kr::win {

class FavoritesGrid {
 public:
  explicit FavoritesGrid(ViewContext& context);
  // Returns whether the Essentials changed order or membership.
  bool Render(const ShellViewModel& vm);
  void SetText(UiTextView text);
  void ClosePreviews();
  void ResetPointerState();
  [[nodiscard]] bool IsPreviewOpen() const;
  void AppendAnchors(std::vector<Anchor>& anchors, bool visible) const;
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

  // Sidebar drags. The tab list owns the pointer; the grid says what is under
  // it and opens a gap where the dragged item would land, as Zen's
  // #animateVerticalPinnedGridDragOver does.
  [[nodiscard]] TabId TileOf(const IInspectable& element) const;
  [[nodiscard]] xaml::FrameworkElement TileElement(TabId id) const;
  [[nodiscard]] xaml::FrameworkElement IconOf(TabId id) const;
  // `dragged` is the Essential being moved, or nothing for a tab that may be
  // added. An empty grid then shows one row to drop into.
  void BeginDrag(TabId dragged, bool accepts_tabs);
  // The final position under `point` (grid coordinates), if it is over the
  // grid.
  [[nodiscard]] std::optional<std::size_t> SlotAt(Point point) const;
  [[nodiscard]] std::pair<TabId, TabId> Neighbours(std::size_t slot) const;
  // Opens a gap at `slot`, or closes it.
  void ShowGap(std::optional<std::size_t> slot);
  [[nodiscard]] Point CellSize() const;
  // `dropped` keeps the gap until the host's next order arrives.
  void EndDrag(bool dropped);
  [[nodiscard]] bool Dragging() const noexcept { return dragging_; }

 private:
  struct Tile {
    std::unique_ptr<NativeButton> button;
    std::unique_ptr<TabHoverCard> preview;
    controls::Image image{nullptr};
    // Created on the tile's first drag. Drags move and hide tiles through it.
    winrt::Microsoft::UI::Composition::Visual visual{nullptr};
    winrt::Windows::Foundation::Numerics::float2 shift{};
    std::string title, url;
    std::size_t index = 0;
    std::uint64_t seen = 0;
  };
  void LayoutRows();
  [[nodiscard]] Point CellOrigin(std::size_t index) const;
  void Settle(bool snap);
  [[nodiscard]] const winrt::Microsoft::UI::Composition::Visual& VisualOf(
      Tile& tile);
  void Shift(Tile& tile,
             winrt::Windows::Foundation::Numerics::float2 offset,
             bool animate);
  ViewContext& context_;
  controls::Grid root_;
  std::vector<TabId> order_;
  std::unordered_map<TabId, Tile> tiles_;
  std::size_t columns_ = 0;
  std::uint64_t generation_ = 0;
  bool dragging_ = false;
  bool accepts_tabs_ = false;
  bool motion_ = false;
  winrt::Microsoft::UI::Composition::Vector3KeyFrameAnimation shift_{nullptr};
  TabId dragged_;
  std::optional<std::size_t> gap_;
  // The gap needs one more row than the Essentials fill.
  bool extra_row_ = false;
  // Tiles keep their drop positions until the host answers the drop.
  bool settling_ = false;
};

}  // namespace kr::win
