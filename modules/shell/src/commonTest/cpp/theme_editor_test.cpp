#include "krypton/ui/presentation/theme_editor.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

#include "krypton/testing/test.hpp"
#include "krypton/ui/headless/headless.hpp"
#include "krypton/ui/presentation/presenter.hpp"
#include "krypton/ui/presentation/theme_gradient.hpp"
#include "krypton/ui/shell/theme_contract.hpp"

namespace kr {
namespace {
KR_TEST(ThemeEditorPresetsAndHarmoniesFollowReferenceLimits) {
  for (std::size_t i = 0; i < design::kTokens.editor_presets.size(); ++i) {
    WorkspaceTheme theme;
    KR_ASSERT(EditWorkspaceTheme(theme,
                                 {.kind = ThemeEditKind::kPreset, .index = i}));
    KR_EXPECT(ValidWorkspaceTheme(theme));
    KR_EXPECT(theme.point_count > 0 && theme.point_count <= kMaxThemePoints);
    for (std::size_t p = 0; p < theme.point_count; ++p)
      KR_EXPECT_EQ(theme.points[p].color.a(), 255);
  }
  WorkspaceTheme theme;
  KR_EXPECT(!EditWorkspaceTheme(theme, {.kind = ThemeEditKind::kAddPoint}));
  KR_ASSERT(EditWorkspaceTheme(
      theme, {.kind = ThemeEditKind::kMovePrimary, .position = {0.7f, 0.6f}}));
  KR_ASSERT(EditWorkspaceTheme(theme, {.kind = ThemeEditKind::kAddPoint}));
  KR_EXPECT(theme.harmony == ThemeHarmony::kComplementary);
  const auto p = theme.points[0].position;
  KR_EXPECT(std::abs(theme.points[1].position.x - (1 - p.x)) < 0.0001f);
  KR_EXPECT(std::abs(theme.points[1].position.y - (1 - p.y)) < 0.0001f);
  KR_ASSERT(EditWorkspaceTheme(theme, {.kind = ThemeEditKind::kAddPoint}));
  KR_EXPECT_EQ(theme.point_count, 3u);
  KR_EXPECT(!EditWorkspaceTheme(theme, {.kind = ThemeEditKind::kAddPoint}));
  KR_ASSERT(EditWorkspaceTheme(theme, {.kind = ThemeEditKind::kCycleHarmony}));
  KR_EXPECT(theme.harmony == ThemeHarmony::kAnalogous);
  for (int i = 0; i < 3; ++i)
    KR_ASSERT(EditWorkspaceTheme(theme, {.kind = ThemeEditKind::kRemovePoint}));
  KR_EXPECT(!EditWorkspaceTheme(theme, {.kind = ThemeEditKind::kRemovePoint}));
}

KR_TEST(ThemeEditorNormalizesInputAndRejectsInvalidHostValues) {
  const auto point = ClampThemePoint({100, -100});
  KR_EXPECT(std::hypot(point.x - 0.5f, point.y - 0.5f) <= 0.50001f);
  KR_EXPECT(ClampThemePoint({std::numeric_limits<float>::infinity(), 0}) ==
            (Point{0.5f, 0.5f}));
  WorkspaceTheme theme;
  KR_ASSERT(EditWorkspaceTheme(
      theme, {.kind = ThemeEditKind::kOpacity, .value = 100}));
  KR_EXPECT_EQ(theme.opacity, design::kTokens.theme.max_opacity);
  KR_ASSERT(EditWorkspaceTheme(
      theme, {.kind = ThemeEditKind::kTexture, .value = 0.55f}));
  KR_EXPECT_EQ(theme.texture, 9.0f / 16);
  const auto saved = theme;
  KR_EXPECT(!EditWorkspaceTheme(
      theme, {.kind = ThemeEditKind::kOpacity,
              .value = std::numeric_limits<float>::quiet_NaN()}));
  KR_EXPECT(theme == saved);
  auto contract = ToThemeContract(SpaceId{3}, theme);
  KR_ASSERT(FromThemeContract(&contract));
  contract.point_count = 4;
  KR_EXPECT(!FromThemeContract(&contract));
  contract = ToThemeContract(SpaceId{3}, theme);
  contract.struct_size = offsetof(KrWorkspaceTheme, points);
  KR_EXPECT(!FromThemeContract(&contract));
  KR_EXPECT(!FromThemeContract(nullptr));
}

KR_TEST(ThemeEditorSessionKeepsWorkspaceAndDraftSeparate) {
  Store store;
  store.SetSpaces(
      {{.id = SpaceId{1}, .name = "One", .color = Color{0xFF7C5CFF}},
       {.id = SpaceId{2}, .name = "Two", .color = Color{0xFF3B82F6}}},
      SpaceId{1});
  store.OpenThemeEditor(SpaceId{2});
  KR_EXPECT(store.ui().theme_editor.open);
  KR_EXPECT(store.ui().theme_editor.space == SpaceId{2});
  KR_ASSERT(store.EditTheme({.kind = ThemeEditKind::kPreset, .index = 0}));
  KR_EXPECT(store.ui().theme_editor.edited);
  Presenter presenter;
  const auto& vm = presenter.Present(store, {}, Dirty::kAll);
  KR_EXPECT(vm.theme_editor.name == "Two");
  KR_EXPECT(!vm.theme.workspace_theme);  // editing an inactive space
  KR_EXPECT_EQ(vm.theme_editor.presets.size(), 8u);
  (void)store.EditTheme({.kind = ThemeEditKind::kNextPage});
  KR_EXPECT_EQ(store.ui().theme_editor.page, 1u);
  store.CloseThemeEditor();
  KR_EXPECT(!store.ui().theme_editor.open);
}

KR_TEST(ThemeEditorPlacementUsesAvailablePageSpace) {
  const Rect client{0, 0, 1000, 800};
  const auto left = PlaceThemeEditor(client, {0, 0, 186, 800}, 700, false);
  KR_EXPECT(left.x >= 186);
  KR_EXPECT(left.right() <= client.right());
  const auto right = PlaceThemeEditor(client, {814, 0, 186, 800}, 700, true);
  KR_EXPECT(right.right() <= 814);
  KR_EXPECT(right.x >= 0);
  const auto narrow =
      PlaceThemeEditor({0, 0, 380, 240}, {0, 0, 186, 240}, 900, false);
  KR_EXPECT(narrow.width < design::kTokens.theme_editor.width);
  KR_EXPECT(narrow.bottom() <= 240);
}

KR_TEST(ThemeEditorMeasuresResponsiveCanvasPresetsAndControls) {
  const auto& token = design::kTokens.theme_editor;
  const auto wide = MeasureThemeEditor(token.width);
  KR_EXPECT_EQ(wide.canvas_side,
               token.width - 2 * (token.padding + token.border_width));
  KR_EXPECT(!wide.stacked_controls);
  for (const float width :
       {0.0f, token.width / 3, token.width / 2, token.width}) {
    const auto geometry = MeasureThemeEditor(width);
    KR_EXPECT(geometry.canvas_side >= 0 && geometry.canvas_side <= width);
    KR_EXPECT(geometry.preset_columns >= 1 &&
              geometry.preset_columns <= kMaxThemePresetsPerPage);
  }
  const auto narrow = MeasureThemeEditor(token.width / 3);
  KR_EXPECT(narrow.stacked_controls);
  KR_EXPECT(narrow.preset_columns < wide.preset_columns);
}

KR_TEST(
    ThemeGradientCoordinatesRemainContinuousAcrossSurfacesAndDisplayScales) {
  const std::array colors{Color{0xFF79B41D}, Color{0xFFAE23C1},
                          Color{0xFF216DAE}};
  for (std::size_t count = 0; count <= colors.size(); ++count) {
    const auto gradient = MakeThemeGradient(
        Color{design::kTokens.dark.gradient_base},
        std::span{colors}.first(count), design::kTokens.theme.opacity);
    for (const Rect client : {Rect{0, 0, 1200, 800}, Rect{20, 30, 600, 1000}}) {
      const Point seam{client.x + client.width / 4,
                       client.y + client.height * 0.9f};
      const Color expected = SampleThemeGradient(gradient, client, seam);
      // Each island receives the full client translated into its local space.
      for (const Point origin :
           {Point{client.x, client.y}, Point{seam.x, client.y}, seam}) {
        const auto actual =
            SampleThemeGradient(gradient,
                                {client.x - origin.x, client.y - origin.y,
                                 client.width, client.height},
                                {seam.x - origin.x, seam.y - origin.y});
        KR_EXPECT_EQ(actual, expected);
      }
      for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
        const auto actual =
            SampleThemeGradient(gradient,
                                {client.x * scale, client.y * scale,
                                 client.width * scale, client.height * scale},
                                {seam.x * scale, seam.y * scale});
        KR_EXPECT(std::abs(static_cast<int>(actual.r()) - expected.r()) <= 1);
        KR_EXPECT(std::abs(static_cast<int>(actual.g()) - expected.g()) <= 1);
        KR_EXPECT(std::abs(static_cast<int>(actual.b()) - expected.b()) <= 1);
      }
    }
  }
}

struct Host {
  KrShellUI ui{};
  KrWorkspaceTheme latest{};
  int previews = 0, commits = 0;
  KrResult Create(bool editing) {
    headless::Reset();
    KrBrowser browser{};
    browser.struct_size =
        editing ? sizeof(browser) : offsetof(KrBrowser, set_workspace_theme);
    browser.self = this;
    browser.set_workspace_theme = [](void* self, const KrWorkspaceTheme* value,
                                     bool commit) {
      auto& host = *static_cast<Host*>(self);
      host.latest = *value;
      if (commit)
        ++host.commits;
      else
        ++host.previews;
    };
    ui.struct_size = sizeof(ui);
    const KrCreateParams params{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                nullptr, &browser};
    return kr_ui_create(&params, &ui);
  }
  ~Host() {
    if (ui.self)
      ui.destroy(ui.self);
    headless::Reset();
  }
};

KR_TEST(ThemeEditorAbiPreviewsThenCommitsOnceAndSupportsOlderHosts) {
  {
    Host old;
    KR_ASSERT(old.Create(false) == KR_OK);
    KR_EXPECT(old.ui.show_theme_editor(old.ui.self, 0) == KR_ERR_UNSUPPORTED);
  }
  Host host;
  KR_ASSERT(host.Create(true) == KR_OK);
  const KrSpace space{.struct_size = sizeof(KrSpace),
                      .id = 1,
                      .name = {"One", 3},
                      .color = 0xFF7C5CFF};
  host.ui.set_spaces(host.ui.self, &space, 1, sizeof(KrSpace), 1);
  KR_ASSERT(host.ui.show_theme_editor(host.ui.self, 1) == KR_OK);
  auto& input = *headless::Delegate();
  input.OnThemeEdited({.kind = ThemeEditKind::kPreset, .index = 0});
  KR_EXPECT_EQ(host.previews, 1);
  KR_EXPECT_EQ(host.commits, 0);
  KR_EXPECT_EQ(host.latest.space, 1u);
  input.OnThemeEditorDismissed();
  input.OnThemeEditorDismissed();
  KR_EXPECT_EQ(host.commits, 1);
  KR_EXPECT(host.ui.show_theme_editor(host.ui.self, 999) ==
            KR_ERR_INVALID_ARGUMENT);
}

KR_TEST(ThemeEditorAbiPreservesOutputCapacityAndIgnoresPartialHostSlots) {
  for (const auto capacity : {offsetof(KrShellUI, set_workspace_theme),
                              offsetof(KrShellUI, show_theme_editor) +
                                  sizeof(KrShellUI::show_theme_editor) / 2}) {
    headless::Reset();
    KrShellUI ui{};
    const auto bytes = std::as_writable_bytes(std::span{&ui, std::size_t{1}});
    std::fill(bytes.begin() + capacity, bytes.end(), std::byte{0x5a});
    ui.struct_size = capacity;
    KrBrowser browser{};
    browser.struct_size = offsetof(KrBrowser, set_workspace_theme) +
                          sizeof(KrBrowser::set_workspace_theme) / 2;
    const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                                &browser};
    KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
    struct Cleanup {
      KrShellUI& ui;
      ~Cleanup() {
        ui.destroy(ui.self);
        headless::RunPendingTasks();
      }
    } cleanup{ui};
    KR_EXPECT_EQ(ui.struct_size, capacity);
    for (std::size_t i = capacity; i < bytes.size(); ++i)
      KR_EXPECT(bytes[i] == std::byte{0x5a});
    KR_EXPECT(!KR_HAS_SLOT(&ui, KrShellUI, show_theme_editor));
  }
  headless::Reset();
  KrBrowser browser{};
  browser.struct_size = offsetof(KrBrowser, set_workspace_theme) +
                        sizeof(KrBrowser::set_workspace_theme) / 2;
  KrShellUI ui{};
  ui.struct_size = sizeof(ui);
  const KrCreateParams params{sizeof(params), KR_UI_ABI_VERSION, nullptr,
                              &browser};
  KR_ASSERT(kr_ui_create(&params, &ui) == KR_OK);
  KR_EXPECT_EQ(ui.show_theme_editor(ui.self, 0), KR_ERR_UNSUPPORTED);
  ui.destroy(ui.self);
  headless::RunPendingTasks();
}
}  // namespace
}  // namespace kr
