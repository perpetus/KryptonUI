#include "onboarding_view.hpp"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/presentation/onboarding.hpp"
#include "krypton/ui/presentation/theme_editor.hpp"

namespace kr::win {
namespace composition = winrt::Microsoft::UI::Composition;
namespace hosting = xaml::Hosting;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;
using winrt::Windows::System::VirtualKey;

namespace {
const auto& Onboarding() noexcept {
  return kTokens.onboarding;
}
float Gap() noexcept {
  return OnboardingRem(Onboarding().card_gap_rem);
}
SpringSpec RiseSpring() noexcept {
  return {Onboarding().rise_spring_damping_ratio,
          Onboarding().rise_spring_period_ms};
}
SpringSpec WordSpring() noexcept {
  return {Onboarding().word_spring_damping_ratio,
          Onboarding().word_spring_period_ms};
}
SpringSpec ActionSpring() noexcept {
  return {Onboarding().action_spring_damping_ratio,
          Onboarding().action_spring_period_ms};
}
SpringSpec LandSpring() noexcept {
  return {Onboarding().land_spring_damping_ratio,
          Onboarding().land_spring_period_ms};
}
SpringSpec MorphSpring() noexcept {
  return {Onboarding().morph_spring_damping_ratio,
          Onboarding().morph_spring_period_ms};
}
SpringSpec PointSpring() noexcept {
  return {Onboarding().point_spring_damping_ratio,
          Onboarding().point_spring_period_ms};
}
composition::Compositor UiCompositor() {
  return xaml::Media::CompositionTarget::GetCompositorForCurrentThread();
}
// Absolute placement inside the client-sized root.
void PlaceAt(const xaml::FrameworkElement& element, Rect rect) {
  element.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  element.VerticalAlignment(xaml::VerticalAlignment::Top);
  element.Margin({rect.x, rect.y, 0, 0});
  element.Width(std::max(0.0f, rect.width));
  element.Height(std::max(0.0f, rect.height));
}
void Show(const xaml::UIElement& element, bool visible) {
  const auto value =
      visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
  if (element.Visibility() != value)
    element.Visibility(value);
}
[[nodiscard]] float Diagonal(Rect rect, Point from) noexcept {
  const float dx = std::max(from.x - rect.x, rect.right() - from.x);
  const float dy = std::max(from.y - rect.y, rect.bottom() - from.y);
  return std::hypot(dx, dy);
}
// The ghost's four regions; a hidden region collapses onto its edge.
std::array<Rect, 4> GhostRegions(const ShellLayout& layout, Rect client) {
  const auto edge = [&](Rect region, bool visible, bool vertical, bool right) {
    if (visible && !region.empty())
      return region;
    if (vertical)
      return Rect{right ? client.right() : client.x, client.y, 0,
                  client.height};
    return Rect{client.x, client.y, client.width, 0};
  };
  return {edge(layout.sidebar, layout.sidebar_visible, true,
               layout.sidebar_on_right),
          edge(layout.toolbar, layout.toolbar_visible, false, false),
          edge(layout.tab_strip, layout.tab_strip_visible, false, false),
          layout.card.empty() ? client : layout.card};
}
// Segoe Fluent Icons: Shield.
constexpr std::wstring_view kShield = L"";
}  // namespace

// ----------------------------------------------------------- backdrop

OnboardingBackdrop::OnboardingBackdrop() {
  root_.IsHitTestVisible(false);
  root_.FlowDirection(xaml::FlowDirection::LeftToRight);
  gradient_.MappingMode(media::BrushMappingMode::Absolute);
  for (auto& stop : stops_) {
    stop = media::GradientStop();
    gradient_.GradientStops().Append(stop);
  }
  stops_[1].Offset(1);
  root_.Children().Append(layers_.Root());
}

void OnboardingBackdrop::Render(const ThemeVm& theme,
                                Rect client,
                                Rect region,
                                const OnboardingStyle& style) {
  if (style.high_contrast()) {
    solid_.Color(NativeColor(style.card_color()));
    if (root_.Background() != solid_)
      root_.Background(solid_);
  } else {
    stops_[0].Color(NativeColor(theme.gradient_start));
    stops_[1].Color(NativeColor(theme.gradient_end));
    gradient_.StartPoint({client.x - region.x, client.y - region.y});
    gradient_.EndPoint({client.right() - region.x, client.bottom() - region.y});
    if (root_.Background() != gradient_)
      root_.Background(gradient_);
  }
  layers_.Render(theme, client, region, style.high_contrast());
}

// ----------------------------------------------------------- lifetime

OnboardingView::OnboardingView(HWND parent,
                               ViewContext& context,
                               NativeFrame& frame)
    : context_(context), frame_(frame), island_(parent) {}

OnboardingView::~OnboardingView() {
  lifetime_.reset();
  morph_batch_.Cancel();
  finish_batch_.Cancel();
  reveal_batch_.Cancel();
  content_batch_.Cancel();
  bloom_batch_.Cancel();
  warmup_batch_.Cancel();
  card_sized_.revoke();
  key_.revoke();
  pointer_moved_.revoke();
  pointer_exited_.revoke();
  field_pressed_.revoke();
  field_moved_.revoke();
  field_released_.revoke();
  field_lost_.revoke();
  field_key_.revoke();
  island_.Hide();
}

bool OnboardingView::Animate() const noexcept {
  return context_.motion.Enabled(style_.high_contrast());
}

std::optional<Rect> OnboardingView::CardBounds() const noexcept {
  if (!placed_ || card_rect_.empty())
    return std::nullopt;
  return card_rect_;
}

Point OnboardingView::CenterOf(const xaml::UIElement& element) const {
  try {
    const auto element_size = element.ActualSize();
    const auto point = element.TransformToVisual(card_).TransformPoint(
        {element_size.x / 2, element_size.y / 2});
    return {card_rect_.x + point.X, card_rect_.y + point.Y};
  } catch (...) {
    return {card_rect_.x + card_rect_.width / 2,
            card_rect_.y + card_rect_.height / 2};
  }
}

// ----------------------------------------------------------- building

void OnboardingView::Build() {
  if (root_)
    return;
  island_.Ensure();
  root_ = island_.Root();
  // The whole window belongs to onboarding: the chrome underneath is shown,
  // never operated. Window drag and caption hits still reach the host frame.
  root_.Background(style_.transparent);
  context_.brushes.Install(root_);
  if (auto attached = frame_.AttachIsland(island_.Window()); !attached)
    platform::WriteDefaultLog(LogLevel::kWarning, attached.error().message);
  moments_ = std::make_unique<OnboardingMoments>(
      context_, style_,
      [this](const xaml::UIElement& element) { return CenterOf(element); });

  page_ = controls::Border();
  page_layers_ = controls::Grid();
  page_.Child(page_layers_);
  page_layers_.Children().Append(page_backdrop_.Root());
  tint_ = controls::Border();
  tint_.IsHitTestVisible(false);
  tint_.Background(style_.field);
  page_layers_.Children().Append(tint_);
  Show(reveal_.Root(), false);
  page_layers_.Children().Append(reveal_.Root());
  grain_ = controls::Image();
  grain_.IsHitTestVisible(false);
  grain_.Stretch(media::Stretch::Fill);
  grain_.Opacity(Onboarding().grain_opacity);
  page_layers_.Children().Append(grain_);
  BuildField();
  page_layers_.Children().Append(field_host_);
  root_.Children().Append(page_);

  veil_ = controls::Grid();
  veil_.IsHitTestVisible(false);
  veil_base_ = controls::Border();
  veil_base_.Background(style_.neutral);
  veil_.Children().Append(veil_base_);
  veil_.Children().Append(veil_theme_.Root());
  root_.Children().Append(veil_);

  BuildGhost();
  root_.Children().Append(ghost_host_);
  BuildCard();
  root_.Children().Append(card_);
  BuildFlight();
  root_.Children().Append(flight_layer_);

  pointer_moved_ = root_.PointerMoved(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (step_ != OnboardingStep::kIgnition || finishing_ || client_.empty())
          return;
        const auto at = args.GetCurrentPoint(root_).Position();
        mark_.Tilt(
            {2 * at.X / client_.width - 1, 2 * at.Y / client_.height - 1},
            Animate());
      });
  pointer_exited_ =
      root_.PointerExited(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (step_ == OnboardingStep::kIgnition)
          mark_.Tilt({}, Animate());
      });

  key_ = root_.KeyDown(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        using Keys = winrt::Windows::UI::Core::CoreVirtualKeyStates;
        const bool alt = (winrt::Microsoft::UI::Input::InputKeyboardSource::
                              GetKeyStateForCurrentThread(VirtualKey::Menu) &
                          Keys::Down) != Keys::None;
        if (finishing_)
          return;
        if (args.Key() == VirtualKey::Escape)
          context_.delegate.OnOnboardingAction(OnboardingAction::kSkip);
        else if (alt && args.Key() == VirtualKey::Left)
          context_.delegate.OnOnboardingAction(OnboardingAction::kBack);
        else if (args.Key() == VirtualKey::Enter)
          Primary();
        else
          return;
        args.Handled(true);
      });
}

void OnboardingView::BuildField() {
  const auto& editor = kTokens.theme_editor;
  field_host_ = controls::ContentControl();
  field_host_.IsTabStop(true);
  field_host_.UseSystemFocusVisuals(true);
  field_host_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  field_host_.VerticalContentAlignment(xaml::VerticalAlignment::Stretch);
  field_ = controls::Grid();
  field_.Background(style_.transparent);
  dots_ = controls::Image();
  dots_.IsHitTestVisible(false);
  dots_.Stretch(media::Stretch::Fill);
  field_.Children().Append(dots_);
  handles_ = controls::Canvas();
  handles_.IsHitTestVisible(false);
  handles_.FlowDirection(xaml::FlowDirection::LeftToRight);
  field_.Children().Append(handles_);
  for (std::size_t i = 0; i < points_.size(); ++i) {
    auto& point = points_[i];
    point = xaml::Shapes::Ellipse();
    const float border = i ? editor.secondary_border : editor.primary_border;
    const float size =
        (i ? editor.secondary_size : editor.primary_size) + 2 * border;
    point.Width(size);
    point.Height(size);
    point.StrokeThickness(border);
    point.Stroke(context_.brushes.editor_point_border);
    point.Fill(point_colors_[i]);
    handles_.Children().Append(point);
  }
  field_host_.Content(field_);
  Show(field_host_, false);
  const auto send = [this](const xaml::Input::PointerRoutedEventArgs& args) {
    const auto at = args.GetCurrentPoint(root_).Position();
    context_.delegate.OnOnboardingColorPoint(
        {client_.x + at.X, client_.y + at.Y});
  };
  field_pressed_ = field_.PointerPressed(
      winrt::auto_revoke,
      [this, send](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (!args.GetCurrentPoint(field_).Properties().IsLeftButtonPressed())
          return;
        field_host_.Focus(xaml::FocusState::Pointer);
        dragging_ = field_.CapturePointer(args.Pointer());
        send(args);
        args.Handled(true);
      });
  field_moved_ = field_.PointerMoved(
      winrt::auto_revoke,
      [this, send](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (!dragging_)
          return;
        send(args);
        args.Handled(true);
      });
  field_released_ = field_.PointerReleased(
      winrt::auto_revoke,
      [this, send](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        if (!dragging_)
          return;
        send(args);
        dragging_ = false;
        field_.ReleasePointerCapture(args.Pointer());
        args.Handled(true);
      });
  field_lost_ = field_.PointerCaptureLost(
      winrt::auto_revoke, [this](auto&&, auto&&) { dragging_ = false; });
  field_key_ = field_host_.KeyDown(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
        Point p = color_point_;
        const float step = kTokens.theme_editor.keyboard_step;
        switch (args.Key()) {
          case VirtualKey::Left:
            p.x -= step;
            break;
          case VirtualKey::Right:
            p.x += step;
            break;
          case VirtualKey::Up:
            p.y -= step;
            break;
          case VirtualKey::Down:
            p.y += step;
            break;
          case VirtualKey::Home:
            p = {0.5f, 0.5f};
            break;
          default:
            return;
        }
        context_.delegate.OnOnboardingColorPoint(
            ColorFieldPosition(color_field_, ClampThemePoint(p)));
        args.Handled(true);
      });
}

void OnboardingView::BuildGhost() {
  const auto compositor = UiCompositor();
  ghost_host_ = controls::Grid();
  ghost_host_.IsHitTestVisible(false);
  ghost_ = compositor.CreateShapeVisual();
  ghost_.Opacity(0);
  ghost_fill_ = compositor.CreateColorBrush();
  ghost_stroke_ = compositor.CreateColorBrush();
  for (auto& geometry : ghost_shapes_) {
    geometry = compositor.CreateRoundedRectangleGeometry();
    geometry.CornerRadius({kTokens.radius.card, kTokens.radius.card});
    auto shape = compositor.CreateSpriteShape(geometry);
    shape.FillBrush(ghost_fill_);
    shape.StrokeBrush(ghost_stroke_);
    shape.StrokeThickness(Onboarding().ghost_stroke);
    ghost_.Shapes().Append(shape);
  }
  hosting::ElementCompositionPreview::SetElementChildVisual(ghost_host_,
                                                            ghost_);
  ghost_spring_ = compositor.CreateSpringVector2Animation();
  Configure(ghost_spring_, MorphSpring());
}

void OnboardingView::BuildCard() {
  const auto& t = Onboarding();
  const auto compositor = UiCompositor();
  card_ = controls::Grid();
  card_.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  card_.VerticalAlignment(xaml::VerticalAlignment::Top);
  AddRow(card_, 0, xaml::GridUnitType::Auto);
  AddRow(card_, 1, xaml::GridUnitType::Star);
  AddRow(card_, 0, xaml::GridUnitType::Auto);
  xaml::Automation::AutomationProperties::SetLandmarkType(
      card_, xaml::Automation::Peers::AutomationLandmarkType::Main);

  // The surface is a compositor shape, so its size can spring between
  // moments without animating XAML layout.
  surface_host_ = controls::Grid();
  surface_host_.IsHitTestVisible(false);
  controls::Grid::SetRowSpan(surface_host_, 3);
  surface_layer_ = compositor.CreateLayerVisual();
  surface_ = compositor.CreateShapeVisual();
  surface_shape_ = compositor.CreateRoundedRectangleGeometry();
  const float radius = OnboardingRem(t.card_radius_rem);
  surface_shape_.CornerRadius({radius, radius});
  surface_fill_ = compositor.CreateColorBrush();
  surface_stroke_ = compositor.CreateColorBrush();
  auto shape = compositor.CreateSpriteShape(surface_shape_);
  shape.FillBrush(surface_fill_);
  shape.StrokeBrush(surface_stroke_);
  shape.StrokeThickness(1);
  surface_.Shapes().Append(shape);
  surface_layer_.Children().InsertAtTop(surface_);
  auto shadow = compositor.CreateDropShadow();
  shadow.BlurRadius(t.card_shadow_blur);
  shadow.Offset({0, t.card_shadow_offset, 0});
  shadow.Color(NativeColor(Color{t.card_shadow}));
  surface_layer_.Shadow(shadow);
  surface_layer_.Opacity(0);
  hosting::ElementCompositionPreview::SetElementChildVisual(surface_host_,
                                                            surface_layer_);
  card_.Children().Append(surface_host_);
  surface_spring_ = compositor.CreateSpringVector2Animation();
  Configure(surface_spring_, MorphSpring());
  place_spring_ = compositor.CreateSpringVector3Animation();
  place_spring_.Target(L"Translation");
  Configure(place_spring_, MorphSpring());

  const float block = OnboardingRem(t.card_padding_block_rem);
  const float inline_padding = OnboardingRem(t.card_padding_inline_rem);
  header_ = controls::Grid();
  header_.Margin({inline_padding, block, inline_padding, 0});
  AddColumn(header_, 0, xaml::GridUnitType::Auto);
  AddColumn(header_, 1, xaml::GridUnitType::Star);
  AddColumn(header_, 0, xaml::GridUnitType::Auto);
  back_ = MakeOnboardingButton(style_, context_, false, [this] {
    context_.delegate.OnOnboardingAction(OnboardingAction::kBack);
  });
  back_->label.FontSize(OnboardingRem(t.back_rem));
  back_->control.Padding(UniformThickness(0));
  header_.Children().Append(back_->control);
  rail_ = controls::Grid();
  rail_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  rail_.VerticalAlignment(xaml::VerticalAlignment::Center);
  rail_dots_panel_ = controls::StackPanel();
  rail_dots_panel_.Orientation(controls::Orientation::Horizontal);
  rail_dots_panel_.Spacing(t.rail_gap);
  rail_.Children().Append(rail_dots_panel_);
  rail_indicator_ = controls::Border();
  rail_indicator_.Width(t.rail_active_width);
  rail_indicator_.Height(t.rail_dot);
  rail_indicator_.CornerRadius(UniformCorners(t.rail_dot / 2));
  rail_indicator_.Background(style_.accent);
  rail_indicator_.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  rail_.Children().Append(rail_indicator_);
  controls::Grid::SetColumn(rail_, 1);
  header_.Children().Append(rail_);
  capsule_ = controls::StackPanel();
  capsule_.Orientation(controls::Orientation::Horizontal);
  capsule_.Spacing(Gap() / 2);
  capsule_.VerticalAlignment(xaml::VerticalAlignment::Center);
  capsule_ring_ = controls::ProgressRing();
  capsule_ring_.IsActive(false);
  capsule_ring_.IsIndeterminate(false);
  capsule_ring_.Width(OnboardingRem(1));
  capsule_ring_.Height(OnboardingRem(1));
  capsule_ring_.Foreground(style_.accent);
  capsule_text_ = OnboardingText(style_, 1, false, true);
  capsule_text_.TextWrapping(xaml::TextWrapping::NoWrap);
  capsule_text_.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
  capsule_.Children().Append(capsule_ring_);
  capsule_.Children().Append(capsule_text_);
  controls::Grid::SetColumn(capsule_, 2);
  header_.Children().Append(capsule_);
  card_.Children().Append(header_);

  scroll_ = controls::ScrollViewer();
  scroll_.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
  scroll_.HorizontalScrollBarVisibility(
      controls::ScrollBarVisibility::Disabled);
  scroll_.Padding({inline_padding, block, inline_padding, block});
  content_ = controls::Grid();
  scroll_.Content(content_);
  controls::Grid::SetRow(scroll_, 1);
  card_.Children().Append(scroll_);

  actions_ = controls::StackPanel();
  actions_.Orientation(controls::Orientation::Horizontal);
  actions_.HorizontalAlignment(xaml::HorizontalAlignment::Right);
  actions_.Spacing(Gap() / 2);
  actions_.Margin({inline_padding, 0, inline_padding, block});
  skip_ = MakeOnboardingButton(style_, context_, false, [this] {
    if (secondary_declines_)
      context_.delegate.OnOnboardingDefaultChoice(false);
    else
      context_.delegate.OnOnboardingAction(OnboardingAction::kSkip);
  });
  next_ = MakeOnboardingButton(style_, context_, true, [this] { Primary(); });
  actions_.Children().Append(skip_->control);
  actions_.Children().Append(next_->control);
  controls::Grid::SetRow(actions_, 2);
  card_.Children().Append(actions_);

  BuildIgnition();
  content_.Children().Append(ignition_);
  const std::weak_ptr<int> weak = lifetime_;
  card_sized_ =
      card_.SizeChanged(winrt::auto_revoke, [this, weak](auto&&, auto&&) {
        if (weak.expired())
          return;
        const bool first = !placed_;
        SpringSurface(!first && Animate());
        Place(!first && Animate());
        LayoutWords();
        if (first && step_ == OnboardingStep::kIgnition)
          PlayIgnition();
      });
}

void OnboardingView::BuildIgnition() {
  const auto& t = Onboarding();
  ignition_ = controls::StackPanel();
  ignition_.Spacing(Gap());
  ignition_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  mark_.Root().HorizontalAlignment(xaml::HorizontalAlignment::Center);
  ignition_.Children().Append(mark_.Root());
  words_ = controls::Canvas();
  words_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  ignition_.Children().Append(words_);
  ignition_body_ = OnboardingText(style_, t.body_rem, false, true);
  ignition_body_.TextAlignment(xaml::TextAlignment::Center);
  ignition_body_.LineHeight(OnboardingRem(t.body_rem) * t.body_line_height);
  ignition_.Children().Append(ignition_body_);
  ignition_actions_ = controls::StackPanel();
  ignition_actions_.Spacing(Gap() / 2);
  ignition_actions_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  begin_ = MakeOnboardingButton(style_, context_, true, [this] {
    context_.delegate.OnOnboardingAction(OnboardingAction::kNext);
  });
  recommended_ = MakeOnboardingButton(style_, context_, false, [this] {
    context_.delegate.OnOnboardingAction(OnboardingAction::kRecommended);
  });
  ignition_skip_ = MakeOnboardingButton(style_, context_, false, [this] {
    context_.delegate.OnOnboardingAction(OnboardingAction::kSkip);
  });
  for (const auto* button :
       {begin_.get(), recommended_.get(), ignition_skip_.get()}) {
    button->control.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    ignition_actions_.Children().Append(button->control);
  }
  ignition_.Children().Append(ignition_actions_);
}

void OnboardingView::BuildFlight() {
  const auto& t = Onboarding();
  flight_layer_ = controls::Canvas();
  flight_layer_.IsHitTestVisible(false);
  flight_layer_.FlowDirection(xaml::FlowDirection::LeftToRight);
  const float size = t.icon_size + Gap();
  flight_ = controls::Border();
  flight_.Width(size);
  flight_.Height(size);
  flight_.CornerRadius(UniformCorners(t.icon_radius));
  flight_.Background(style_.tile);
  flight_.Opacity(0);
  flight_.CenterPoint({size / 2, size / 2, 0});
  controls::Grid chip;
  flight_icon_ = controls::Image();
  flight_icon_.Width(t.icon_size);
  flight_icon_.Height(t.icon_size);
  flight_glyph_ = Glyph(kShield, t.icon_size * 0.75);
  flight_glyph_.Foreground(style_.accent);
  flight_glyph_.HorizontalAlignment(xaml::HorizontalAlignment::Center);
  flight_glyph_.VerticalAlignment(xaml::VerticalAlignment::Center);
  chip.Children().Append(flight_icon_);
  chip.Children().Append(flight_glyph_);
  flight_.Child(chip);
  flight_layer_.Children().Append(flight_);
  ring_ = controls::Border();
  ring_.BorderBrush(style_.accent);
  ring_.BorderThickness(UniformThickness(t.ring_stroke));
  ring_.Opacity(0);
  flight_layer_.Children().Append(ring_);
}

// ----------------------------------------------------------- text

void OnboardingView::SetText(UiTextView text) {
  ignition_title_ = NativeText(UiText::kOnboardingIgnitionTitle, text);
  words_width_ = -1;  // relayout
  for (const auto& block : word_blocks_)
    block.Text(L"");
  std::size_t count = 0;
  const std::wstring_view title = ignition_title_;
  for (std::size_t at = 0; at < title.size();) {
    const auto start = title.find_first_not_of(L' ', at);
    if (start == std::wstring_view::npos)
      break;
    const auto end = std::min(title.find(L' ', start), title.size());
    if (count == word_blocks_.size()) {
      const auto& t = Onboarding();
      auto block = OnboardingText(style_, t.ignition_title_rem, true);
      block.TextWrapping(xaml::TextWrapping::NoWrap);
      block.CharacterSpacing(static_cast<std::int32_t>(t.ignition_tracking));
      block.LineHeight(OnboardingRem(t.ignition_title_rem) *
                       t.ignition_line_height);
      words_.Children().Append(block);
      word_blocks_.push_back(block);
    }
    word_blocks_[count++].Text(
        winrt::hstring(title.substr(start, end - start)));
    at = end;
  }
  for (std::size_t i = 0; i < word_blocks_.size(); ++i)
    Show(word_blocks_[i], i < count);
  word_count_ = count;
  // The title is read once, as a sentence, not word by word.
  xaml::Automation::AutomationProperties::SetName(words_, ignition_title_);
  ignition_body_.Text(NativeText(UiText::kOnboardingIgnitionBody, text));
  begin_->label.Text(NativeText(UiText::kOnboardingBegin, text));
  recommended_->label.Text(NativeText(UiText::kOnboardingRecommended, text));
  ignition_skip_->label.Text(NativeText(UiText::kOnboardingSkip, text));
  back_->label.Text(NativeText(UiText::kOnboardingBack, text));
  Accessible(card_, NativeText(UiText::kOnboardingBegin, text));
  Accessible(field_host_, NativeText(UiText::kOnboardingColorField, text));
  text_ready_ = true;
}

// Lays the title's words out like a centred paragraph, so each word can rise
// on its own without splitting shaped text.
void OnboardingView::LayoutWords() {
  if (!words_ || word_count_ == 0)
    return;
  const float available = std::max(
      0.0f, static_cast<float>(card_.ActualWidth()) -
                2 * OnboardingRem(Onboarding().card_padding_inline_rem));
  if (available <= 0 || available == words_width_)
    return;
  words_width_ = available;
  const float space = OnboardingRem(Onboarding().ignition_title_rem) / 4;
  struct Placed {
    float width, height;
  };
  std::vector<Placed> sizes;
  sizes.reserve(word_count_);
  for (std::size_t i = 0; i < word_count_; ++i) {
    auto& block = word_blocks_[i];
    block.MaxWidth(available);
    block.TextWrapping(xaml::TextWrapping::Wrap);
    block.Measure({available, std::numeric_limits<float>::infinity()});
    const auto desired = block.DesiredSize();
    sizes.push_back({desired.Width, desired.Height});
  }
  float y = 0, width = 0;
  for (std::size_t first = 0; first < word_count_;) {
    std::size_t last = first;
    float line = sizes[first].width, height = sizes[first].height;
    while (last + 1 < word_count_ &&
           line + space + sizes[last + 1].width <= available) {
      ++last;
      line += space + sizes[last].width;
      height = std::max(height, sizes[last].height);
    }
    float x = (available - line) / 2;
    for (std::size_t i = first; i <= last; ++i) {
      controls::Canvas::SetLeft(word_blocks_[i], x);
      controls::Canvas::SetTop(word_blocks_[i], y);
      x += sizes[i].width + space;
    }
    width = std::max(width, line);
    y += height;
    first = last + 1;
  }
  words_.Width(available);
  words_.Height(y);
}

// ----------------------------------------------------------- render

void OnboardingView::Render(const ShellViewModel& vm) {
  const auto& onboarding = vm.onboarding;
  if (!onboarding.open)
    return;
  Build();
  text_ = vm.text;
  const bool first = open_serial_ != onboarding.open_serial;
  client_ = vm.client;
  layout_ = vm.layout;
  scale_ = vm.scale_factor;
  const bool rtl = vm.text.right_to_left;
  if (first || vm.changed.HasAny(Dirty::kTheme)) {
    style_.Apply(vm.theme, context_.brushes);
    surface_fill_.Color(NativeColor(style_.card_color()));
    surface_stroke_.Color(NativeColor(style_.border_color()));
    ghost_fill_.Color(NativeColor(style_.accent_color().WithAlpha(
        static_cast<std::uint8_t>(Onboarding().ghost_fill_opacity * 255))));
    ghost_stroke_.Color(NativeColor(style_.accent_color()));
  }
  if (first || !text_ready_ || vm.changed.Has(Dirty::kText) ||
      right_to_left_ != rtl) {
    right_to_left_ = rtl;
    card_.FlowDirection(rtl ? xaml::FlowDirection::RightToLeft
                            : xaml::FlowDirection::LeftToRight);
    SetText(vm.text);
  }
  const auto& t = Onboarding();
  const float margin = OnboardingRem(t.card_margin_rem);
  const float width = std::min(OnboardingRem(t.card_max_width_rem) +
                                   2 * OnboardingRem(t.card_padding_inline_rem),
                               std::max(0.0f, client_.width - 2 * margin));
  const float ignition_width =
      std::clamp(client_.width * t.ignition_max_width, width,
                 std::max(width, client_.width - 2 * margin));
  card_.Width(onboarding.step == OnboardingStep::kIgnition ? ignition_width
                                                           : width);
  card_.MaxHeight(std::max(0.0f, client_.height - 2 * margin));

  RenderBackdrops(vm);
  moments_->Render(vm, first || vm.changed.Has(Dirty::kText));
  RenderHeader(onboarding);
  RenderActions(onboarding);
  RenderField(onboarding);
  RenderGhost(vm);
  Reveal(vm);
  // Answers leave from the moment that sent them, so fly before switching.
  if (onboarding.flight.serial != flight_serial_)
    Fly(vm);
  if (first) {
    open_serial_ = onboarding.open_serial;
    step_serial_ = onboarding.step_serial;
    ShowMoment(onboarding, true);
  } else if (onboarding.step_serial != step_serial_) {
    step_serial_ = onboarding.step_serial;
    ShowMoment(onboarding, false);
  }
  if (onboarding.finishing && !finishing_)
    Finish(onboarding);
  const auto theme =
      vm.theme.dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
  island_.Show(vm.client, vm.scale_factor, theme);
  if (placed_ && vm.changed.HasAny(Dirty::kLayout | Dirty::kWindow))
    Place(Animate());
  text_ = {};
}

void OnboardingView::RenderBackdrops(const ShellViewModel& vm) {
  const auto& onboarding = vm.onboarding;
  // Keep the newest theme; the page holds the old one while a reveal plays.
  theme_ = vm.theme;
  if (vm.theme.workspace_theme) {
    theme_value_ = *vm.theme.workspace_theme;
    theme_.workspace_theme = &*theme_value_;
  }
  const Rect page = layout_.card.empty() ? client_ : layout_.card;
  PlaceAt(page_, page);
  page_.CornerRadius(UniformCorners(layout_.card_radius));
  Show(grain_, !style_.high_contrast());
  const bool starting_reveal = Animate() && onboarding.reveal.serial != 0 &&
                               onboarding.reveal.serial != reveal_serial_;
  if (!revealing_ && !starting_reveal)
    page_backdrop_.Render(theme_, client_, page, style_);
  if (revealing_)
    reveal_.Render(theme_, client_, page, style_);
  veil_theme_.Render(theme_, client_, client_, style_);
  PlaceAt(veil_, client_);
  const auto grain =
      grain_pattern_.Grain(static_cast<int>(std::ceil(page.width)),
                           static_cast<int>(std::ceil(page.height)),
                           {page.x - client_.x, page.y - client_.y});
  if (grain_.Source() != grain)
    grain_.Source(grain);
  // The page clears to pure colour while it is the colour field.
  const bool painting = onboarding.step == OnboardingStep::kColor;
  if (Animate() && !tint_.OpacityTransition()) {
    xaml::ScalarTransition transition;
    transition.Duration(std::chrono::duration_cast<foundation::TimeSpan>(
        std::chrono::duration<float, std::milli>(
            Onboarding().content_fade_ms)));
    tint_.OpacityTransition(transition);
  } else if (!Animate() && tint_.OpacityTransition()) {
    tint_.OpacityTransition(nullptr);
  }
  tint_.Opacity(painting ? 0 : 1);
  ghost_.Size({client_.width, client_.height});
  surface_.Size({client_.width, client_.height});
  surface_layer_.Size({client_.width, client_.height});
}

void OnboardingView::RenderHeader(const OnboardingVm& vm) {
  const auto& t = Onboarding();
  const bool bookend = vm.step == OnboardingStep::kIgnition;
  Show(header_, !bookend);
  Show(back_->control, vm.can_go_back);
  while (rail_dots_.size() < vm.rail.size()) {
    controls::Border dot;
    dot.Width(t.rail_dot);
    dot.Height(t.rail_dot);
    dot.CornerRadius(UniformCorners(t.rail_dot / 2));
    dot.Background(style_.border);
    rail_dots_panel_.Children().Append(dot);
    rail_dots_.push_back(dot);
  }
  for (std::size_t i = 0; i < rail_dots_.size(); ++i)
    Show(rail_dots_[i], i < vm.rail.size());
  const bool on_rail = vm.rail_index < vm.rail.size();
  Show(rail_indicator_, on_rail);
  if (on_rail) {
    const float x =
        static_cast<float>(vm.rail_index) * (t.rail_dot + t.rail_gap) -
        (t.rail_active_width - t.rail_dot) / 2;
    if (Animate() && rail_placed_) {
      if (!rail_spring_) {
        rail_spring_ =
            CompositorFor(rail_indicator_).CreateSpringVector3Animation();
        rail_spring_.Target(L"Translation");
        Configure(rail_spring_, PointSpring());
      }
      rail_spring_.FinalValue(float3{x, 0, 0});
      rail_indicator_.StartAnimation(rail_spring_);
    } else {
      rail_indicator_.Translation({x, 0, 0});
    }
    rail_placed_ = true;
  }
  const bool importing = !vm.import_status.empty();
  Show(capsule_, importing);
  const bool running = importing && vm.import.fraction >= 0 &&
                       vm.import.phase == ImportPhase::kRunning;
  if (capsule_ring_.IsActive() != running)
    capsule_ring_.IsActive(running);
  if (importing) {
    capsule_text_.Text(winrt::to_hstring(vm.import_status));
    const bool known = vm.import.fraction >= 0;
    Show(capsule_ring_, known && vm.import.phase == ImportPhase::kRunning);
    if (known)
      capsule_ring_.Value(vm.import.fraction * 100);
  }
}

void OnboardingView::RenderActions(const OnboardingVm& vm) {
  const bool ignition = vm.step == OnboardingStep::kIgnition;
  Show(actions_, !ignition);
  if (ignition)
    return;
  const auto flags = vm.default_browser;
  const bool can_request = flags.Has(DefaultBrowserFlag::kCanSet) &&
                           !flags.Has(DefaultBrowserFlag::kIsDefault) &&
                           !flags.Has(DefaultBrowserFlag::kManaged) &&
                           !vm.default_requested;
  primary_requests_default_ =
      vm.step == OnboardingStep::kDefaultBrowser && can_request;
  secondary_declines_ = primary_requests_default_;
  primary_finishes_ = vm.step == OnboardingStep::kReady;
  const UiText primary = primary_finishes_ ? UiText::kOnboardingStart
                         : primary_requests_default_
                             ? UiText::kOnboardingMakeDefault
                             : UiText::kOnboardingNext;
  const UiText secondary =
      secondary_declines_ ? UiText::kOnboardingNotNow : UiText::kOnboardingSkip;
  next_->label.Text(NativeText(primary, text_));
  skip_->label.Text(NativeText(secondary, text_));
  Show(skip_->control, !primary_finishes_);
  next_->control.IsEnabled(!vm.finishing);
  skip_->control.IsEnabled(!vm.finishing);
}

void OnboardingView::Primary() {
  if (finishing_)
    return;
  if (step_ == OnboardingStep::kIgnition)
    context_.delegate.OnOnboardingAction(OnboardingAction::kNext);
  else if (primary_requests_default_)
    context_.delegate.OnOnboardingDefaultChoice(true);
  else if (primary_finishes_)
    context_.delegate.OnOnboardingAction(OnboardingAction::kFinish);
  else
    context_.delegate.OnOnboardingAction(OnboardingAction::kNext);
}

void OnboardingView::RenderField(const OnboardingVm& vm) {
  const bool painting = vm.step == OnboardingStep::kColor && !vm.finishing;
  Show(field_host_, painting);
  if (!painting)
    return;
  color_field_ = vm.color_field;
  const Rect page = layout_.card.empty() ? client_ : layout_.card;
  const auto dots = dot_pattern_.Dots(
      static_cast<int>(std::ceil(page.width)),
      static_cast<int>(std::ceil(page.height)),
      Color::FromArgb(context_.brushes.editor_dots.Color().A,
                      context_.brushes.editor_dots.Color().R,
                      context_.brushes.editor_dots.Color().G,
                      context_.brushes.editor_dots.Color().B));
  if (dots_.Source() != dots)
    dots_.Source(dots);
  const auto* theme = vm.theme;
  const std::uint32_t count = theme ? theme->point_count : 0;
  color_point_ = count ? theme->points[0].position : Point{0.5f, 0.5f};
  for (std::size_t i = 0; i < points_.size(); ++i) {
    auto& point = points_[i];
    Show(point, i < count);
    if (i >= count)
      continue;
    point_colors_[i].Color(NativeColor(theme->points[i].color));
    const auto at =
        ColorFieldPosition(vm.color_field, theme->points[i].position);
    const float3 target{at.x - page.x - static_cast<float>(point.Width()) / 2,
                        at.y - page.y - static_cast<float>(point.Height()) / 2,
                        0};
    // Jumps (presets, keys, harmonies) spring; a drag follows the pointer.
    if (Animate() && !dragging_ && points_placed_) {
      if (!point_spring_) {
        point_spring_ = CompositorFor(point).CreateSpringVector3Animation();
        point_spring_.Target(L"Translation");
        Configure(point_spring_, PointSpring());
      }
      point_spring_.FinalValue(target);
      point.StartAnimation(point_spring_);
    } else {
      if (point_spring_)
        point.StopAnimation(point_spring_);
      point.Translation(target);
    }
  }
  points_placed_ = true;
}

void OnboardingView::RenderGhost(const ShellViewModel& vm) {
  const auto& onboarding = vm.onboarding;
  const bool want = onboarding.ghost && !onboarding.finishing;
  const bool new_morph =
      onboarding.chrome_veiled && onboarding.morph_serial != morph_serial_;
  const auto compositor = UiCompositor();
  if (new_morph) {
    morph_serial_ = onboarding.morph_serial;
    morph_batch_.Begin(compositor);
  }
  const auto targets =
      GhostRegions(want ? onboarding.ghost_layout : vm.layout, client_);
  if (want && !ghost_shown_) {
    // Start from the real window, then grow into the candidate.
    const auto from = GhostRegions(vm.layout, client_);
    for (std::size_t i = 0; i < ghost_shapes_.size(); ++i) {
      ghost_shapes_[i].Offset({from[i].x, from[i].y});
      ghost_shapes_[i].Size({from[i].width, from[i].height});
    }
  }
  if (want) {
    for (std::size_t i = 0; i < ghost_shapes_.size(); ++i) {
      auto& shape = ghost_shapes_[i];
      const float2 offset{targets[i].x, targets[i].y};
      const float2 size{targets[i].width, targets[i].height};
      if (Animate()) {
        ghost_spring_.FinalValue(offset);
        shape.StartAnimation(L"Offset", ghost_spring_);
        ghost_spring_.FinalValue(size);
        shape.StartAnimation(L"Size", ghost_spring_);
      } else {
        shape.Offset(offset);
        shape.Size(size);
      }
    }
  }
  if (want != ghost_shown_) {
    ghost_shown_ = want;
    if (Animate()) {
      auto fade = compositor.CreateScalarKeyFrameAnimation();
      fade.InsertKeyFrame(1, want ? 1.0f : 0.0f);
      fade.Duration(Ms(kTokens.motion.fast_ms));
      ghost_.StartAnimation(L"Opacity", fade);
    } else {
      ghost_.Opacity(want ? 1.0f : 0.0f);
    }
  }
  const bool veiled =
      onboarding.step == OnboardingStep::kIgnition || onboarding.chrome_veiled;
  if (veiled != veiled_ && onboarding.step != OnboardingStep::kIgnition &&
      step_ != OnboardingStep::kIgnition)
    Veil(veiled, kTokens.motion.normal_ms);
  if (new_morph) {
    const auto serial = morph_serial_;
    morph_batch_.End(
        [this, serial] { context_.delegate.OnOnboardingMorphSettled(serial); });
  }
}

void OnboardingView::Veil(bool on, float duration_ms, float delay_ms) {
  veiled_ = on;
  if (!Animate() || duration_ms <= 0) {
    veil_.Opacity(on ? 1 : 0);
    return;
  }
  const auto compositor = CompositorFor(veil_);
  auto fade = compositor.CreateScalarKeyFrameAnimation();
  fade.Target(L"Opacity");
  fade.InsertExpressionKeyFrame(0, L"this.StartingValue");
  fade.InsertKeyFrame(1, on ? 1.0f : 0.0f,
                      Bezier(compositor, Onboarding().ease_out));
  fade.Duration(Ms(duration_ms));
  fade.DelayTime(Ms(delay_ms));
  veil_.StartAnimation(fade);
}

void OnboardingView::Reveal(const ShellViewModel& vm) {
  const auto& reveal = vm.onboarding.reveal;
  if (reveal.serial == reveal_serial_)
    return;
  reveal_serial_ = reveal.serial;
  const Rect page = layout_.card.empty() ? client_ : layout_.card;
  if (!Animate() || reveal.serial == 0) {
    reveal_batch_.Cancel();
    revealing_ = false;
    Show(reveal_.Root(), false);
    page_backdrop_.Render(theme_, client_, page, style_);
    return;
  }
  // The new colour spreads from where it was chosen; the page keeps the old
  // one underneath until the circle covers it.
  revealing_ = true;
  Show(reveal_.Root(), true);
  reveal_.Render(theme_, client_, page, style_);
  const auto compositor = UiCompositor();
  const auto visual =
      hosting::ElementCompositionPreview::GetElementVisual(reveal_.Root());
  if (!reveal_shape_) {
    reveal_shape_ = compositor.CreateEllipseGeometry();
    visual.Clip(compositor.CreateGeometricClip(reveal_shape_));
  }
  const float2 center{reveal.origin.x - page.x, reveal.origin.y - page.y};
  const float radius =
      Diagonal({0, 0, page.width, page.height}, {center.x, center.y});
  reveal_shape_.Center(center);
  auto grow = compositor.CreateVector2KeyFrameAnimation();
  grow.InsertKeyFrame(0, {0, 0});
  grow.InsertKeyFrame(1, {radius, radius},
                      Bezier(compositor, kTokens.icon_packs.bezier));
  grow.Duration(Ms(kTokens.motion.slow_ms));
  reveal_batch_.Begin(compositor);
  reveal_shape_.StartAnimation(L"Radius", grow);
  reveal_batch_.End([this] {
    revealing_ = false;
    const Rect current = layout_.card.empty() ? client_ : layout_.card;
    page_backdrop_.Render(theme_, client_, current, style_);
    Show(reveal_.Root(), false);
  });
}

void OnboardingView::Fly(const ShellViewModel& vm) {
  const auto& onboarding = vm.onboarding;
  flight_serial_ = onboarding.flight.serial;
  if (!Animate() || onboarding.flight.serial == 0)
    return;
  const OnboardingLanding* landing = nullptr;
  for (const auto& item : onboarding.landings)
    if (item.element == onboarding.flight.target && item.visible)
      landing = &item;
  if (!landing)
    return;
  const auto source_element = moments_->Source(step_);
  const Point from = source_element
                         ? CenterOf(source_element)
                         : Point{card_rect_.x + card_rect_.width / 2,
                                 card_rect_.y + card_rect_.height / 2};
  const Rect target = landing->bounds;
  const Point to{target.x + target.width / 2, target.y + target.height / 2};
  const bool has_icon = static_cast<bool>(onboarding.flight.icon);
  flight_icon_.Source(
      has_icon ? context_.images.Get(onboarding.flight.icon, vm.images)
               : nullptr);
  Show(flight_icon_, has_icon);
  Show(flight_glyph_, !has_icon);
  const float size = static_cast<float>(flight_.Width());
  controls::Canvas::SetLeft(flight_, from.x - client_.x - size / 2);
  controls::Canvas::SetTop(flight_, from.y - client_.y - size / 2);
  const auto compositor = UiCompositor();
  const auto& t = Onboarding();
  auto travel = compositor.CreateSpringVector3Animation();
  travel.Target(L"Translation");
  Configure(travel, LandSpring());
  travel.InitialValue(float3{0, 0, 0});
  travel.FinalValue(float3{to.x - from.x, to.y - from.y, 0});
  auto shrink = compositor.CreateSpringVector3Animation();
  shrink.Target(L"Scale");
  Configure(shrink, LandSpring());
  const float landed =
      std::clamp(std::min(target.width, target.height) / size, 0.3f, 1.0f);
  shrink.InitialValue(float3{1, 1, 1});
  shrink.FinalValue(float3{landed, landed, 1});
  auto appear = compositor.CreateScalarKeyFrameAnimation();
  appear.Target(L"Opacity");
  appear.InsertKeyFrame(0, 1);
  appear.InsertKeyFrame(0.7f, 1);
  appear.InsertKeyFrame(1, 0);
  appear.Duration(Ms(t.land_spring_period_ms + kTokens.motion.fast_ms));
  flight_.StartAnimation(travel);
  flight_.StartAnimation(shrink);
  flight_.StartAnimation(appear);
  // A ring settles where the answer landed.
  const float ring = std::max(1.0f, std::min(target.width, target.height));
  ring_.Width(ring);
  ring_.Height(ring);
  ring_.CornerRadius(UniformCorners(ring / 2));
  ring_.CenterPoint({ring / 2, ring / 2, 0});
  controls::Canvas::SetLeft(ring_, to.x - client_.x - ring / 2);
  controls::Canvas::SetTop(ring_, to.y - client_.y - ring / 2);
  auto spread = compositor.CreateVector3KeyFrameAnimation();
  spread.Target(L"Scale");
  spread.InsertKeyFrame(0, {1, 1, 1});
  spread.InsertKeyFrame(1, {t.ring_scale, t.ring_scale, 1},
                        Bezier(compositor, t.ease_out));
  spread.Duration(Ms(kTokens.motion.slow_ms));
  spread.DelayTime(Ms(t.land_spring_period_ms * 0.6f));
  spread.DelayBehavior(
      composition::AnimationDelayBehavior::SetInitialValueBeforeDelay);
  auto fade = compositor.CreateScalarKeyFrameAnimation();
  fade.Target(L"Opacity");
  fade.InsertKeyFrame(0, 1);
  fade.InsertKeyFrame(1, 0, Bezier(compositor, t.ease_out));
  fade.Duration(Ms(kTokens.motion.slow_ms));
  fade.DelayTime(Ms(t.land_spring_period_ms * 0.6f));
  fade.DelayBehavior(
      composition::AnimationDelayBehavior::SetInitialValueBeforeDelay);
  ring_.StartAnimation(spread);
  ring_.StartAnimation(fade);
}

// ----------------------------------------------------------- moments

void OnboardingView::ShowMoment(const OnboardingVm& vm, bool first) {
  const auto from = step_;
  step_ = vm.step;
  const xaml::FrameworkElement next = vm.step == OnboardingStep::kIgnition
                                          ? xaml::FrameworkElement(ignition_)
                                          : moments_->Panel(vm.step);
  std::uint32_t index = 0;
  if (!content_.Children().IndexOf(next, index))
    content_.Children().Append(next);
  Show(next, true);
  const auto previous = std::exchange(shown_, next);
  const bool animate = Animate() && !first;
  const auto& t = Onboarding();
  const float direction =
      static_cast<float>(vm.direction) * (right_to_left_ ? -1.0f : 1.0f);
  const bool leaving_ignition =
      from == OnboardingStep::kIgnition && vm.step != OnboardingStep::kIgnition;
  const bool entering_ignition =
      from != OnboardingStep::kIgnition && vm.step == OnboardingStep::kIgnition;
  float delay = 0;
  content_batch_.Begin(CompositorFor(content_));
  if (previous && previous != next) {
    if (animate && leaving_ignition) {
      // Zen's start stage: everything lifts away together, then the card.
      std::vector<xaml::UIElement> parts{mark_.Root()};
      for (std::size_t i = 0; i < word_count_; ++i)
        parts.push_back(word_blocks_[i]);
      parts.push_back(ignition_body_);
      parts.push_back(ignition_actions_);
      choreography_.Recede(parts, {.duration_ms = t.exit_ms,
                                   .stagger_ms = t.exit_stagger_ms,
                                   .easing = t.ease_in,
                                   .to = {0, t.exit_rise, 0}});
      delay = t.exit_ms + t.exit_stagger_ms * static_cast<float>(parts.size());
    } else if (animate) {
      const std::array<xaml::UIElement, 1> leaving{previous};
      choreography_.Recede(leaving,
                           {.duration_ms = t.recede_ms,
                            .easing = t.ease_in,
                            .to = {-t.recede_shift * direction, 0, 0}});
    }
  }
  content_batch_.End([this, previous, next] {
    if (previous && previous != next && previous != shown_) {
      Show(previous, false);
      std::array parts{xaml::UIElement(previous)};
      Choreography::Snap(parts, true);
    }
  });
  if (leaving_ignition) {
    Veil(false, animate ? t.card_fade_ms : 0, delay);
    FadeSurface(true, animate ? t.card_fade_ms : 0, delay);
  } else if (entering_ignition) {
    Veil(true, animate ? t.card_fade_ms : 0);
    FadeSurface(false, animate ? t.card_fade_ms : 0);
  } else if (first) {
    Veil(vm.step == OnboardingStep::kIgnition, 0);
    FadeSurface(vm.step != OnboardingStep::kIgnition, 0);
  }
  warmup_batch_.Begin(CompositorFor(content_));
  if (vm.step == OnboardingStep::kIgnition) {
    if (!first)
      PlayIgnition();
  } else {
    const auto parts = moments_->Parts(vm.step);
    if (animate && parts.size() == 3) {
      content_motion_.Rise(parts.first(2),
                           {.spring = RiseSpring(),
                            .delay_ms = delay,
                            .stagger_ms = t.rise_stagger_ms,
                            .from = {t.rise_shift * direction, 0, 0}});
      content_motion_.Rise(
          parts.subspan(2),
          {.spring = RiseSpring(), .delay_ms = delay + 2 * t.rise_stagger_ms});
    } else {
      Choreography::Snap(parts, true);
    }
  }
  auto upcoming = OnboardingStep::kReady;
  for (const auto step : vm.rail)
    if (step > vm.step) {
      upcoming = step;
      break;
    }
  const auto serial = step_serial_;
  const std::weak_ptr<int> weak = lifetime_;
  warmup_batch_.End([this, weak, upcoming, serial] {
    if (weak.expired())
      return;
    root_.DispatcherQueue().TryEnqueue([this, weak, upcoming, serial] {
      if (!weak.expired() && !finishing_ && serial == step_serial_)
        (void)moments_->Panel(upcoming);
    });
  });
  if (!first || vm.step != OnboardingStep::kIgnition)
    Announce(vm);
  FocusMoment(vm.step);
}

void OnboardingView::FadeSurface(bool on, float duration_ms, float delay_ms) {
  if (duration_ms <= 0 || !Animate()) {
    surface_layer_.Opacity(on ? 1.0f : 0.0f);
    return;
  }
  const auto compositor = UiCompositor();
  auto fade = compositor.CreateScalarKeyFrameAnimation();
  fade.InsertExpressionKeyFrame(0, L"this.StartingValue");
  fade.InsertKeyFrame(1, on ? 1.0f : 0.0f,
                      Bezier(compositor, Onboarding().ease_out));
  fade.Duration(Ms(duration_ms));
  fade.DelayTime(Ms(delay_ms));
  surface_layer_.StartAnimation(L"Opacity", fade);
}

void OnboardingView::PlayIgnition() {
  const auto& t = Onboarding();
  std::vector<xaml::UIElement> words;
  for (std::size_t i = 0; i < word_count_; ++i)
    words.push_back(word_blocks_[i]);
  std::array<xaml::UIElement, 2> rest{ignition_body_, ignition_actions_};
  std::array<xaml::UIElement, 1> mark{mark_.Root()};
  Choreography::Snap(mark, true);
  if (!Animate()) {
    mark_.Draw(false);
    Choreography::Snap(words, true);
    Choreography::Snap(rest, true);
    veil_theme_.Root().Opacity(1);
    return;
  }
  mark_.Reset();
  mark_.Draw(true);
  choreography_.Rise(words, {.spring = WordSpring(),
                             .delay_ms = t.word_delay_ms,
                             .stagger_ms = t.word_stagger_ms,
                             .from = {0, t.word_rise, 0}});
  choreography_.Rise(
      rest, {.spring = ActionSpring(),
             .delay_ms = t.word_delay_ms +
                         t.word_stagger_ms * static_cast<float>(words.size()) +
                         t.action_delay_ms,
             .stagger_ms = t.rise_stagger_ms,
             .from = {0, t.action_rise, 0}});
  // The theme blooms out of the mark as its strokes finish.
  const auto compositor = UiCompositor();
  const auto visual =
      hosting::ElementCompositionPreview::GetElementVisual(veil_theme_.Root());
  if (!bloom_) {
    bloom_ = compositor.CreateEllipseGeometry();
    visual.Clip(compositor.CreateGeometricClip(bloom_));
  }
  const Point origin = CenterOf(mark_.Root());
  const float2 center{origin.x - client_.x, origin.y - client_.y};
  const float radius =
      Diagonal({0, 0, client_.width, client_.height}, {center.x, center.y});
  bloom_.Center(center);
  auto grow = compositor.CreateVector2KeyFrameAnimation();
  grow.InsertKeyFrame(0, {0, 0});
  grow.InsertKeyFrame(1, {radius, radius}, Bezier(compositor, t.ease_out));
  grow.Duration(Ms(t.bloom_ms));
  grow.DelayTime(Ms(t.word_delay_ms));
  grow.DelayBehavior(
      composition::AnimationDelayBehavior::SetInitialValueBeforeDelay);
  bloom_batch_.Begin(compositor);
  bloom_.StartAnimation(L"Radius", grow);
  bloom_batch_.End([this, visual] {
    // A resize may outgrow the circle; once bloomed, nothing is clipped.
    visual.Clip(nullptr);
    bloom_ = nullptr;
  });
}

void OnboardingView::Place(bool animate) {
  const auto rect = PlaceOnboardingCard(
      client_, layout_, step_, static_cast<float>(card_.ActualWidth()),
      static_cast<float>(card_.ActualHeight()));
  card_rect_ = rect;
  const float3 target{rect.x - client_.x, rect.y - client_.y, 0};
  if (animate && placed_) {
    place_spring_.FinalValue(target);
    card_.StartAnimation(place_spring_);
  } else {
    card_.StopAnimation(place_spring_);
    card_.Translation(target);
  }
  placed_ = true;
}

void OnboardingView::SpringSurface(bool animate) {
  if (finishing_)
    return;
  const float2 size{static_cast<float>(card_.ActualWidth()),
                    static_cast<float>(card_.ActualHeight())};
  if (animate) {
    surface_spring_.FinalValue(size);
    surface_shape_.StartAnimation(L"Size", surface_spring_);
  } else {
    surface_shape_.StopAnimation(L"Size");
    surface_shape_.Size(size);
  }
}

void OnboardingView::Finish(const OnboardingVm& vm) {
  finishing_ = true;
  const auto& t = Onboarding();
  const auto compositor = UiCompositor();
  finish_batch_.Begin(compositor);
  if (Animate()) {
    const auto ease = Bezier(compositor, t.finish_bezier);
    const auto fade_out = [&](float delay) {
      auto fade = compositor.CreateScalarKeyFrameAnimation();
      fade.InsertExpressionKeyFrame(0, L"this.StartingValue");
      fade.InsertKeyFrame(1, 0, ease);
      fade.Duration(Ms(t.finish_fade_ms));
      fade.DelayTime(Ms(delay));
      fade.DelayBehavior(
          composition::AnimationDelayBehavior::SetInitialValueAfterDelay);
      return fade;
    };
    if (vm.outcome == OnboardingOutcome::kCompleted) {
      // The card becomes the address field: its content lifts away while
      // the surface morphs into place, then everything melts into the page.
      std::array<xaml::UIElement, 3> parts{header_, scroll_, actions_};
      choreography_.Recede(parts, {.duration_ms = t.exit_ms,
                                   .stagger_ms = t.exit_stagger_ms,
                                   .easing = t.ease_in,
                                   .to = {0, t.exit_rise, 0}});
      const Rect target = vm.ready_target;
      const float2 offset{target.x - card_rect_.x, target.y - card_rect_.y};
      const float2 size{target.width, target.height};
      for (const auto& [property, value] :
           {std::pair{L"Offset", offset}, std::pair{L"Size", size},
            std::pair{L"CornerRadius",
                      float2{vm.ready_radius, vm.ready_radius}}}) {
        auto morph = compositor.CreateSpringVector2Animation();
        Configure(morph, MorphSpring());
        morph.FinalValue(value);
        morph.DelayTime(Ms(t.exit_ms));
        surface_shape_.StartAnimation(property, morph);
      }
      const float settle = t.exit_ms + t.morph_spring_period_ms;
      auto fade = fade_out(settle);
      fade.Target(L"Opacity");
      surface_layer_.StartAnimation(L"Opacity", fade);
      for (const auto& layer :
           {xaml::UIElement(page_), xaml::UIElement(veil_)}) {
        auto layer_fade = fade_out(settle);
        layer_fade.Target(L"Opacity");
        layer.StartAnimation(layer_fade);
      }
    } else {
      auto fade = fade_out(0);
      fade.Target(L"Opacity");
      root_.StartAnimation(fade);
    }
  }
  finish_batch_.End(
      [this] { context_.delegate.OnOnboardingHandoffFinished(); });
}

void OnboardingView::Announce(const OnboardingVm& vm) {
  try {
    const auto title = NativeText(vm.title, text_);
    const auto message = vm.progress.empty()
                             ? title
                             : winrt::to_hstring(vm.progress) + L", " + title;
    auto peer =
        xaml::Automation::Peers::FrameworkElementAutomationPeer::FromElement(
            card_);
    if (!peer)
      peer = xaml::Automation::Peers::FrameworkElementAutomationPeer::
          CreatePeerForElement(card_);
    if (peer)
      peer.RaiseNotificationEvent(
          xaml::Automation::Peers::AutomationNotificationKind::ActionCompleted,
          xaml::Automation::Peers::AutomationNotificationProcessing::
              ImportantMostRecent,
          message, L"KryptonOnboardingStep");
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Onboarding step could not be announced");
  }
}

void OnboardingView::FocusMoment(OnboardingStep step) {
  const std::weak_ptr<int> weak = lifetime_;
  // Focus after the new panel is in the tree and laid out.
  root_.DispatcherQueue().TryEnqueue([this, weak, step] {
    if (weak.expired() || step != step_ || finishing_)
      return;
    try {
      controls::Control target = step == OnboardingStep::kIgnition
                                     ? begin_->control
                                     : moments_->FirstFocus(step);
      if (!target)
        target = next_->control;
      target.Focus(xaml::FocusState::Programmatic);
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "Onboarding focus could not move");
    }
  });
}
}  // namespace kr::win
