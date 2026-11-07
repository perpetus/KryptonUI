#include "krypton/testing/test.hpp"
#include "krypton/ui/layout/shell_layout.hpp"

namespace kr {
namespace {

constexpr float kMargin = design::kTokens.spacing.window_margin;
constexpr float kDefault = design::kTokens.sidebar.default_width;
constexpr float kToolbar = design::kTokens.spacing.toolbar_height;
constexpr float kRail = design::kTokens.sidebar.collapsed_width;

WindowState Window(float width,
                   float height,
                   Flags<WindowFlag> flags = WindowFlag::kActive) {
  return {.flags = flags, .scale_factor = 1, .client = {0, 0, width, height}};
}

bool Overlaps(Rect a, Rect b) {
  return !a.empty() && !b.empty() && a.x < b.right() && a.right() > b.x &&
         a.y < b.bottom() && a.bottom() > b.y;
}

bool Within(Rect outer, Rect inner) {
  return inner.empty() ||
         (inner.x >= outer.x && inner.y >= outer.y &&
          inner.right() <= outer.right() && inner.bottom() <= outer.bottom());
}

KR_TEST(LayoutDefaultsToSidebarAndTopToolbar) {
  const ShellLayout layout = ComputeLayout(Window(1400, 900), {}, false);
  KR_EXPECT(layout.toolbar_layout == ToolbarLayout::kMultiple);
  KR_EXPECT(layout.sidebar_visible);
  KR_EXPECT(layout.toolbar_visible);
  KR_EXPECT(!layout.sidebar_icon_only);
  KR_EXPECT_EQ(layout.sidebar, (Rect{0, 0, kDefault, 900}));
  KR_EXPECT_EQ(layout.toolbar, (Rect{kDefault, 0, 1400 - kDefault, kToolbar}));
  KR_EXPECT_EQ(layout.card, (Rect{kDefault, kToolbar, 1400 - kDefault - kMargin,
                                  900 - kToolbar - kMargin}));
  KR_EXPECT_EQ(layout.card_radius, design::kTokens.radius.card);
  KR_ASSERT(layout.drag_area().size() == 2);
  KR_EXPECT_EQ(layout.drag_area()[0],
               (Rect{0, 0, kDefault, design::kTokens.spacing.sidebar_padding}));
}

KR_TEST(LayoutOnlySidebarKeepsAddressControlsBesideThePage) {
  const ShellLayout layout = ComputeLayout(
      Window(1400, 900), {.flags = LayoutFlag::kToolbarSingle}, false);
  // The default width is too narrow for the caption buttons that share the
  // sidebar's top row in this layout.
  const float width = MinSidebarWidth(LayoutFlag::kToolbarSingle);
  KR_EXPECT(width > kDefault);
  KR_EXPECT(layout.toolbar_layout == ToolbarLayout::kSingle);
  KR_EXPECT(!layout.toolbar_visible);
  KR_EXPECT(layout.toolbar.empty());
  KR_EXPECT_EQ(layout.sidebar_width, width);
  KR_EXPECT_EQ(layout.card, (Rect{width, kMargin, 1400 - width - kMargin,
                                  900 - 2 * kMargin}));
  KR_ASSERT(layout.drag_area().size() == 2);
  KR_EXPECT_EQ(layout.drag_area()[0],
               (Rect{layout.card.x, 0, layout.card.width, kMargin}));
}

KR_TEST(LayoutCollapsedSidebarKeepsAnIconRailAndTopToolbar) {
  const ShellLayout layout = ComputeLayout(
      Window(1400, 900), {.flags = LayoutFlag::kToolbarCollapsed}, false);
  KR_EXPECT(layout.toolbar_layout == ToolbarLayout::kCollapsed);
  KR_EXPECT(layout.sidebar_visible);
  KR_EXPECT(layout.sidebar_icon_only);
  KR_EXPECT(layout.toolbar_visible);
  KR_EXPECT_EQ(layout.sidebar.width, kRail);
  KR_EXPECT_EQ(layout.sidebar_width, kRail);
  KR_EXPECT_EQ(layout.toolbar.x, kRail);
  KR_EXPECT_EQ(layout.card.x, kRail);

  const ShellLayout both = ComputeLayout(
      Window(1400, 900),
      {.flags = LayoutFlag::kToolbarCollapsed | LayoutFlag::kToolbarSingle},
      false);
  KR_EXPECT(both.toolbar_layout == ToolbarLayout::kCollapsed);
}

KR_TEST(LayoutIconOnlyFlagDoesNotHideTheSidebar) {
  const ShellLayout layout = ComputeLayout(
      Window(1400, 900), {.flags = LayoutFlag::kSidebarIconOnly}, false);
  KR_EXPECT(layout.sidebar_visible);
  KR_EXPECT(layout.sidebar_icon_only);
  KR_EXPECT_EQ(layout.sidebar.width, kRail);
}

KR_TEST(LayoutMirrorsBothNativeRegionsForRightSidebar) {
  const ShellLayout layout = ComputeLayout(
      Window(1400, 900), {.flags = LayoutFlag::kSidebarRight}, false);
  KR_EXPECT_EQ(layout.sidebar, (Rect{1400 - kDefault, 0, kDefault, 900}));
  KR_EXPECT_EQ(layout.toolbar, (Rect{0, 0, 1400 - kDefault, kToolbar}));
  KR_EXPECT_EQ(layout.card.x, kMargin);
  KR_EXPECT_EQ(layout.card.right(), 1400 - kDefault);
}

KR_TEST(LayoutSidebarModesMapToLayoutFlags) {
  KR_EXPECT(SidebarModeOf({}) == SidebarMode::kExpanded);
  KR_EXPECT(SidebarModeOf(LayoutFlag::kSidebarIconOnly) ==
            SidebarMode::kCollapsed);
  KR_EXPECT(SidebarModeOf(LayoutFlag::kToolbarCollapsed) ==
            SidebarMode::kCollapsed);
  KR_EXPECT(SidebarModeOf(LayoutFlag::kSidebarAutohide) ==
            SidebarMode::kFloating);
  KR_EXPECT(SidebarModeOf(LayoutFlag::kSidebarCollapsed) ==
            SidebarMode::kFloating);

  const Flags<LayoutFlag> kept =
      LayoutFlag::kSidebarRight | LayoutFlag::kCompactMode;
  const auto collapsed = WithSidebarMode(kept, SidebarMode::kCollapsed);
  KR_EXPECT(SidebarModeOf(collapsed) == SidebarMode::kCollapsed);
  KR_EXPECT(collapsed.Has(LayoutFlag::kSidebarRight));
  KR_EXPECT(collapsed.Has(LayoutFlag::kCompactMode));
  const auto floating = WithSidebarMode(collapsed, SidebarMode::kFloating);
  KR_EXPECT(SidebarModeOf(floating) == SidebarMode::kFloating);
  KR_EXPECT(!floating.Has(LayoutFlag::kSidebarIconOnly));
  KR_EXPECT(WithSidebarMode(floating, SidebarMode::kExpanded) == kept);

  // Leaving the icon rail leaves the Collapsed Sidebar layout, which would
  // otherwise force the rail back.
  const auto expanded = WithSidebarMode(
      LayoutFlag::kToolbarCollapsed | LayoutFlag::kSidebarIconOnly,
      SidebarMode::kExpanded);
  KR_EXPECT(expanded.none());
  KR_EXPECT(!ComputeLayout(Window(1400, 900), {.flags = expanded}, false)
                 .sidebar_icon_only);
}

KR_TEST(BrowserLayoutChoicesNormalizeSidebarPresentationAndKeepCompactPrefs) {
  const auto kept = LayoutFlag::kSidebarRight | LayoutFlag::kCompactMode |
                    LayoutFlag::kCompactHideToolbar;
  const auto stale = kept | LayoutFlag::kSidebarCollapsed |
                     LayoutFlag::kSidebarAutohide |
                     LayoutFlag::kSidebarIconOnly | LayoutFlag::kToolbarSingle |
                     LayoutFlag::kToolbarCollapsed;
  for (const auto mode : {ToolbarLayout::kSingle, ToolbarLayout::kMultiple,
                          ToolbarLayout::kCollapsed}) {
    const auto flags = WithBrowserLayout(stale, mode);
    KR_EXPECT(BrowserLayoutOf(flags) == mode);
    KR_EXPECT((flags & kept) == kept);
    KR_EXPECT(!flags.Has(LayoutFlag::kSidebarCollapsed));
    KR_EXPECT(!flags.Has(LayoutFlag::kSidebarAutohide));
    const auto layout =
        ComputeLayout(Window(1400, 900), {.flags = flags}, true, true);
    KR_EXPECT(layout.sidebar_visible);
    KR_EXPECT(layout.sidebar_icon_only == (mode == ToolbarLayout::kCollapsed));
    if (mode == ToolbarLayout::kSingle)
      KR_EXPECT(layout.toolbar.empty());
    else
      KR_EXPECT(!layout.toolbar.empty());
  }
}

KR_TEST(CollapsingOnlySidebarMovesItsAddressControlsToTheTopToolbar) {
  const auto flags =
      WithSidebarMode(LayoutFlag::kToolbarSingle, SidebarMode::kCollapsed);
  const auto layout = ComputeLayout(Window(1400, 900), {.flags = flags}, false);
  KR_EXPECT(layout.toolbar_layout == ToolbarLayout::kCollapsed);
  KR_EXPECT(layout.sidebar_icon_only);
  KR_EXPECT(layout.toolbar_visible);
  KR_EXPECT(!flags.Has(LayoutFlag::kToolbarSingle));
  const auto expanded = WithSidebarMode(flags, SidebarMode::kExpanded);
  KR_EXPECT(BrowserLayoutOf(expanded) == ToolbarLayout::kMultiple);
  KR_EXPECT(!expanded.Has(LayoutFlag::kSidebarIconOnly));
}

KR_TEST(LayoutSidebarTogglesStepThroughTheirModes) {
  using enum SidebarMode;
  KR_EXPECT(NextSidebarMode(kExpanded, SidebarToggle::kCycle) == kCollapsed);
  KR_EXPECT(NextSidebarMode(kCollapsed, SidebarToggle::kCycle) == kFloating);
  KR_EXPECT(NextSidebarMode(kFloating, SidebarToggle::kCycle) == kExpanded);
  KR_EXPECT(NextSidebarMode(kExpanded, SidebarToggle::kCollapse) == kCollapsed);
  KR_EXPECT(NextSidebarMode(kCollapsed, SidebarToggle::kCollapse) == kExpanded);
  KR_EXPECT(NextSidebarMode(kFloating, SidebarToggle::kCollapse) == kExpanded);
}

KR_TEST(LayoutSidebarWidthsAreWholeDipsWithinTheirLimits) {
  const auto& tokens = design::kTokens;
  KR_EXPECT_EQ(ClampSidebarWidth(200.4f, {}), 200.0f);
  KR_EXPECT_EQ(ClampSidebarWidth(200.6f, {}), 201.0f);
  KR_EXPECT_EQ(ClampSidebarWidth(1, {}), tokens.sidebar.min_width);
  KR_EXPECT_EQ(ClampSidebarWidth(9999, {}), tokens.sidebar.max_width);
  KR_EXPECT_EQ(
      MinSidebarWidth(LayoutFlag::kToolbarSingle),
      tokens.sidebar.caption_row_min_width + 3 * tokens.caption.button_width);
  KR_EXPECT_EQ(MinSidebarWidth(LayoutFlag::kToolbarSingle |
                               LayoutFlag::kToolbarCollapsed),
               tokens.sidebar.min_width);
}

KR_TEST(LayoutClampsSidebarWidthAndUsesTokensForChrome) {
  const auto& sidebar = design::kTokens.sidebar;
  KR_EXPECT_EQ(ComputeLayout(Window(1400, 900), {.sidebar_width = 10}, false)
                   .sidebar_width,
               sidebar.min_width);
  KR_EXPECT_EQ(ComputeLayout(Window(1400, 900), {.sidebar_width = 5000}, false)
                   .sidebar_width,
               sidebar.max_width);

  auto tokens = design::kTokens;
  tokens.spacing.toolbar_height = 43;
  tokens.sidebar.collapsed_width = 67;
  const ShellLayout custom =
      ComputeLayout(Window(1400, 900), {.flags = LayoutFlag::kToolbarCollapsed},
                    false, tokens);
  KR_EXPECT_EQ(custom.toolbar.height, 43.0f);
  KR_EXPECT_EQ(custom.sidebar.width, 67.0f);
}

KR_TEST(LayoutHiddenSidebarRetainsTheTopToolbarAndNarrowWindowsStayUsable) {
  const ShellLayout hidden = ComputeLayout(
      Window(1400, 900), {.flags = LayoutFlag::kSidebarCollapsed}, true);
  KR_EXPECT(!hidden.sidebar_visible);
  KR_EXPECT(hidden.sidebar.empty());
  KR_EXPECT_EQ(hidden.toolbar, (Rect{0, 0, 1400, kToolbar}));
  KR_EXPECT_EQ(hidden.card.x, kMargin);

  const ShellLayout narrow = ComputeLayout(Window(400, 700), {}, false);
  KR_EXPECT(!narrow.sidebar_visible);
  KR_EXPECT(narrow.toolbar_visible);
  KR_EXPECT_EQ(narrow.card.x, kMargin);
  const ShellLayout narrow_rail = ComputeLayout(
      Window(400, 700), {.flags = LayoutFlag::kToolbarCollapsed}, false);
  KR_EXPECT(narrow_rail.sidebar_visible);
  KR_EXPECT_EQ(narrow_rail.sidebar.width, kRail);
}

KR_TEST(LayoutFullscreenHasNoChromeOrRevealStrips) {
  for (const WindowFlag fullscreen :
       {WindowFlag::kFullscreen, WindowFlag::kTabFullscreen}) {
    const ShellLayout layout =
        ComputeLayout(Window(1400, 900, fullscreen),
                      {.flags = LayoutFlag::kCompactMode}, true, true);
    KR_EXPECT(!layout.sidebar_visible);
    KR_EXPECT(!layout.toolbar_visible);
    KR_EXPECT(layout.sidebar.empty());
    KR_EXPECT(layout.toolbar.empty());
    KR_EXPECT_EQ(layout.card, (Rect{0, 0, 1400, 900}));
    KR_EXPECT_EQ(layout.card_radius, 0.0f);
    KR_EXPECT(layout.drag_area().empty());
  }
}

KR_TEST(LayoutAutohideKeepsAnEventStripAndRevealsWithoutMovingThePage) {
  const LayoutPrefs prefs{.flags = LayoutFlag::kSidebarAutohide};
  const ShellLayout hidden = ComputeLayout(Window(1400, 900), prefs, false);
  KR_EXPECT(!hidden.sidebar_visible);
  KR_EXPECT_EQ(hidden.sidebar.width, design::kTokens.sidebar.reveal_width);
  KR_EXPECT(hidden.sidebar.right() <= hidden.card.x);
  KR_EXPECT(!Overlaps(hidden.sidebar, hidden.toolbar));

  const ShellLayout revealed = ComputeLayout(Window(1400, 900), prefs, true);
  KR_EXPECT(revealed.sidebar_visible);
  KR_EXPECT(revealed.sidebar_overlays);
  KR_EXPECT_EQ(revealed.sidebar.width, kDefault);
  KR_EXPECT_EQ(revealed.card, hidden.card);
  KR_EXPECT(!Overlaps(revealed.sidebar, revealed.toolbar));
}

KR_TEST(LayoutCompactDefaultsToHidingTheSidebar) {
  const LayoutPrefs prefs{.flags = LayoutFlag::kCompactMode};
  const ShellLayout hidden =
      ComputeLayout(Window(1400, 900), prefs, false, false);
  KR_EXPECT(hidden.compact_mode == CompactMode::kHideSidebar);
  KR_EXPECT(!hidden.sidebar_visible);
  KR_EXPECT(hidden.toolbar_visible);
  KR_EXPECT(!hidden.toolbar_overlays);
  KR_EXPECT_EQ(hidden.card.x, kMargin);

  const ShellLayout revealed =
      ComputeLayout(Window(1400, 900), prefs, true, true);
  KR_EXPECT(revealed.sidebar_overlays);
  KR_EXPECT(!revealed.toolbar_overlays);
  KR_EXPECT_EQ(revealed.card, hidden.card);
}

KR_TEST(LayoutCompactCanHideOnlyTheTopToolbar) {
  const LayoutPrefs prefs{.flags = LayoutFlag::kCompactMode |
                                   LayoutFlag::kCompactHideToolbar};
  const ShellLayout hidden =
      ComputeLayout(Window(1400, 900), prefs, false, false);
  KR_EXPECT(hidden.compact_mode == CompactMode::kHideToolbar);
  KR_EXPECT(hidden.sidebar_visible);
  KR_EXPECT(!hidden.toolbar_visible);
  KR_EXPECT(hidden.toolbar.height <= kMargin);
  KR_EXPECT_EQ(hidden.card.y, kMargin);

  const ShellLayout revealed =
      ComputeLayout(Window(1400, 900), prefs, false, true);
  KR_EXPECT(revealed.toolbar_visible);
  KR_EXPECT(revealed.toolbar_overlays);
  KR_EXPECT_EQ(revealed.card, hidden.card);
  KR_EXPECT(!Overlaps(revealed.sidebar, revealed.toolbar));
}

KR_TEST(LayoutCompactCanHideBothBarsAndKeepRevealedIslandsDisjoint) {
  const LayoutPrefs prefs{.flags = LayoutFlag::kCompactMode |
                                   LayoutFlag::kCompactHideSidebar |
                                   LayoutFlag::kCompactHideToolbar};
  const ShellLayout hidden =
      ComputeLayout(Window(1400, 900), prefs, false, false);
  KR_EXPECT(hidden.compact_mode == CompactMode::kHideBoth);
  KR_EXPECT(!hidden.sidebar_visible);
  KR_EXPECT(!hidden.toolbar_visible);
  KR_EXPECT_EQ(hidden.card, (Rect{0, 0, 1400, 900}.Inset(kMargin)));
  const ShellLayout revealed =
      ComputeLayout(Window(1400, 900), prefs, true, true);
  KR_EXPECT(revealed.sidebar_overlays);
  KR_EXPECT(revealed.toolbar_overlays);
  KR_EXPECT_EQ(revealed.card, hidden.card);
  KR_EXPECT(!Overlaps(revealed.sidebar, revealed.toolbar));
}

KR_TEST(LayoutCompactTargetsNeedTheMasterFlagAndSingleLayoutHidesItsSharedBar) {
  const ShellLayout inactive =
      ComputeLayout(Window(1400, 900),
                    {.flags = LayoutFlag::kCompactHideSidebar |
                              LayoutFlag::kCompactHideToolbar},
                    false, false);
  KR_EXPECT(inactive.compact_mode == CompactMode::kOff);
  KR_EXPECT(inactive.sidebar_visible);
  KR_EXPECT(inactive.toolbar_visible);

  const ShellLayout single = ComputeLayout(
      Window(1400, 900),
      {.flags = LayoutFlag::kToolbarSingle | LayoutFlag::kCompactMode |
                LayoutFlag::kCompactHideToolbar},
      false, true);
  KR_EXPECT(single.compact_mode == CompactMode::kHideSidebar);
  KR_EXPECT(!single.sidebar_visible);
  KR_EXPECT(!single.toolbar_visible);
  KR_EXPECT(single.toolbar.empty());
}

KR_TEST(LayoutToolbarWindowMovingGeometryFollowsItsNativeBounds) {
  const WindowState window{.client = {20, 30, 1400, 900}};
  const LayoutPrefs prefs{.flags = LayoutFlag::kSidebarRight |
                                   LayoutFlag::kCompactMode |
                                   LayoutFlag::kCompactHideToolbar};
  const ShellLayout hidden = ComputeLayout(window, prefs, false, false);
  for (const Rect region : hidden.drag_area())
    KR_EXPECT(region != hidden.toolbar);
  const ShellLayout revealed = ComputeLayout(window, prefs, false, true);
  KR_ASSERT(!revealed.drag_area().empty());
  KR_EXPECT_EQ(revealed.drag_area().back(), revealed.toolbar);
  KR_EXPECT_EQ(revealed.toolbar.x, window.client.x);
  KR_EXPECT_EQ(revealed.toolbar.y, window.client.y);

  const ShellLayout normal = ComputeLayout(window, {}, false, false);
  KR_EXPECT_EQ(normal.drag_area().back(), normal.toolbar);
}

KR_TEST(LayoutAllModesKeepSteadyRegionsOutsideThePage) {
  const WindowState window{.scale_factor = 1, .client = {20, 30, 1400, 900}};
  for (const auto toolbar :
       {Flags<LayoutFlag>{}, Flags<LayoutFlag>{LayoutFlag::kToolbarSingle},
        Flags<LayoutFlag>{LayoutFlag::kToolbarCollapsed}}) {
    for (const auto compact :
         {Flags<LayoutFlag>{}, Flags<LayoutFlag>{LayoutFlag::kCompactMode},
          LayoutFlag::kCompactMode | LayoutFlag::kCompactHideToolbar,
          LayoutFlag::kCompactMode | LayoutFlag::kCompactHideSidebar |
              LayoutFlag::kCompactHideToolbar}) {
      for (const auto side : {Flags<LayoutFlag>{},
                              Flags<LayoutFlag>{LayoutFlag::kSidebarRight}}) {
        const LayoutPrefs prefs{.flags = toolbar | compact | side};
        const ShellLayout layout = ComputeLayout(window, prefs, false, false);
        KR_EXPECT(Within(window.client, layout.sidebar));
        KR_EXPECT(Within(window.client, layout.toolbar));
        KR_EXPECT(Within(window.client, layout.card));
        KR_EXPECT(!Overlaps(layout.sidebar, layout.card));
        KR_EXPECT(!Overlaps(layout.toolbar, layout.card));
        KR_EXPECT(!Overlaps(layout.sidebar, layout.toolbar));
        const ShellLayout revealed = ComputeLayout(window, prefs, true, true);
        KR_EXPECT_EQ(revealed.card, layout.card);
        KR_EXPECT(!Overlaps(revealed.sidebar, revealed.toolbar));
      }
    }
  }
}

KR_TEST(LayoutTinyAndEmptyWindowsNeverProduceNegativeOrOutOfBoundsRegions) {
  for (const Rect client :
       {Rect{20, 30, 40, 5}, Rect{20, 30, 1, 1}, Rect{20, 30, 0, 0}}) {
    const WindowState window{.client = client};
    for (const bool revealed : {false, true}) {
      const ShellLayout layout = ComputeLayout(
          window,
          {.flags = LayoutFlag::kCompactMode | LayoutFlag::kCompactHideSidebar |
                    LayoutFlag::kCompactHideToolbar},
          revealed, revealed);
      for (const Rect rect : {layout.sidebar, layout.toolbar, layout.card}) {
        KR_EXPECT(rect.width >= 0);
        KR_EXPECT(rect.height >= 0);
        KR_EXPECT(Within(client, rect));
      }
    }
  }
}

KR_TEST(LayoutDragAreasUseOffsetClientCoordinatesAndStayAboveControls) {
  const WindowState window{.client = {20, 30, 1400, 900}};
  auto tokens = design::kTokens;
  tokens.spacing.drag_strip_height = tokens.spacing.sidebar_padding * 2;
  const ShellLayout layout = ComputeLayout(
      window, {.flags = LayoutFlag::kSidebarRight | LayoutFlag::kToolbarSingle},
      false, tokens);
  KR_ASSERT(layout.drag_area().size() == 2);
  KR_EXPECT_EQ(layout.drag_area()[0], (Rect{layout.card.x, window.client.y,
                                            layout.card.width, kMargin}));
  KR_EXPECT_EQ(layout.drag_area()[1],
               (Rect{layout.sidebar.x, layout.sidebar.y, layout.sidebar.width,
                     tokens.spacing.sidebar_padding}));
}

}  // namespace
}  // namespace kr
