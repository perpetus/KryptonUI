#pragma once

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>

#include "chrome_header.hpp"
#include "download_stack.hpp"
#include "icon_view.hpp"
#include "media_stack.hpp"
#include "sprite_icon.hpp"
#include "workspace_gesture.hpp"

namespace kr::win {

class SidebarChrome {
 public:
  SidebarChrome(ViewContext& context,
                ChromeHeader& header,
                const xaml::UIElement& tabs);
  ~SidebarChrome();
  [[nodiscard]] xaml::FrameworkElement NewTabButton() const {
    return new_tab_->control;
  }
  void SetWorkspaceSwipeEnabled(bool enabled);
  void SetIconOnly(bool icons);
  void SetLibraryPlacement(bool horizontal, const controls::Grid& toolbar_slot);
  void Render(std::span<const SpaceVm> spaces);
  // Downloads stack, media cards and the library button's badge.
  void RenderLibrary(const ShellViewModel& vm);
  void RefreshHoverPopups();
  void DismissHoverPopups(const xaml::FrameworkElement& keep = nullptr);
  [[nodiscard]] bool HasActiveHoverPopup() const noexcept;
  // Rasterizes the library sprite for the window's scale and theme.
  void SetLibraryIcon(const ThemeVm& theme, float scale);
  void SetIncognito(bool incognito);
  void SetText(UiTextView text);
  [[nodiscard]] bool FocusNative(Element target);
  void AppendAnchors(std::vector<Anchor>& anchors, bool visible) const;
  void AppendLibraryAnchor(std::vector<Anchor>& anchors, bool visible) const;
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

 private:
  void DownloadStarted(const LibraryVm& library);
  ViewContext& context_;
  controls::Grid root_, footer_, library_cell_;
  bool horizontal_library_ = false;
  controls::Canvas arrival_host_;
  controls::Border arrival_;
  std::uint64_t download_start_serial_ = 0;
  controls::StackPanel workspace_icons_;
  controls::ScrollViewer workspace_scroll_;
  controls::TextBlock workspace_glyph_{nullptr};
  std::unique_ptr<IconView> workspace_icon_, dropdown_icon_;
  std::unique_ptr<SpriteIcon> library_sprite_;
  std::unique_ptr<MediaStack> media_;
  std::unique_ptr<DownloadStack> downloads_;
  xaml::UIElement::PointerEntered_revoker library_entered_;
  xaml::UIElement::PointerExited_revoker library_exited_;
  WorkspaceGesture gesture_;
  bool workspace_swipe_enabled_ = false;
  bool library_hovered_ = false;
  bool incognito_ = false, workspace_symbol_ = false;
  std::unique_ptr<NativeButton> workspace_, new_tab_, library_, create_;
  std::unordered_map<SpaceId, std::unique_ptr<NativeButton>> space_buttons_;
  std::uint64_t space_generation_ = 0;
  SpaceId active_space_;
  bool icons_ = false;
  winrt::hstring switch_text_ = NativeText(UiText::kSwitchWorkspace);
};

}  // namespace kr::win
