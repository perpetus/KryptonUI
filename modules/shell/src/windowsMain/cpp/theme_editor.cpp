#include "theme_editor.hpp"

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

#include <algorithm>
#include <cmath>

#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/presentation/theme_editor.hpp"
#include "native_input.hpp"

namespace kr::win {
ThemeEditor::~ThemeEditor() {
  lifetime_.reset();
  loaded_.revoke();
  canvas_loaded_.revoke();
  owner_sized_.revoke();
  closed_.revoke();
  key_.revoke();
  fallback_key_.revoke();
  Close();
}

std::unique_ptr<NativeButton> ThemeEditor::Button(
    const design::icons::Icon& icon,
    ThemeEdit edit) {
  auto result = SourceButton(context_.brushes, icon, L"", [this, edit] {
    context_.delegate.OnThemeEdited(edit);
  });
  const auto& t = kTokens.theme_editor;
  result->control.Width(t.button_size);
  result->control.Height(t.button_size);
  result->control.Margin(UniformThickness(0));
  result->control.Resources().Insert(
      winrt::box_value(L"ButtonBackgroundPointerOver"),
      context_.brushes.editor_hover);
  result->control.Resources().Insert(
      winrt::box_value(L"ButtonBackgroundPressed"),
      context_.brushes.editor_selection);
  const auto view = result->control.Content().as<controls::Viewbox>();
  view.Width(t.icon_size);
  view.Height(t.icon_size);
  const auto canvas = view.Child().as<controls::Canvas>();
  for (const auto& child : canvas.Children())
    child.as<xaml::Shapes::Path>().Fill(context_.brushes.editor_foreground);
  return result;
}

void ThemeEditor::Build() {
  if (owner_)
    return;
  const auto& t = kTokens.theme_editor;
  island_.Ensure();
  owner_ = island_.Root();
  context_.brushes.Install(owner_);
  popup_ = controls::Primitives::Popup();
  popup_.IsLightDismissEnabled(true);
  surface_ = controls::Border();
  context_.brushes.Install(surface_);
  surface_.Background(context_.brushes.overlay);
  surface_.BorderBrush(context_.brushes.preview_border);
  surface_.BorderThickness(UniformThickness(t.border_width));
  surface_.CornerRadius(UniformCorners(t.radius));
  surface_.Padding(UniformThickness(t.padding));
  scroll_ = controls::ScrollViewer();
  scroll_.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
  scroll_.HorizontalScrollBarVisibility(
      controls::ScrollBarVisibility::Disabled);
  content_ = controls::StackPanel();
  content_.Spacing(t.row_gap);
  scroll_.Content(content_);
  surface_.Child(scroll_);
  popup_.Child(surface_);

  canvas_ = std::make_unique<ThemeCanvas>(context_);
  controls_ = std::make_unique<ThemeControls>(context_);
  board_ = controls::Grid();
  board_.Children().Append(canvas_->Root());
  content_.Children().Append(board_);
  schemes_ = controls::StackPanel();
  schemes_.Orientation(controls::Orientation::Horizontal);
  schemes_.Spacing(t.button_gap);
  schemes_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  schemes_.VerticalAlignment(xaml::VerticalAlignment::Top);
  schemes_.Margin({0, t.scheme_top, 0, 0});
  const std::array icons{&design::icons::kSparkles, &design::icons::kFaceSun,
                         &design::icons::kMoonStars};
  for (std::size_t i = 0; i < scheme_buttons_.size(); ++i) {
    scheme_buttons_[i] =
        Button(*icons[i], {.kind = ThemeEditKind::kScheme, .index = i});
    schemes_.Children().Append(scheme_buttons_[i]->control);
  }
  board_.Children().Append(schemes_);
  actions_ = controls::StackPanel();
  actions_.Orientation(controls::Orientation::Horizontal);
  actions_.Spacing(t.button_gap);
  actions_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  actions_.VerticalAlignment(xaml::VerticalAlignment::Bottom);
  actions_.Margin({0, 0, 0, t.action_bottom});
  add_ = Button(design::icons::kAdd, {.kind = ThemeEditKind::kAddPoint});
  remove_ = IconButton(context_.brushes, glyphs::kMinimize, L"", [this] {
    context_.delegate.OnThemeEdited({.kind = ThemeEditKind::kRemovePoint});
  });
  remove_->control.Width(t.button_size);
  remove_->control.Height(t.button_size);
  remove_->control.Margin(UniformThickness(0));
  remove_->glyph.Foreground(context_.brushes.editor_foreground);
  remove_->glyph.FontSize(t.icon_size);
  harmony_ =
      Button(design::icons::kAlgorithm, {.kind = ThemeEditKind::kCycleHarmony});
  for (const auto* button : {add_.get(), remove_.get(), harmony_.get()})
    actions_.Children().Append(button->control);
  board_.Children().Append(actions_);

  controls::Grid page;
  AddColumn(page, 0, xaml::GridUnitType::Auto);
  AddColumn(page, 1, xaml::GridUnitType::Star);
  AddColumn(page, 0, xaml::GridUnitType::Auto);
  previous_ = IconButton(context_.brushes, glyphs::kChevronLeft, L"", [this] {
    context_.delegate.OnThemeEdited({.kind = ThemeEditKind::kPreviousPage});
  });
  next_ = IconButton(context_.brushes, glyphs::kChevronRight, L"", [this] {
    context_.delegate.OnThemeEdited({.kind = ThemeEditKind::kNextPage});
  });
  for (auto* button : {previous_.get(), next_.get()}) {
    button->control.Width(t.page_button_size);
    button->control.Height(t.page_button_size);
    button->control.Margin(UniformThickness(0));
    button->glyph.FontSize(t.icon_size);
    button->glyph.Foreground(context_.brushes.editor_foreground);
    const auto resources = button->control.Resources();
    for (const auto* key :
         {L"ButtonBackground", L"ButtonBackgroundPointerOver",
          L"ButtonBackgroundPressed", L"ButtonBackgroundDisabled",
          L"ButtonBorderBrush", L"ButtonBorderBrushPointerOver",
          L"ButtonBorderBrushPressed", L"ButtonBorderBrushDisabled"})
      resources.Insert(winrt::box_value(key), context_.brushes.transparent);
  }
  page.Children().Append(previous_->control);
  controls::Grid::SetColumn(next_->control, 2);
  page.Children().Append(next_->control);
  presets_ = controls::Grid();
  presets_.Margin({t.padding, 0, t.padding, 0});
  controls::Grid::SetColumn(presets_, 1);
  page.Children().Append(presets_);
  for (std::size_t i = 0; i < swatches_.size(); ++i) {
    auto& swatch = swatches_[i];
    swatch.button = IconButton(context_.brushes, L"", L"", [this, i] {
      context_.delegate.OnThemeEdited(
          {.kind = ThemeEditKind::kPreset, .index = first_preset_ + i});
    });
    swatch.button->control.Width(t.swatch_size);
    swatch.button->control.Height(t.swatch_size);
    swatch.button->control.Margin(UniformThickness(0));
    swatch.button->control.CornerRadius(UniformCorners(t.swatch_size / 2));
    swatch.button->control.HorizontalAlignment(
        xaml::HorizontalAlignment::Center);
    controls::Grid tile;
    tile.Width(t.swatch_size);
    tile.Height(t.swatch_size);
    for (auto& layer : swatch.layers) {
      layer.CornerRadius(UniformCorners(t.swatch_size / 2));
      tile.Children().Append(layer);
    }
    swatch.layers[0].Background(swatch.base);
    for (std::size_t layer = 0; layer < 2; ++layer) {
      auto& brush = swatch.brushes[layer];
      brush.Center({static_cast<float>(layer), 0});
      brush.GradientOrigin({static_cast<float>(layer), 0});
      brush.RadiusX(std::sqrt(2.0));
      brush.RadiusY(std::sqrt(2.0));
      swatch.stops[layer * 2].Offset(0);
      swatch.stops[layer * 2 + 1].Offset(1);
      brush.GradientStops().Append(swatch.stops[layer * 2]);
      brush.GradientStops().Append(swatch.stops[layer * 2 + 1]);
      swatch.layers[layer + 1].Background(brush);
    }
    swatch.button->control.Content(tile);
    controls::Grid::SetColumn(swatch.button->control, static_cast<int>(i));
    presets_.Children().Append(swatch.button->control);
  }
  controls_card_ = controls::Border();
  controls_card_.CornerRadius(UniformCorners(t.controls_card_radius));
  controls_card_.BorderThickness(
      UniformThickness(t.controls_card_border_width));
  controls_card_.Padding(
      {t.controls_card_padding_inline, t.controls_card_padding_top,
       t.controls_card_padding_inline, t.controls_card_padding_bottom});
  controls_card_fill_ = media::LinearGradientBrush();
  controls_card_fill_.StartPoint({0, 0});
  controls_card_fill_.EndPoint({1, 1});
  for (std::size_t i = 0; i < controls_card_stops_.size(); ++i) {
    controls_card_stops_[i] = media::GradientStop();
    controls_card_stops_[i].Offset(static_cast<double>(i));
    controls_card_fill_.GradientStops().Append(controls_card_stops_[i]);
  }
  controls::StackPanel bottom;
  bottom.Spacing(t.row_gap);
  bottom.Children().Append(page);
  bottom.Children().Append(controls_->Root());
  controls_card_.Child(bottom);
  content_.Children().Append(controls_card_);
  const std::weak_ptr<int> weak = lifetime_;
  loaded_ = owner_.Loaded(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
    if (!weak.expired())
      OpenWhenLoaded();
  });
  canvas_loaded_ =
      canvas_->Root().Loaded(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (!weak.expired() && requested_open_ && popup_.IsOpen())
          canvas_->Root().Focus(xaml::FocusState::Programmatic);
      });
  owner_sized_ =
      owner_.SizeChanged(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (!weak.expired())
          OpenWhenLoaded();
      });
  closed_ = popup_.Closed(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
    if (weak.expired())
      return;
    const bool notify = std::exchange(requested_open_, false);
    island_.Hide();
    if (notify)
      context_.delegate.OnThemeEditorDismissed();
    context_.hover_changed();
  });
  key_ = surface_.KeyDown(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        using winrt::Windows::System::VirtualKey;
        using Keys = winrt::Windows::UI::Core::CoreVirtualKeyStates;
        const auto down = [](VirtualKey key) {
          return (winrt::Microsoft::UI::Input::InputKeyboardSource::
                      GetKeyStateForCurrentThread(key) &
                  Keys::Down) != Keys::None;
        };
        const bool primary = down(VirtualKey::Control) &&
                             !down(VirtualKey::Shift) &&
                             !down(VirtualKey::Menu);
        if (primary &&
            (args.Key() == VirtualKey::L || args.Key() == VirtualKey::T) &&
            context_.input.ForwardUnhandledKey(
                static_cast<std::uint32_t>(args.Key()))) {
          args.Handled(true);
          return;
        }
        if (args.Key() == VirtualKey::Escape)
          context_.delegate.OnThemeEditorDismissed();
        else if (primary && args.Key() == VirtualKey::L)
          context_.delegate.OnCommandBarRequested();
        else if (primary && args.Key() == VirtualKey::T)
          context_.delegate.OnNewTab();
        else
          return;
        args.Handled(true);
      });
  fallback_key_ = surface_.ProcessKeyboardAccelerators(
      winrt::auto_revoke,
      [this](auto&&,
             const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
        if (!args.Handled() && context_.input.ForwardUnhandledKey(
                                   static_cast<std::uint32_t>(args.Key())))
          args.Handled(true);
      });
}

void ThemeEditor::SetText(UiTextView text) {
  Accessible(surface_, NativeText(UiText::kThemeEditor, text));
  canvas_->SetText(text);
  controls_->SetText(text);
  const std::array names{UiText::kThemeSystem, UiText::kThemeLight,
                         UiText::kThemeDark};
  for (std::size_t i = 0; i < scheme_buttons_.size(); ++i)
    SetButtonText(*scheme_buttons_[i], NativeText(names[i], text));
  SetButtonText(*add_, NativeText(UiText::kThemeAdd, text));
  SetButtonText(*remove_, NativeText(UiText::kThemeRemove, text));
  SetButtonText(*harmony_, NativeText(UiText::kThemeHarmony, text));
  SetButtonText(*previous_, NativeText(UiText::kThemePrevious, text));
  SetButtonText(*next_, NativeText(UiText::kThemeNext, text));
  previous_->glyph.Text(winrt::hstring(
      text.right_to_left ? glyphs::kChevronRight : glyphs::kChevronLeft));
  next_->glyph.Text(winrt::hstring(text.right_to_left ? glyphs::kChevronLeft
                                                      : glyphs::kChevronRight));
  for (auto& swatch : swatches_)
    SetButtonText(*swatch.button, NativeText(UiText::kThemePreset, text));
  text_ready_ = true;
}

void ThemeEditor::Render(const ShellViewModel& vm) {
  const auto& editor = vm.theme_editor;
  if (!editor.open || !editor.value) {
    Close();
    return;
  }
  Build();
  const auto area =
      PlaceThemeEditor(vm.client, vm.layout.sidebar, vm.client.height,
                       vm.layout.sidebar_on_right);
  if (area.empty()) {
    context_.delegate.OnThemeEditorDismissed();
    Close();
    return;
  }
  requested_open_ = true;
  const auto theme =
      vm.theme.dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
  if (owner_.RequestedTheme() != theme)
    owner_.RequestedTheme(theme);
  const auto direction = vm.text.right_to_left
                             ? xaml::FlowDirection::RightToLeft
                             : xaml::FlowDirection::LeftToRight;
  if (surface_.FlowDirection() != direction)
    surface_.FlowDirection(direction);
  const auto& palette =
      vm.theme.dark ? kTokens.editor_dark : kTokens.editor_light;
  const bool high_contrast = context_.brushes.high_contrast();
  if (!high_contrast) {
    const std::array colors{palette.controls_card_top,
                            palette.controls_card_bottom};
    for (std::size_t i = 0; i < colors.size(); ++i) {
      const auto color = NativeColor(Color{colors[i]});
      if (controls_card_stops_[i].Color() != color)
        controls_card_stops_[i].Color(color);
    }
  }
  const media::Brush card_background =
      high_contrast ? media::Brush(context_.brushes.overlay)
                    : media::Brush(controls_card_fill_);
  const auto card_border =
      high_contrast ? context_.brushes.icon
                    : context_.brushes.For(Color{palette.controls_card_border});
  if (controls_card_.Background() != card_background)
    controls_card_.Background(card_background);
  if (controls_card_.BorderBrush() != card_border)
    controls_card_.BorderBrush(card_border);
  surface_.Background(context_.glass.For(surface_, GlassRole::kPanel,
                                         context_.brushes.overlay,
                                         kTokens.radius.overlay));
  context_.glass.Place(surface_, area);
  surface_.Width(area.width);
  scroll_.MaxHeight(
      std::max(0.0f, area.height - 2 * (kTokens.theme_editor.padding +
                                        kTokens.theme_editor.border_width)));
  popup_.HorizontalOffset(area.x - vm.client.x);
  popup_.VerticalOffset(area.y - vm.client.y);
  if (!text_ready_ || vm.changed.Has(Dirty::kText))
    SetText(vm.text);
  const auto geometry = MeasureThemeEditor(area.width);
  canvas_->Render(*editor.value, geometry.canvas_side);
  controls_->Render(*editor.value, geometry.stacked_controls);
  for (std::size_t i = 0; i < scheme_buttons_.size(); ++i)
    scheme_buttons_[i]->control.Background(
        i == static_cast<std::size_t>(editor.value->scheme)
            ? context_.brushes.editor_selection
            : context_.brushes.transparent);
  add_->control.IsEnabled(editor.can_add);
  remove_->control.IsEnabled(editor.can_remove);
  harmony_->control.IsEnabled(editor.can_harmonize);
  previous_->control.IsEnabled(editor.previous_page);
  next_->control.IsEnabled(editor.next_page);
  previous_->control.Opacity(
      editor.previous_page ? 1
                           : kTokens.theme_editor.page_button_disabled_opacity);
  next_->control.Opacity(
      editor.next_page ? 1 : kTokens.theme_editor.page_button_disabled_opacity);
  const auto columns = std::min(geometry.preset_columns, editor.presets.size());
  if (first_preset_ != editor.first_preset ||
      preset_count_ != editor.presets.size() || preset_columns_ != columns) {
    first_preset_ = editor.first_preset;
    preset_count_ = editor.presets.size();
    preset_columns_ = columns;
    presets_.ColumnDefinitions().Clear();
    presets_.RowDefinitions().Clear();
    for (std::size_t i = 0; i < columns; ++i)
      AddColumn(presets_, 1, xaml::GridUnitType::Star);
    if (columns)
      for (std::size_t i = 0; i < (preset_count_ + columns - 1) / columns; ++i)
        AddRow(presets_, 0, xaml::GridUnitType::Auto);
    presets_.RowSpacing(kTokens.theme_editor.button_gap);
    for (std::size_t i = 0; i < swatches_.size(); ++i) {
      auto& swatch = swatches_[i];
      swatch.button->control.Visibility(i < preset_count_
                                            ? xaml::Visibility::Visible
                                            : xaml::Visibility::Collapsed);
      if (i >= preset_count_)
        continue;
      controls::Grid::SetColumn(swatch.button->control,
                                static_cast<int>(i % columns));
      controls::Grid::SetRow(swatch.button->control,
                             static_cast<int>(i / columns));
      const auto& preset = editor.presets[i];
      WorkspaceTheme sample;
      (void)EditWorkspaceTheme(
          sample, {.kind = ThemeEditKind::kPreset, .index = first_preset_ + i});
      swatch.base.Color(NativeColor(Color{preset.color}));
      for (std::size_t j = 0; j < 2; ++j) {
        swatch.layers[j + 1].Visibility(sample.point_count == 3
                                            ? xaml::Visibility::Visible
                                            : xaml::Visibility::Collapsed);
        swatch.stops[j * 2].Color(NativeColor(sample.points[j].color));
        swatch.stops[j * 2 + 1].Color(
            NativeColor(sample.points[j].color.WithAlpha(0)));
      }
      if (sample.point_count == 3)
        swatch.base.Color(NativeColor(sample.points[2].color));
    }
  }
  island_.Show(vm.client, vm.scale_factor, theme);
  OpenWhenLoaded();
}

void ThemeEditor::OpenWhenLoaded() noexcept {
  try {
    if (!requested_open_ || !island_.IsReady())
      return;
    if (popup_.XamlRoot() != owner_.XamlRoot())
      popup_.XamlRoot(owner_.XamlRoot());
    if (!popup_.IsOpen()) {
      popup_.IsOpen(true);
      fade_.Play(content_, context_.motion, context_.brushes.high_contrast());
      canvas_->Root().Focus(xaml::FocusState::Programmatic);
      island_.Raise();
      context_.hover_changed();
    }
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError, "Theme editor could not open");
  }
}

void ThemeEditor::Close() noexcept {
  fade_.Stop();
  requested_open_ = false;
  if (canvas_)
    canvas_->CancelDrag();
  try {
    if (popup_)
      popup_.IsOpen(false);
    island_.Hide();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kError, "Theme editor could not close");
  }
}
}  // namespace kr::win
