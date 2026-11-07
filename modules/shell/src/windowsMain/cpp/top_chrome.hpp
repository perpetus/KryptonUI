#pragma once

#include "chrome_header.hpp"
#include "tab_list.hpp"

namespace kr::win {
// Native parenting and measured placement. Ordering and row extents come from
// the shared layout. One virtualized TabList is retained across both
// orientations.
class TopChrome {
 public:
  TopChrome(ViewContext& context, ChromeHeader& header, TabList& tabs);
  ~TopChrome();
  void SetLayout(const ShellLayout& layout, const controls::Grid& sidebar);
  void SetText(UiTextView text);
  [[nodiscard]] bool FocusNative(Element target);
  void AppendDragExclusions(std::vector<Rect>& controls, bool visible) const;
  void AppendAnchors(std::vector<Anchor>& anchors, bool visible) const;
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

 private:
  void PositionNewTab();
  ViewContext& context_;
  ChromeHeader& header_;
  TabList& tabs_;
  controls::Grid root_, strip_;
  media::TranslateTransform new_tab_position_;
  std::unique_ptr<NativeButton> new_tab_;
  float tabs_extent_ = 0;
  float new_tab_row_height_ = kTokens.spacing.toolbar_height;
  bool new_tab_in_strip_ = true;
  TabLayout mode_ = TabLayout::kVertical;
};
}  // namespace kr::win
