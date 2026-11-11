#pragma once

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <functional>
#include <memory>

#include "icon_view.hpp"
#include "view_context.hpp"

// Native pieces shared by the sidebar's download stack and media cards and
// the library panel, following Zen's zen-library-widget.css.
namespace kr::win {
namespace shapes = xaml::Shapes;

// Segoe Fluent glyphs where Zen draws animated sprites or toolkit icons.
namespace library_glyphs {
inline constexpr std::wstring_view kRouting = L"\uE8AB";
inline constexpr std::wstring_view kMedia = L"";      // Photo2
inline constexpr std::wstring_view kDownloads = L"";  // Download
inline constexpr std::wstring_view kBoosts = L"";     // Brush
inline constexpr std::wstring_view kSpaces = L"";     // Copy
inline constexpr std::wstring_view kHistory = L"";    // History
inline constexpr std::wstring_view kFolder = L"";     // FolderOpen
inline constexpr std::wstring_view kFile = L"";       // Document
inline constexpr std::wstring_view kPictureInPicture = L"";
}  // namespace library_glyphs

// Whether a routed event's source lies inside `ancestor`.
[[nodiscard]] bool IsWithin(const IInspectable& source,
                            const xaml::UIElement& ancestor);

[[nodiscard]] std::wstring_view LibrarySectionGlyph(
    LibrarySection section) noexcept;

// A path icon that can switch between two shapes, e.g. play and pause.
struct SwitchIcon {
  controls::Viewbox root{nullptr};
  shapes::Path first{nullptr}, second{nullptr};
  void Show(bool second_shape) const;
  void Fill(const media::Brush& brush) const;
};
[[nodiscard]] SwitchIcon MakeSwitchIcon(const design::icons::Icon& first,
                                        const design::icons::Icon* second,
                                        double size);

// A download's badge: its picture, or a ring showing its progress while it
// is pending. Laid out in a 48px slot like Zen's badge and its margins.
class DownloadBadge {
 public:
  explicit DownloadBadge(ViewContext& context);
  void Update(const DownloadVm& download,
              const LibraryPaletteVm& palette,
              const media::ImageSource& picture);
  void SetCompact(bool compact);
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }

 private:
  ViewContext& context_;
  controls::Grid root_;
  controls::Border picture_, placeholder_;
  controls::Grid pending_;
  shapes::Ellipse fill_, track_, arc_;
  std::unique_ptr<IconView> arrow_;
  media::ImageBrush picture_brush_;
  float progress_ = -1;
  bool is_pending_ = false, compact_ = false, has_picture_ = false;
};

// One entry of the hover stack or the library's downloads list.
class DownloadEntry {
 public:
  // `bounds` places menus for rows outside the chrome's islands.
  DownloadEntry(ViewContext& context,
                bool library_row,
                std::function<Rect(const xaml::FrameworkElement&)> bounds = {});
  void Update(const DownloadVm& download,
              const LibraryPaletteVm& palette,
              const media::ImageSource& picture);
  void SetCompact(bool compact);
  void SetActionSpaceReserved(bool reserved);
  void SetText(UiTextView text);
  [[nodiscard]] const controls::Grid& Root() const noexcept { return root_; }
  [[nodiscard]] DownloadId id() const noexcept { return id_; }

 private:
  void Refresh();
  ViewContext& context_;
  std::function<Rect(const xaml::FrameworkElement&)> bounds_;
  controls::Grid root_;
  DownloadBadge badge_;
  controls::StackPanel text_;
  controls::TextBlock title_, subtitle_;
  std::unique_ptr<NativeButton> action_;
  DownloadId id_;
  bool can_cancel_ = false, can_show_ = false, can_open_ = false,
       hovered_ = false;
  DownloadId pressed_, released_;
  std::uint32_t pressed_pointer_ = 0;
  bool compact_ = false;
  winrt::hstring cancel_text_, show_text_;
  xaml::UIElement::PointerEntered_revoker entered_;
  xaml::UIElement::PointerExited_revoker exited_;
  xaml::UIElement::PointerPressed_revoker pressed_event_;
  xaml::UIElement::PointerReleased_revoker released_event_;
  xaml::UIElement::PointerCanceled_revoker canceled_;
  xaml::UIElement::PointerCaptureLost_revoker capture_lost_;
  xaml::FrameworkElement::Unloaded_revoker unloaded_;
  xaml::UIElement::Tapped_revoker tapped_;
  xaml::UIElement::RightTapped_revoker context_menu_;
};
}  // namespace kr::win
