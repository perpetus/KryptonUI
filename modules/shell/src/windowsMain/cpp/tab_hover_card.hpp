#pragma once

#include <winrt/Microsoft.UI.Dispatching.h>

#include <string>

#include "krypton/ui/presentation/tab_preview.hpp"
#include "view_context.hpp"

namespace kr::win {

// The hover card of one realized tab. It opens once the pointer has rested for
// the shared hover delay, or at once for keyboard focus, and closes when the
// pointer leaves or presses. Its visual tree is created on first open; request
// ids tie replies and dismissal to that exact hover.
class TabHoverCard {
 public:
  // `focus` is the element whose keyboard focus opens the card; by default
  // the target itself.
  TabHoverCard(ViewContext& context,
               const xaml::FrameworkElement& target,
               const xaml::UIElement& focus = nullptr);
  ~TabHoverCard();
  TabHoverCard(const TabHoverCard&) = delete;
  TabHoverCard& operator=(const TabHoverCard&) = delete;

  void SetTarget(TabId tab, std::string_view title, std::string_view url);
  void SetText(UiTextView text);
  void Configure(TabPreviewMetrics metrics);
  void Render(const ShellViewModel& vm);
  void Close();
  [[nodiscard]] bool IsOpen() const noexcept { return request_id_ != 0; }

 private:
  void Build();
  void Schedule();
  void Open();
  void EndRequest();
  ViewContext& context_;
  winrt::weak_ref<xaml::FrameworkElement> target_;
  winrt::weak_ref<xaml::UIElement> focus_;
  TabId tab_;
  std::uint32_t request_id_ = 0;
  TabPreviewMetrics metrics_ = MeasureTabPreview(kTokens.tab_hover.width);
  bool text_current_ = false;
  controls::Border card_{nullptr}, viewport_{nullptr}, footer_{nullptr};
  controls::TextBlock title_{nullptr}, domain_{nullptr}, memory_{nullptr},
      status_{nullptr};
  controls::Image favicon_{nullptr}, thumbnail_{nullptr};
  xaml::FrameworkElement placeholder_{nullptr};
  winrt::hstring target_title_, target_domain_;
  std::string title_text_, domain_text_, memory_text_, status_text_;
  winrt::hstring unavailable_ = NativeText(UiText::kPreviewUnavailable);
  winrt::hstring page_preview_ = NativeText(UiText::kPagePreview);
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer_{nullptr};
  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer::Tick_revoker tick_;
  xaml::UIElement::PointerEntered_revoker entered_;
  xaml::UIElement::PointerExited_revoker exited_;
  xaml::UIElement::GotFocus_revoker focused_;
  xaml::UIElement::LostFocus_revoker blurred_;
  // Registered for handled presses too, since buttons handle their own.
  xaml::Input::PointerEventHandler pressed_{nullptr};
};

}  // namespace kr::win
