#include "krypton/ui/presentation/multi_page.hpp"

#include <algorithm>
#include <cmath>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton/ui/presentation/theme_editor.hpp"

namespace kr {
MultiPageGeometry PresentSplitLayoutPreview(const SplitLayoutChoice& choice,
                                            Rect bounds,
                                            float gap,
                                            bool right_to_left) noexcept {
  const std::array ids{TabId{1}, TabId{2}, TabId{3}, TabId{4}};
  if (choice.preview_members < 2 || choice.preview_members > ids.size())
    return {};
  const auto group = MakeSplit(SplitGroupId{1}, SpaceId{1},
                               std::span(ids).first(choice.preview_members),
                               ids[0], choice.layout);
  if (!group)
    return {};
  return LayoutSplit(*group, bounds, gap, right_to_left)
      .value_or(MultiPageGeometry{});
}
GlanceColors GlanceButtonColors(const ThemeVm& theme) noexcept {
  const auto& t = design::kTokens.glance;
  const Color accent = theme.accent.a() ? theme.accent.WithAlpha(0xFF)
                                        : Color{theme.palette->text};
  const auto alpha = [](float value) {
    return static_cast<std::uint8_t>(std::lround(value * 255));
  };
  if (theme.high_contrast) {
    return {Color{theme.palette->overlay},
            Color{theme.palette->selection},
            Color{theme.palette->text},
            {},
            {}};
  }
  return {
      Mix(Color{theme.dark ? t.button_dark : t.button_light}, accent,
          t.button_primary_mix),
      Color{theme.dark ? t.hover_dark : t.hover_light}, accent,
      Color{design::kTokens.light.card_shadow}.WithAlpha(alpha(t.shadow_alpha)),
      Color{design::kTokens.light.card_shadow}.WithAlpha(
          alpha(t.button_shadow_alpha))};
}

TabId SplitRepresentative(const SplitGroup& group) noexcept {
  return *std::ranges::min_element(group.tabs());
}
Color SplitOutlineColor(const ThemeVm& theme) noexcept {
  if (!theme.accent.a())
    return {};
  const Color accent = theme.accent.WithAlpha(0xFF);
  if (theme.dark)
    return accent;
  // hsl(from primary h s calc(l - 20)) in zen-split-view.css.
  const float r = accent.r() / 255.0f, g = accent.g() / 255.0f,
              b = accent.b() / 255.0f;
  const float high = std::max({r, g, b}), low = std::min({r, g, b});
  const float range = high - low, lightness = (high + low) / 2;
  float hue = 0, saturation = 0;
  if (range > 0) {
    saturation = range / (1 - std::abs(2 * lightness - 1));
    hue = high == r   ? (g - b) / range + (g < b ? 6 : 0)
          : high == g ? (b - r) / range + 2
                      : (r - g) / range + 4;
    hue /= 6;
  }
  return Hsl(hue, saturation,
             lightness - design::kTokens.split_view.outline_light_darken);
}

TabItemVm PresentMultiPageTab(const Store& store, const Tab& tab) {
  const auto active = store.presented_tab();
  const auto* group = store.SplitForTab(tab.id);
  if (!group)
    return PresentTabItem(tab, active);
  const auto* selected = store.FindTab(group->active);
  auto row = PresentTabItem(selected ? *selected : tab, active);
  row.id = SplitRepresentative(*group);
  row.active = std::ranges::find(group->tabs(), active) != group->tabs().end();
  return row;
}
std::optional<SplitRowVm> PresentSplitRow(const Store& store, TabId tab) {
  const auto* group = store.SplitForTab(tab);
  if (!group)
    return std::nullopt;
  SplitRowVm row{group->id,       SplitRepresentative(*group),
                 group->revision, group->layout,
                 group->count,    {}};
  for (std::size_t i = 0; i < group->count; ++i) {
    if (const auto* member = store.FindTab(group->members[i]))
      row.members[i] = PresentTabItem(*member, group->active);
  }
  return row;
}
}  // namespace kr
