#pragma once

#include "krypton/ui/presentation/multi_page.hpp"
#include "transient_surface.hpp"
#include "view_context.hpp"

namespace kr::win {
class SplitOptions {
 public:
  SplitOptions(HWND parent, ViewContext& context);
  ~SplitOptions();
  void Render(const ShellViewModel::SplitChooserVm& model,
              UiTextView text,
              Rect client,
              float scale,
              xaml::ElementTheme theme,
              const ImageStore* images);
  void Close() noexcept;
  [[nodiscard]] bool IsOpen() const noexcept { return requested_; }

 private:
  void Build();
  void Open() noexcept;
  void Place() noexcept;
  ViewContext& context_;
  TransientSurface island_;
  controls::Primitives::Popup popup_{nullptr};
  controls::Border surface_{nullptr};
  struct Tile {
    controls::Button button{nullptr};
    std::array<controls::Border, kMaxSplitMembers> panes{nullptr, nullptr,
                                                         nullptr, nullptr};
    std::array<controls::Image, kMaxSplitMembers> icons{nullptr, nullptr,
                                                        nullptr, nullptr};
    controls::Primitives::ButtonBase::Click_revoker click;
  };
  std::array<Tile, kSplitLayoutChoices.size()> tiles_;
  struct Page {
    controls::StackPanel row{nullptr};
    controls::Image icon{nullptr};
    controls::TextBlock title{nullptr};
  };
  std::array<Page, kMaxSplitMembers> pages_;
  Rect anchor_;
  bool requested_ = false, placing_ = false, focus_pending_ = false,
       populated_ = false;
  std::size_t focused_ = 0;
  xaml::FrameworkElement::Loaded_revoker loaded_;
  xaml::FrameworkElement::Loaded_revoker content_loaded_;
  xaml::FrameworkElement::SizeChanged_revoker size_;
  controls::Primitives::Popup::Closed_revoker closed_;
};
}  // namespace kr::win
