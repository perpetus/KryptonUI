#include "krypton/testing/test.hpp"
#include "krypton/ui/layout/shell_layout.hpp"

namespace kr {
namespace {

KR_TEST(NativeCaptionInsetsKeepIslandsAndPageClearOfSystemButtons) {
  const WindowState window{.scale_factor = 1.5f, .client = {0, 0, 1280, 800}};
  const NativeTitleBarMetrics native{
      .enabled = true, .height = 32, .right_inset = 138};
  for (const bool on_right : {false, true}) {
    for (const auto mode :
         {Flags<LayoutFlag>{}, Flags<LayoutFlag>{LayoutFlag::kToolbarSingle},
          Flags<LayoutFlag>{LayoutFlag::kToolbarCollapsed}}) {
      LayoutPrefs prefs{.flags = mode, .sidebar_width = 186};
      prefs.flags.Set(LayoutFlag::kSidebarRight, on_right);
      const auto layout =
          ComputeLayout(window, prefs, false, false, design::kTokens, native);
      KR_EXPECT(layout.native_titlebar.enabled);
      if (layout.toolbar_layout == ToolbarLayout::kSingle)
        KR_EXPECT_EQ(layout.card.y, design::kTokens.spacing.window_margin);
      else
        KR_EXPECT(layout.card.y >= native.height);
      if (layout.toolbar_visible)
        KR_EXPECT(layout.toolbar.right() <=
                  window.client.right() - native.right_inset);
      if (on_right && layout.sidebar_visible && !layout.caption_overlay)
        KR_EXPECT(layout.sidebar.y >= native.height);
      const Rect caption{window.client.right() - native.right_inset,
                         window.client.y, native.right_inset, native.height};
      if (!layout.caption_overlay)
        for (const auto& region : layout.drag_area())
          KR_EXPECT(region.y >= caption.bottom() ||
                    region.right() <= caption.x);
    }
  }
}

KR_TEST(NativeCaptionDoesNotReserveChromeInFullscreen) {
  const WindowState window{.flags = WindowFlag::kFullscreen,
                           .client = {0, 0, 1280, 800}};
  const NativeTitleBarMetrics native{
      .enabled = true, .height = 32, .right_inset = 138};
  const auto layout =
      ComputeLayout(window, {}, false, false, design::kTokens, native);
  KR_EXPECT(layout.card == window.client);
  KR_EXPECT(layout.sidebar.empty());
  KR_EXPECT(layout.toolbar.empty());
}

}  // namespace
}  // namespace kr

namespace kr {
KR_TEST(OnlySidebarNativeCaptionCollapsesAtRestAndRevealsWithoutMovingThePage) {
  const WindowState window{.scale_factor = 1.75f,
                           .client = {17, 23, 1280, 800}};
  const NativeTitleBarMetrics native{
      .enabled = true, .height = 48, .left_inset = 12, .right_inset = 180};
  const LayoutPrefs prefs{.flags = LayoutFlag::kToolbarSingle};
  const auto hidden =
      ComputeLayout(window, prefs, false, false, design::kTokens, native);
  const auto shown =
      ComputeLayout(window, prefs, false, true, design::kTokens, native);
  KR_EXPECT(hidden.caption_overlay && !hidden.caption_visible);
  KR_EXPECT(shown.caption_overlay && shown.caption_visible);
  KR_EXPECT_EQ(hidden.card, shown.card);
  KR_EXPECT_EQ(hidden.card.y,
               window.client.y + design::kTokens.spacing.window_margin);
  KR_EXPECT_EQ(hidden.caption_reveal.height,
               design::kTokens.spacing.window_margin);
  KR_EXPECT_EQ(shown.caption_reveal.height, native.height);
  KR_EXPECT_EQ(hidden.sidebar.y, window.client.y);
  KR_EXPECT(hidden.toolbar.empty() && shown.toolbar.empty());
}

KR_TEST(OnlySidebarRightCaptionStaysAtItsNativeSidebarEdge) {
  const NativeTitleBarMetrics native{
      .enabled = true, .height = 32, .right_inset = 138};
  const auto layout = ComputeLayout(
      {.client = {0, 0, 1280, 800}},
      {.flags = LayoutFlag::kToolbarSingle | LayoutFlag::kSidebarRight}, false,
      false, design::kTokens, native);
  KR_EXPECT(!layout.caption_overlay && layout.caption_reveal.empty());
  KR_EXPECT_EQ(layout.card.y, design::kTokens.spacing.window_margin);
  KR_EXPECT_EQ(layout.sidebar.y, native.height);
}

KR_TEST(OnlySidebarCompactAndNarrowRevealsDoNotRestoreAnEmptyCaptionBand) {
  const NativeTitleBarMetrics native{
      .enabled = true, .height = 32, .right_inset = 138};
  const WindowState window{.client = {0, 0, 1280, 800}};
  const LayoutPrefs compact{.flags = LayoutFlag::kToolbarSingle |
                                     LayoutFlag::kCompactMode};
  const auto hidden =
      ComputeLayout(window, compact, false, false, design::kTokens, native);
  const auto shown =
      ComputeLayout(window, compact, true, true, design::kTokens, native);
  KR_EXPECT_EQ(hidden.card, shown.card);
  KR_EXPECT_EQ(hidden.card.y, design::kTokens.spacing.window_margin);
  KR_EXPECT(!hidden.sidebar.empty());
  KR_EXPECT(shown.sidebar_overlays && shown.sidebar_visible);
  const WindowState narrow{.client = {0, 0, 400, 600}};
  const LayoutPrefs single{.flags = LayoutFlag::kToolbarSingle};
  const auto edge =
      ComputeLayout(narrow, single, false, false, design::kTokens, native);
  const auto revealed =
      ComputeLayout(narrow, single, true, false, design::kTokens, native);
  KR_EXPECT(!edge.sidebar.empty());
  KR_EXPECT(!edge.sidebar_visible && revealed.sidebar_visible);
  KR_EXPECT_EQ(edge.card, revealed.card);
  KR_EXPECT_EQ(revealed.sidebar.y, 0.0f);

  const LayoutPrefs right_compact{.flags = compact.flags |
                                           LayoutFlag::kSidebarRight};
  const auto right_hidden = ComputeLayout(window, right_compact, false, false,
                                          design::kTokens, native);
  const auto right_shown =
      ComputeLayout(window, right_compact, true, true, design::kTokens, native);
  KR_EXPECT(right_hidden.caption_overlay && !right_hidden.caption_visible);
  KR_EXPECT(right_shown.caption_visible && right_shown.sidebar_visible);
  KR_EXPECT_EQ(right_hidden.card, right_shown.card);
  KR_EXPECT(right_shown.sidebar.y >= native.height);
}
}  // namespace kr
