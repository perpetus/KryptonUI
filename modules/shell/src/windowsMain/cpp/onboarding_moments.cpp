#include "onboarding_moments.hpp"

#include <shcore.h>
#include <shlwapi.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <format>
#include <string>
#include <utility>

#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/presentation/onboarding.hpp"
#include "settings_appearance.hpp"

namespace kr::win {
namespace {
const auto& Onboarding() noexcept {
  return kTokens.onboarding;
}
float Gap() noexcept {
  return OnboardingRem(Onboarding().card_gap_rem);
}
constexpr std::array kImportItems{
    ImportData::kBookmarks,     ImportData::kHistory,  ImportData::kPasswords,
    ImportData::kSearchEngines, ImportData::kAutofill, ImportData::kExtensions};
// Segoe Fluent Icons: CheckMark.
constexpr std::wstring_view kCheck = L"";

void Show(const xaml::UIElement& element, bool visible) {
  const auto value =
      visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
  if (element.Visibility() != value)
    element.Visibility(value);
}
void SetIfChanged(const controls::TextBlock& block,
                  const winrt::hstring& text) {
  if (block.Text() != text)
    block.Text(text);
}

// Keep one native choice per host id, including across reordering and removal.
template <typename Records, typename Factory>
void ReconcileChoices(std::vector<std::unique_ptr<OnboardingChoice>>& cards,
                      const controls::StackPanel& panel,
                      const Records& records,
                      Factory make) {
  const auto children = panel.Children();
  for (std::size_t i = 0; i < cards.size();) {
    if (std::ranges::any_of(records, [&](const auto& record) {
          return record.id.value == cards[i]->id;
        })) {
      ++i;
      continue;
    }
    std::uint32_t index = 0;
    if (children.IndexOf(cards[i]->frame, index))
      children.RemoveAt(index);
    cards.erase(cards.begin() + static_cast<std::ptrdiff_t>(i));
  }
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto id = records[i].id.value;
    const auto found = std::ranges::find_if(
        cards, [id](const auto& card) { return card->id == id; });
    if (found == cards.end()) {
      auto card = make(id);
      children.InsertAt(static_cast<std::uint32_t>(i), card->frame);
      cards.insert(cards.begin() + static_cast<std::ptrdiff_t>(i),
                   std::move(card));
    } else if (static_cast<std::size_t>(found - cards.begin()) != i) {
      auto card = std::move(*found);
      cards.erase(found);
      std::uint32_t index = 0;
      if (children.IndexOf(card->frame, index))
        children.RemoveAt(index);
      children.InsertAt(static_cast<std::uint32_t>(i), card->frame);
      cards.insert(cards.begin() + static_cast<std::ptrdiff_t>(i),
                   std::move(card));
    }
  }
}
}  // namespace

OnboardingMoments::OnboardingMoments(ViewContext& context,
                                     OnboardingStyle& style,
                                     ClientCenter center)
    : context_(context), style_(style), center_(std::move(center)) {}

OnboardingMoments::~OnboardingMoments() = default;

std::size_t OnboardingMoments::Index(OnboardingStep step) noexcept {
  return std::to_underlying(step);
}

xaml::FrameworkElement OnboardingMoments::Panel(OnboardingStep step) {
  return Build(step).panel;
}

std::span<const xaml::UIElement> OnboardingMoments::Parts(
    OnboardingStep step) const noexcept {
  const auto& moment = moments_[Index(step)];
  if (!moment.panel)
    return {};
  return moment.parts;
}

std::unique_ptr<OnboardingChoice> OnboardingMoments::Choice(
    std::wstring_view group,
    std::uint64_t id,
    OnboardingChoiceOptions options,
    std::move_only_function<void(std::uint64_t)> chosen) {
  // Reconciling checks cards to match the view model; only the user's choice
  // reaches the delegate.
  return MakeOnboardingChoice(
      style_, context_, group, id, options,
      [this, chosen = std::move(chosen)](std::uint64_t value) mutable {
        if (!reconciling_)
          chosen(value);
      });
}

OnboardingMoments::Moment& OnboardingMoments::Build(OnboardingStep step) {
  auto& moment = moments_[Index(step)];
  if (moment.panel)
    return moment;
  const auto& t = Onboarding();
  moment.panel = controls::StackPanel();
  moment.panel.Spacing(Gap());
  moment.title = OnboardingText(style_, t.title_rem, true);
  moment.title.LineHeight(OnboardingRem(t.title_rem) * t.title_line_height);
  moment.title.CharacterSpacing(static_cast<std::int32_t>(t.title_tracking));
  moment.title.Margin({0, 0, 0, OnboardingRem(t.title_gap_rem) - Gap()});
  moment.body = OnboardingText(style_, t.body_rem, false, true);
  moment.body.LineHeight(OnboardingRem(t.body_rem) * t.body_line_height);
  xaml::FrameworkElement controls{nullptr};
  switch (step) {
    case OnboardingStep::kShape:
      controls = BuildShape();
      break;
    case OnboardingStep::kColor:
      controls = BuildColor();
      break;
    case OnboardingStep::kImport:
      controls = BuildImport();
      break;
    case OnboardingStep::kSearch:
      controls = BuildSearch();
      break;
    case OnboardingStep::kPrivacy:
      controls = BuildPrivacy();
      break;
    case OnboardingStep::kDefaultBrowser:
      controls = BuildDefault();
      break;
    case OnboardingStep::kReady:
      controls = BuildReady();
      break;
    case OnboardingStep::kIgnition:
      controls = controls::Grid();
      break;
  }
  moment.panel.Children().Append(moment.title);
  moment.panel.Children().Append(moment.body);
  moment.panel.Children().Append(controls);
  moment.parts = {moment.title, moment.body, controls};
  text_ready_ = false;
  return moment;
}

// ------------------------------------------------------------- building

xaml::FrameworkElement OnboardingMoments::BuildShape() {
  controls::StackPanel root;
  root.Spacing(Gap());
  controls::Grid grid;
  grid.ColumnSpacing(Gap() / 2);
  grid.RowSpacing(Gap() / 2);
  constexpr int kColumns = 3;
  for (int i = 0; i < kColumns; ++i)
    AddColumn(grid, 1, xaml::GridUnitType::Star);
  const auto items = SettingsLayouts();
  for (std::size_t i = 0; i < (items.size() + kColumns - 1) / kColumns; ++i)
    AddRow(grid, 0, xaml::GridUnitType::Auto);
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto choice = items[i].choice;
    auto card =
        Choice(L"krypton-onboarding-layout", std::to_underlying(choice),
               {.picture = true, .compact = true}, [this](std::uint64_t id) {
                 context_.delegate.OnOnboardingLayoutChosen(
                     static_cast<LayoutChoice>(id));
               });
    media::Imaging::BitmapImage bitmap;
    // URI decoding and file loading are asynchronous WinUI services.
    bitmap.UriSource(LayoutImage(items[i].image));
    card->picture.Source(bitmap);
    // Pointing at or focusing a layout previews it over the real window.
    const auto preview = [this, choice](bool on) {
      context_.delegate.OnOnboardingLayoutHovered(on ? std::optional(choice)
                                                     : std::nullopt);
    };
    card->hovered = card->radio.PointerEntered(
        winrt::auto_revoke, [preview](auto&&, auto&&) { preview(true); });
    card->unhovered = card->radio.PointerExited(
        winrt::auto_revoke, [preview](auto&&, auto&&) { preview(false); });
    card->focused = card->radio.GotFocus(
        winrt::auto_revoke, [preview](auto&&, auto&&) { preview(true); });
    card->unfocused = card->radio.LostFocus(
        winrt::auto_revoke, [preview](auto&&, auto&&) { preview(false); });
    controls::Grid::SetColumn(card->frame, static_cast<int>(i % kColumns));
    controls::Grid::SetRow(card->frame, static_cast<int>(i / kColumns));
    grid.Children().Append(card->frame);
    layouts_.push_back(std::move(card));
  }
  root.Children().Append(grid);
  side_ = controls::ToggleSwitch();
  side_.Foreground(style_.text);
  side_toggled_ = side_.Toggled(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (!reconciling_)
      context_.delegate.OnOnboardingSideChosen(side_.IsOn());
  });
  root.Children().Append(side_);
  return root;
}

xaml::FrameworkElement OnboardingMoments::BuildColor() {
  const auto& editor = kTokens.theme_editor;
  controls::StackPanel root;
  root.Spacing(Gap());
  controls::StackPanel schemes;
  schemes.Orientation(controls::Orientation::Horizontal);
  schemes.Spacing(editor.button_gap);
  const std::array icons{&design::icons::kSparkles, &design::icons::kFaceSun,
                         &design::icons::kMoonStars};
  for (std::size_t i = 0; i < schemes_.size(); ++i) {
    schemes_[i] = SourceButton(context_.brushes, *icons[i], L"", [this, i] {
      context_.delegate.OnOnboardingThemeEdited(
          {.kind = ThemeEditKind::kScheme, .index = i},
          center_(schemes_[i]->control));
    });
    schemes_[i]->control.Width(editor.button_size);
    schemes_[i]->control.Height(editor.button_size);
    schemes.Children().Append(schemes_[i]->control);
  }
  root.Children().Append(schemes);
  swatch_row_ = controls::StackPanel();
  swatch_row_.Orientation(controls::Orientation::Horizontal);
  swatch_row_.Spacing(editor.button_gap);
  root.Children().Append(swatch_row_);
  controls::StackPanel tools;
  tools.Orientation(controls::Orientation::Horizontal);
  tools.Spacing(editor.button_gap);
  const auto tool = [this](const design::icons::Icon& icon,
                           ThemeEditKind kind) {
    auto button = SourceButton(context_.brushes, icon, L"", nullptr);
    button->click.revoke();
    button->click = button->control.Click(
        winrt::auto_revoke,
        [this, kind, element = button->control](auto&&, auto&&) {
          context_.delegate.OnOnboardingThemeEdited({.kind = kind},
                                                    center_(element));
        });
    return button;
  };
  add_ = tool(design::icons::kAdd, ThemeEditKind::kAddPoint);
  harmony_ = tool(design::icons::kAlgorithm, ThemeEditKind::kCycleHarmony);
  remove_ = IconButton(context_.brushes, glyphs::kMinimize, L"", [this] {
    context_.delegate.OnOnboardingThemeEdited(
        {.kind = ThemeEditKind::kRemovePoint}, center_(remove_->control));
  });
  for (const auto* button : {add_.get(), remove_.get(), harmony_.get()}) {
    button->control.Width(editor.button_size);
    button->control.Height(editor.button_size);
    tools.Children().Append(button->control);
  }
  root.Children().Append(tools);
  return root;
}

xaml::FrameworkElement OnboardingMoments::BuildImport() {
  controls::StackPanel root;
  root.Spacing(Gap());
  sources_panel_ = controls::StackPanel();
  sources_panel_.Spacing(Gap() / 2);
  fresh_ = Choice(L"krypton-onboarding-import", 0, {}, [this](std::uint64_t) {
    context_.delegate.OnOnboardingImportChosen({});
  });
  sources_panel_.Children().Append(fresh_->frame);
  root.Children().Append(sources_panel_);
  items_panel_ = controls::StackPanel();
  items_panel_.Spacing(0);
  for (std::size_t i = 0; i < items_.size(); ++i) {
    auto& item = items_[i];
    item.box = controls::CheckBox();
    item.box.Foreground(style_.text);
    const auto kind = kImportItems[i];
    item.checked =
        item.box.Checked(winrt::auto_revoke, [this, kind](auto&&, auto&&) {
          if (!reconciling_)
            context_.delegate.OnOnboardingImportItemToggled(kind, true);
        });
    item.unchecked =
        item.box.Unchecked(winrt::auto_revoke, [this, kind](auto&&, auto&&) {
          if (!reconciling_)
            context_.delegate.OnOnboardingImportItemToggled(kind, false);
        });
    items_panel_.Children().Append(item.box);
  }
  root.Children().Append(items_panel_);
  return root;
}

xaml::FrameworkElement OnboardingMoments::BuildSearch() {
  engines_panel_ = controls::StackPanel();
  engines_panel_.Spacing(Gap() / 2);
  return engines_panel_;
}

xaml::FrameworkElement OnboardingMoments::BuildPrivacy() {
  privacy_ = std::make_unique<PrivacySettings>(context_);
  return privacy_->Root();
}

xaml::FrameworkElement OnboardingMoments::BuildDefault() {
  controls::StackPanel row;
  row.Orientation(controls::Orientation::Horizontal);
  row.Spacing(Gap() / 2);
  default_glyph_ = Glyph(kCheck, OnboardingRem(Onboarding().body_rem));
  default_glyph_.Foreground(style_.accent);
  default_glyph_.VerticalAlignment(xaml::VerticalAlignment::Center);
  default_status_ = OnboardingText(style_, Onboarding().body_rem, true);
  row.Children().Append(default_glyph_);
  row.Children().Append(default_status_);
  return row;
}

xaml::FrameworkElement OnboardingMoments::BuildReady() {
  controls::StackPanel root;
  root.Spacing(Gap());
  summary_ = controls::StackPanel();
  summary_.Spacing(Gap() / 2);
  root.Children().Append(summary_);
  relaunch_ = controls::Button();
  relaunch_.Foreground(style_.accent);
  relaunch_click_ = relaunch_.Click(winrt::auto_revoke, [this](auto&&, auto&&) {
    context_.delegate.OnPrivacyRelaunch();
  });
  root.Children().Append(relaunch_);
  return root;
}

// ------------------------------------------------------------- rendering

void OnboardingMoments::SetText(UiTextView text) {
  for (std::size_t i = 0; i < moments_.size(); ++i) {
    const auto& moment = moments_[i];
    if (!moment.panel)
      continue;
    const auto step = static_cast<OnboardingStep>(i);
    moment.title.Text(NativeText(OnboardingTitle(step), text));
    moment.body.Text(NativeText(OnboardingBody(step), text));
    moment.panel.FlowDirection(text.right_to_left
                                   ? xaml::FlowDirection::RightToLeft
                                   : xaml::FlowDirection::LeftToRight);
  }
  const auto layouts = SettingsLayouts();
  for (std::size_t i = 0; i < layouts_.size() && i < layouts.size(); ++i) {
    const auto label = NativeText(layouts[i].label, text);
    layouts_[i]->label.Text(label);
    Accessible(layouts_[i]->radio, label);
  }
  if (side_)
    side_.Header(
        winrt::box_value(NativeText(UiText::kSettingsSidebarRight, text)));
  const std::array schemes{UiText::kThemeSystem, UiText::kThemeLight,
                           UiText::kThemeDark};
  for (std::size_t i = 0; i < schemes_.size(); ++i)
    if (schemes_[i])
      SetButtonText(*schemes_[i], NativeText(schemes[i], text));
  for (auto& swatch : swatches_)
    SetButtonText(*swatch, NativeText(UiText::kThemePreset, text));
  if (add_) {
    SetButtonText(*add_, NativeText(UiText::kThemeAdd, text));
    SetButtonText(*remove_, NativeText(UiText::kThemeRemove, text));
    SetButtonText(*harmony_, NativeText(UiText::kThemeHarmony, text));
  }
  if (fresh_)
    fresh_->label.Text(NativeText(UiText::kOnboardingImportNone, text));
  for (std::size_t i = 0; i < items_.size(); ++i)
    if (items_[i].box)
      items_[i].box.Content(
          winrt::box_value(NativeText(ImportDataText(kImportItems[i]), text)));
  if (relaunch_)
    relaunch_.Content(
        winrt::box_value(NativeText(UiText::kPrivacyRelaunch, text)));
  text_ready_ = true;
}

void OnboardingMoments::Render(const ShellViewModel& vm, bool text_changed) {
  const auto& onboarding = vm.onboarding;
  // Build the current moment before reconciling, so its first frame is right.
  if (onboarding.step != OnboardingStep::kIgnition)
    (void)Build(onboarding.step);
  reconciling_ = true;
  struct Scope {
    bool& value;
    ~Scope() { value = false; }
  } scope{reconciling_};
  if (!text_ready_ || text_changed)
    SetText(vm.text);
  if (!layouts_.empty())
    RenderShape(onboarding, vm.text);
  if (swatch_row_)
    RenderColor(onboarding);
  if (sources_panel_)
    RenderImport(vm);
  if (engines_panel_)
    RenderSearch(vm);
  if (privacy_)
    RenderPrivacy(vm, text_changed);
  if (default_status_)
    RenderDefault(onboarding, vm.text);
  if (summary_)
    RenderReady(onboarding, vm.text);
}

void OnboardingMoments::RenderShape(const OnboardingVm& vm, UiTextView) {
  for (auto& card : layouts_)
    SelectOnboardingChoice(*card, style_,
                           card->id == std::to_underlying(vm.layout));
  Show(side_, vm.side_available);
  if (side_.IsOn() != vm.sidebar_right)
    side_.IsOn(vm.sidebar_right);
}

void OnboardingMoments::RenderColor(const OnboardingVm& vm) {
  const auto& editor = kTokens.theme_editor;
  if (swatches_.size() != vm.presets.size() ||
      first_preset_ != vm.first_preset) {
    first_preset_ = vm.first_preset;
    swatch_row_.Children().Clear();
    swatches_.clear();
    swatch_colors_.clear();
    for (std::size_t i = 0; i < vm.presets.size(); ++i) {
      auto button = IconButton(context_.brushes, L"", L"", [this, i] {
        context_.delegate.OnOnboardingThemeEdited(
            {.kind = ThemeEditKind::kPreset, .index = first_preset_ + i},
            center_(swatches_[i]->control));
      });
      button->control.Width(editor.swatch_size);
      button->control.Height(editor.swatch_size);
      button->control.Margin(UniformThickness(0));
      button->control.CornerRadius(UniformCorners(editor.swatch_size / 2));
      controls::Border dot;
      dot.Width(editor.swatch_size);
      dot.Height(editor.swatch_size);
      dot.CornerRadius(UniformCorners(editor.swatch_size / 2));
      media::SolidColorBrush color;
      dot.Background(color);
      button->control.Content(dot);
      swatch_row_.Children().Append(button->control);
      swatches_.push_back(std::move(button));
      swatch_colors_.push_back(color);
    }
    text_ready_ = false;
  }
  for (std::size_t i = 0; i < vm.presets.size(); ++i)
    swatch_colors_[i].Color(NativeColor(Color{vm.presets[i].color}));
  const auto* theme = vm.theme;
  for (std::size_t i = 0; i < schemes_.size(); ++i)
    schemes_[i]->control.Background(
        theme && i == static_cast<std::size_t>(theme->scheme)
            ? context_.brushes.editor_selection
            : context_.brushes.transparent);
  const auto points = theme ? theme->point_count : 0u;
  add_->control.IsEnabled(points > 0 && points < kMaxThemePoints);
  remove_->control.IsEnabled(points > 0);
  harmony_->control.IsEnabled(points > 1);
}

media::ImageSource OnboardingMoments::BundledImage(const OnboardingIcon* icon,
                                                   bool dark) {
  if (!icon || !icon->geometry.empty())
    return nullptr;
  auto& entry = brand_images_[icon];
  const bool variant = dark && !icon->dark_svg.empty();
  auto& image = variant ? entry.dark : entry.light;
  auto& tried = variant ? entry.tried_dark : entry.tried_light;
  if (tried)
    return image;
  tried = true;
  try {
    const auto svg = variant ? icon->dark_svg : icon->svg;
    const auto* data = !svg.empty() ? reinterpret_cast<const BYTE*>(svg.data())
                                    : icon->png.data();
    const auto size = !svg.empty() ? svg.size() : icon->png.size();
    winrt::com_ptr<IStream> memory;
    memory.attach(SHCreateMemStream(data, static_cast<UINT>(size)));
    winrt::check_bool(static_cast<bool>(memory));
    winrt::Windows::Storage::Streams::IRandomAccessStream stream{nullptr};
    winrt::check_hresult(CreateRandomAccessStreamOverStream(
        memory.get(), BSOS_DEFAULT,
        winrt::guid_of<winrt::Windows::Storage::Streams::IRandomAccessStream>(),
        winrt::put_abi(stream)));
    // WinUI owns the asynchronous decode; nothing reads files or waits on the
    // UI thread. Keep one source per brand and scheme for all profile cards.
    if (!svg.empty()) {
      media::Imaging::SvgImageSource source;
      [[maybe_unused]] const auto loading = source.SetSourceAsync(stream);
      image = source;
    } else {
      media::Imaging::BitmapImage source;
      [[maybe_unused]] const auto loading = source.SetSourceAsync(stream);
      image = source;
    }
  } catch (const winrt::hresult_error& error) {
    platform::WriteDefaultLog(
        LogLevel::kWarning,
        std::format("Onboarding icon {} could not load (0x{:08X})", icon->name,
                    static_cast<std::uint32_t>(error.code().value)));
  }
  return image;
}

void OnboardingMoments::SetChoiceIcon(OnboardingChoice& choice,
                                      ImageId host_icon,
                                      const ShellViewModel& vm,
                                      const OnboardingIcon* icon) {
  auto image = context_.images.Get(host_icon, vm.images);
  if (!image)
    image = BundledImage(icon, vm.theme.dark);
  if (image) {
    if (choice.icon.Source() != image)
      choice.icon.Source(image);
    if (choice.icon_tile.Child() != choice.icon)
      choice.icon_tile.Child(choice.icon);
    return;
  }
  const auto* artwork = icon && !icon->geometry.empty() ? icon : nullptr;
  if (!choice.brand_view || choice.brand != artwork) {
    const auto source = artwork ? design::icons::Icon{artwork->geometry, 24, 24}
                                : design::icons::kGlobe;
    choice.brand_view = SourceIcon(style_.text, source);
    choice.brand_view.Width(Onboarding().icon_size);
    choice.brand_view.Height(Onboarding().icon_size);
    choice.brand = artwork;
  }
  if (choice.icon_tile.Child() != choice.brand_view)
    choice.icon_tile.Child(choice.brand_view);
}

void OnboardingMoments::RenderImport(const ShellViewModel& vm) {
  const auto& onboarding = vm.onboarding;
  const auto sources = onboarding.import_sources;
  const bool busy = onboarding.import_requested ||
                    onboarding.import.phase == ImportPhase::kRunning ||
                    onboarding.import.phase == ImportPhase::kSucceeded;
  ReconcileChoices(sources_, sources_panel_, sources, [this](std::uint64_t id) {
    return Choice(
        L"krypton-onboarding-import", id, {.icon = true},
        [this](std::uint64_t id) {
          context_.delegate.OnOnboardingImportChosen(ImportSourceId{id});
        });
  });
  for (std::size_t i = 0; i < sources.size(); ++i) {
    auto& card = *sources_[i];
    SetIfChanged(card.label, winrt::to_hstring(sources[i].name));
    SetChoiceIcon(card, sources[i].icon, vm,
                  OnboardingBrowserIcon(sources[i].name));
    SelectOnboardingChoice(card, style_,
                           onboarding.import_source == sources[i].id);
    card.radio.IsEnabled(!busy);
  }
  SelectOnboardingChoice(*fresh_, style_, !onboarding.import_source);
  fresh_->radio.IsEnabled(!busy);
  const ImportSource* selected = nullptr;
  for (const auto& source : sources)
    if (source.id == onboarding.import_source)
      selected = &source;
  Show(items_panel_, selected != nullptr);
  for (std::size_t i = 0; i < items_.size(); ++i) {
    auto& item = items_[i];
    const bool offered = selected && selected->items.Has(kImportItems[i]);
    Show(item.box, offered);
    item.box.IsEnabled(!busy);
    const bool checked = onboarding.import_items.Has(kImportItems[i]);
    if (!item.box.IsChecked() || item.box.IsChecked().Value() != checked)
      item.box.IsChecked(checked);
  }
}

void OnboardingMoments::RenderSearch(const ShellViewModel& vm) {
  const auto& onboarding = vm.onboarding;
  const auto engines = onboarding.engines;
  ReconcileChoices(engines_, engines_panel_, engines, [this](std::uint64_t id) {
    return Choice(
        L"krypton-onboarding-search", id, {.icon = true, .detail = true},
        [this](std::uint64_t id) {
          context_.delegate.OnOnboardingSearchChosen(SearchEngineId{id});
        });
  });
  for (std::size_t i = 0; i < engines.size(); ++i) {
    auto& card = *engines_[i];
    SetIfChanged(card.label, winrt::to_hstring(engines[i].name));
    SetIfChanged(card.detail, winrt::to_hstring(engines[i].keyword));
    SetChoiceIcon(card, engines[i].icon, vm,
                  OnboardingSearchIcon(engines[i].keyword, engines[i].name));
    SelectOnboardingChoice(card, style_, onboarding.engine == engines[i].id);
  }
}

void OnboardingMoments::RenderPrivacy(const ShellViewModel& vm,
                                      bool text_changed) {
  const auto& onboarding = vm.onboarding;
  PrivacyFeaturesVm rows;
  rows.rows = onboarding.privacy;
  rows.confirmation = onboarding.privacy_confirmation;
  rows.visible = true;
  rows.restart_pending = onboarding.privacy_restart;
  rows.can_relaunch = onboarding.can_relaunch;
  rows.failed = vm.privacy_features.failed;
  rows.revision = vm.privacy_features.revision;
  if (text_changed)
    privacy_->InvalidateText();
  privacy_->Render(rows, vm.text, text_changed);
}

void OnboardingMoments::RenderDefault(const OnboardingVm& vm, UiTextView text) {
  const bool done = vm.default_browser.Has(DefaultBrowserFlag::kIsDefault);
  const bool waiting =
      !done && (vm.default_requested ||
                vm.default_browser.Has(DefaultBrowserFlag::kPending));
  Show(default_glyph_, done);
  Show(default_status_, done || waiting);
  SetIfChanged(default_status_,
               NativeText(done ? UiText::kOnboardingDefaultDone
                               : UiText::kOnboardingDefaultWaiting,
                          text));
}

void OnboardingMoments::RenderReady(const OnboardingVm& vm, UiTextView text) {
  std::array<winrt::hstring, 4> lines;
  std::size_t count = 0;
  for (const auto& item : vm.layouts)
    if (item.choice == vm.layout)
      lines[count++] = NativeText(item.label, text);
  for (const auto& engine : vm.engines)
    if (engine.id == vm.engine)
      lines[count++] = winrt::to_hstring(engine.name);
  if (!vm.import_status.empty())
    lines[count++] = winrt::to_hstring(vm.import_status);
  if (vm.default_browser.Has(DefaultBrowserFlag::kIsDefault))
    lines[count++] = NativeText(UiText::kOnboardingDefaultDone, text);
  while (summary_lines_.size() < count) {
    controls::StackPanel row;
    row.Orientation(controls::Orientation::Horizontal);
    row.Spacing(Gap() / 2);
    auto glyph = Glyph(kCheck, OnboardingRem(Onboarding().body_rem));
    glyph.Foreground(style_.accent);
    glyph.VerticalAlignment(xaml::VerticalAlignment::Center);
    auto label = OnboardingText(style_, Onboarding().body_rem);
    row.Children().Append(glyph);
    row.Children().Append(label);
    summary_.Children().Append(row);
    summary_lines_.push_back(label);
  }
  for (std::size_t i = 0; i < summary_lines_.size(); ++i) {
    const auto row = summary_.Children().GetAt(static_cast<std::uint32_t>(i));
    Show(row, i < count);
    if (i < count)
      SetIfChanged(summary_lines_[i], lines[i]);
  }
  Show(relaunch_, vm.privacy_restart && vm.can_relaunch);
}

xaml::UIElement OnboardingMoments::Source(OnboardingStep step) const {
  const auto selected = [](const Choices& cards) -> xaml::UIElement {
    for (const auto& card : cards) {
      const auto checked = card->radio.IsChecked();
      if (checked && checked.Value())
        return card->frame;
    }
    return nullptr;
  };
  switch (step) {
    case OnboardingStep::kImport:
      return selected(sources_);
    case OnboardingStep::kSearch:
      return selected(engines_);
    case OnboardingStep::kPrivacy:
      return privacy_ ? xaml::UIElement(privacy_->Root()) : nullptr;
    default:
      return nullptr;
  }
}

controls::Control OnboardingMoments::FirstFocus(OnboardingStep step) const {
  const auto first = [](const Choices& cards) -> controls::Control {
    for (const auto& card : cards) {
      const auto checked = card->radio.IsChecked();
      if (checked && checked.Value())
        return card->radio;
    }
    return cards.empty() ? nullptr : controls::Control(cards.front()->radio);
  };
  switch (step) {
    case OnboardingStep::kShape:
      return first(layouts_);
    case OnboardingStep::kColor:
      return schemes_[0] ? controls::Control(schemes_[0]->control) : nullptr;
    case OnboardingStep::kImport:
      return sources_.empty()
                 ? (fresh_ ? controls::Control(fresh_->radio) : nullptr)
                 : first(sources_);
    case OnboardingStep::kSearch:
      return first(engines_);
    default:
      return nullptr;
  }
}
}  // namespace kr::win
