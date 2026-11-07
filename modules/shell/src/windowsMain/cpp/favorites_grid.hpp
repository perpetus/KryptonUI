#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tab_hover_card.hpp"
#include "view_context.hpp"

namespace kr::win {

class FavoritesGrid {
 public:
  explicit FavoritesGrid(ViewContext& context);
  void Render(const ShellViewModel& vm);
  void SetText(UiTextView text);
  void ClosePreviews();
  [[nodiscard]] bool IsPreviewOpen() const;
  void AppendAnchors(std::vector<Anchor>& anchors, bool visible) const;
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

 private:
  struct Tile {
    std::unique_ptr<NativeButton> button;
    std::unique_ptr<TabHoverCard> preview;
    controls::Image image{nullptr};
    std::string title, url;
    std::size_t index = 0;
    std::uint64_t seen = 0;
  };
  ViewContext& context_;
  controls::Grid root_;
  std::unordered_map<TabId, Tile> tiles_;
  std::size_t columns_ = 0;
  std::uint64_t generation_ = 0;
};

}  // namespace kr::win
