#pragma once

// Small factories for the controls the chrome is built from. The UI is built
// in code, so these play the role of XAML styles: one place per kind of
// control, every size and colour from the design tokens.

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.Text.h>

#include <memory>
#include <string_view>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/design/icons.generated.hpp"
#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/platform/shell_view.hpp"

namespace kr::win {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;

inline constexpr auto& kTokens = design::kTokens;

[[nodiscard]] constexpr xaml::CornerRadius UniformCorners(
    double radius) noexcept {
  return {radius, radius, radius, radius};
}

[[nodiscard]] constexpr xaml::Thickness UniformThickness(
    double value) noexcept {
  return {value, value, value, value};
}

static_assert(UniformCorners(1).TopLeft == 1 &&
              UniformCorners(1).TopRight == 1 &&
              UniformCorners(1).BottomRight == 1 &&
              UniformCorners(1).BottomLeft == 1);
static_assert(UniformThickness(1).Left == 1 && UniformThickness(1).Top == 1 &&
              UniformThickness(1).Right == 1 &&
              UniformThickness(1).Bottom == 1);

// Segoe Fluent Icons code points.
namespace glyphs {
inline constexpr std::wstring_view kSidebar = L"";  // ClosePaneMirrored
inline constexpr std::wstring_view kAdd = L"";
inline constexpr std::wstring_view kBack = L"";
inline constexpr std::wstring_view kForward = L"";
inline constexpr std::wstring_view kChevronLeft = L"\uE76B";
inline constexpr std::wstring_view kChevronRight = L"\uE76C";
inline constexpr std::wstring_view kReload = L"";
inline constexpr std::wstring_view kStop = L"";
inline constexpr std::wstring_view kSearch = L"";
inline constexpr std::wstring_view kLock = L"";
inline constexpr std::wstring_view kInfo = L"";
inline constexpr std::wstring_view kMore = L"";
inline constexpr std::wstring_view kMinimize = L"";
inline constexpr std::wstring_view kMaximize = L"";
inline constexpr std::wstring_view kRestore = L"";
inline constexpr std::wstring_view kClose = L"";
inline constexpr std::wstring_view kGlobe = L"";
inline constexpr std::wstring_view kAction = L"";
}  // namespace glyphs

// Font weights for the workspace name, the address text and action badges.
inline constexpr winrt::Windows::UI::Text::FontWeight kBold{700};
inline constexpr winrt::Windows::UI::Text::FontWeight kSemiBold{600};
inline constexpr winrt::Windows::UI::Text::FontWeight kMedium{500};

class Brushes;
class IconView;
// Lets NativeButton own an IconView where IconView is incomplete.
struct IconViewDeleter {
  void operator()(IconView* view) const noexcept;
};

[[nodiscard]] winrt::Windows::UI::Color NativeColor(Color color);
void Accessible(const xaml::DependencyObject& element, std::wstring_view name);
void SetToolTip(const xaml::DependencyObject& element, std::wstring_view text);
[[nodiscard]] winrt::hstring NativeText(UiText id, UiTextView text = {});

controls::ColumnDefinition AddColumn(const controls::Grid& grid,
                                     double width,
                                     xaml::GridUnitType unit);
controls::RowDefinition AddRow(const controls::Grid& grid,
                               double height,
                               xaml::GridUnitType unit);

[[nodiscard]] controls::TextBlock Label(std::wstring_view text, double size);
[[nodiscard]] controls::TextBlock Glyph(
    std::wstring_view glyph,
    double size = kTokens.spacing.icon_size);

[[nodiscard]] controls::Viewbox SourceIcon(const Brushes& brushes,
                                           const design::icons::Icon& icon);
// The same icon filled with `fill` rather than the icon colour.
[[nodiscard]] controls::Viewbox SourceIcon(const xaml::Media::Brush& fill,
                                           const design::icons::Icon& icon);

// A button together with the revokers of the handlers it owns. Heap-allocated
// so handlers can safely capture its address.
struct NativeButton {
  controls::Button control{nullptr};
  controls::TextBlock glyph{nullptr};
  controls::TextBlock label{nullptr};
  controls::Button::Click_revoker click;
  xaml::UIElement::RightTapped_revoker right_tapped;
  std::unique_ptr<IconView, IconViewDeleter> icon;  // pack icon, if any
  std::string tooltip_text;
  winrt::hstring accessible_name;
  bool text_initialized = false;
  std::uint64_t seen = 0;
};

void SetButtonText(NativeButton& button, const winrt::hstring& text);

// A toolbar button with source-derived icon, box and margin tokens.
[[nodiscard]] std::unique_ptr<NativeButton> IconButton(const Brushes& brushes,
                                                       std::wstring_view glyph,
                                                       std::wstring_view name,
                                                       Task action);

[[nodiscard]] std::unique_ptr<NativeButton> SourceButton(
    const Brushes& brushes,
    const design::icons::Icon& icon,
    std::wstring_view name,
    Task action);

// A Windows caption button spanning the full toolbar height.
[[nodiscard]] std::unique_ptr<NativeButton> CaptionButton(
    const Brushes& brushes,
    std::wstring_view glyph,
    std::wstring_view name,
    Task action);

// A sidebar row the size of a tab: a leading element, then a label.
[[nodiscard]] std::unique_ptr<NativeButton> RowButton(
    const Brushes& brushes,
    const xaml::UIElement& lead,
    std::wstring_view label,
    std::wstring_view name,
    Task action);

}  // namespace kr::win
