#pragma once

#include <windows.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>

#include <array>
#include <memory>
#include <optional>

#include "island_hide.hpp"
#include "library_list.hpp"
#include "native_input.hpp"
#include "routing_form.hpp"
#include "sprite_icon.hpp"
#include "theme_backdrop.hpp"

namespace kr::win {

// Zen's library (ZenLibrary.mjs): a panel that springs out from the
// sidebar's edge over the chrome, a rail of animated section tabs beside the
// open section. It is a transient surface with its own island, created on
// first open and hidden, not destroyed, when it closes.
class LibraryPanel {
 public:
  LibraryPanel(HWND parent, ViewContext& context, xaml::UIElement chrome);
  ~LibraryPanel();
  LibraryPanel(const LibraryPanel&) = delete;
  LibraryPanel& operator=(const LibraryPanel&) = delete;

  void Render(const ShellViewModel& vm, xaml::ElementTheme theme);
  void Close() noexcept;
  void Raise() noexcept;
  [[nodiscard]] bool IsOpen() const noexcept { return open_; }
  [[nodiscard]] bool Contains(Point point) const noexcept;
  [[nodiscard]] bool OwnsInput() const noexcept { return open_ || visible_; }

 private:
  struct Tab {
    LibrarySection section = LibrarySection::kHistory;
    controls::Button button{nullptr};
    std::unique_ptr<SpriteIcon> icon;
    controls::TextBlock label{nullptr};
    controls::Button::Click_revoker click;
  };
  void Build();
  void Place(const ShellViewModel& vm);
  void Slide(bool open);
  void ResetBatch() noexcept;
  void HideSource();
  void RenderRail(const ShellViewModel& vm);
  void RenderSection(const ShellViewModel& vm);
  void BuildOptIn();
  void LayoutSkeleton();
  [[nodiscard]] Rect Anchor(const xaml::FrameworkElement& element) const;

  HWND parent_;
  // Where keyboard focus was when the panel opened; it returns there.
  HWND return_focus_ = nullptr;
  NativeInputTarget input_target_;
  IslandHide hide_;
  ViewContext& context_;
  xaml::UIElement chrome_;
  xaml::Hosting::DesktopWindowXamlSource source_{nullptr};
  controls::Grid root_{nullptr}, slider_{nullptr}, side_{nullptr},
      content_{nullptr}, body_{nullptr};
  controls::StackPanel rail_{nullptr};
  controls::Border side_shade_{nullptr};
  std::unique_ptr<ThemeBackdrop> backdrop_;
  std::array<Tab, kLibrarySectionCount> tabs_;
  std::unique_ptr<NativeButton> back_;
  controls::TextBlock heading_{nullptr}, empty_{nullptr};
  controls::Grid actions_{nullptr};
  controls::TextBox search_{nullptr};
  controls::Button create_{nullptr};
  winrt::hstring create_text_;
  bool updating_search_ = false;
  controls::TextBox::TextChanged_revoker searched_;
  controls::Button::Click_revoker created_;
  std::unique_ptr<LibraryList> rows_, tiles_;
  std::unique_ptr<RoutingForm> routing_form_;
  // Media opt-in: a pulsing skeleton behind Zen's explanation card.
  controls::Grid opt_in_{nullptr};
  controls::Canvas skeleton_{nullptr};
  winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation pulse_{nullptr};
  std::unique_ptr<SpriteIcon> opt_in_icon_;
  controls::TextBlock opt_in_title_{nullptr}, opt_in_body_{nullptr},
      opt_in_note_{nullptr};
  controls::Button opt_in_button_{nullptr};
  controls::Border opt_in_card_{nullptr};
  std::unique_ptr<IconView> opt_in_lock_;
  ContentFade fade_;
  Rect bounds_;
  float scale_ = 1;
  bool open_ = false, visible_ = false, on_right_ = false, labels_ = true;
  bool pulsing_ = false;
  std::optional<LibrarySection> shown_section_;
  std::uint64_t serial_ = 0;
  float chrome_opacity_ = 1;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  winrt::Microsoft::UI::Composition::CompositionScopedBatch batch_{nullptr};
  winrt::Microsoft::UI::Composition::SpringVector3NaturalMotionAnimation slide_{
      nullptr};
  winrt::Microsoft::UI::Composition::ScalarKeyFrameAnimation chrome_fade_{
      nullptr};
  winrt::event_token batch_done_{};
  controls::Button::Click_revoker opt_in_click_;
  xaml::UIElement::PointerEntered_revoker opt_in_entered_;
  xaml::UIElement::PointerExited_revoker opt_in_exited_;
  xaml::UIElement::KeyDown_revoker key_;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker fallback_key_;
  xaml::FrameworkElement::SizeChanged_revoker body_sized_;
};
}  // namespace kr::win
