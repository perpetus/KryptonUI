#pragma once

#include "krypton/ui/presentation/view_model.hpp"

namespace kr {
struct SplitLayoutChoice {
  SplitLayout layout;
  UiText label;
  std::size_t preview_members;
};
inline constexpr std::array kSplitLayoutChoices{
    SplitLayoutChoice{SplitLayout::kColumns, UiText::kSplitColumns, 2},
    SplitLayoutChoice{SplitLayout::kRows, UiText::kSplitRows, 2},
    SplitLayoutChoice{SplitLayout::kGrid, UiText::kSplitGrid, 4},
    SplitLayoutChoice{SplitLayout::kWideLeft, UiText::kSplitWideLeft, 2},
    SplitLayoutChoice{SplitLayout::kWideRight, UiText::kSplitWideRight, 2},
    SplitLayoutChoice{SplitLayout::kWideCenter, UiText::kSplitWideCenter, 3},
    SplitLayoutChoice{SplitLayout::kMainLeft, UiText::kSplitMainLeft, 3},
    SplitLayoutChoice{SplitLayout::kMainRight, UiText::kSplitMainRight, 3},
    SplitLayoutChoice{SplitLayout::kMainTop, UiText::kSplitMainTop, 3},
    SplitLayoutChoice{SplitLayout::kMainBottom, UiText::kSplitMainBottom, 3}};
// The chooser draws these same bounded shared topologies as the live pages.
[[nodiscard]] MultiPageGeometry PresentSplitLayoutPreview(
    const SplitLayoutChoice& choice,
    Rect bounds,
    float gap,
    bool right_to_left) noexcept;

struct SplitRowVm {
  SplitGroupId id;
  TabId representative;
  std::uint64_t revision = 0;
  SplitLayout layout = SplitLayout::kColumns;
  std::size_t count = 0;
  std::array<TabItemVm, kMaxSplitMembers> members{};
};
[[nodiscard]] TabId SplitRepresentative(const SplitGroup& group) noexcept;
[[nodiscard]] TabItemVm PresentMultiPageTab(const Store& store, const Tab& tab);
[[nodiscard]] std::optional<SplitRowVm> PresentSplitRow(const Store& store,
                                                        TabId tab);
// The focused split page's outline: the theme accent, darkened in light
// schemes. Transparent when there is no accent.
[[nodiscard]] Color SplitOutlineColor(const ThemeVm& theme) noexcept;
struct GlanceColors {
  Color button;
  Color button_hover;
  Color button_icon;
  Color shadow;
  Color button_shadow;
  friend bool operator==(const GlanceColors&, const GlanceColors&) = default;
};
[[nodiscard]] GlanceColors GlanceButtonColors(const ThemeVm& theme) noexcept;
}  // namespace kr
