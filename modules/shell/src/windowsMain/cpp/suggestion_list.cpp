#include "suggestion_list.hpp"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Core.h>

#include <algorithm>
#include <limits>

#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {

constexpr auto kNoSuggestion = std::numeric_limits<std::size_t>::max();
Disposition ClickDisposition() {
  const auto down = [](winrt::Windows::System::VirtualKey key) {
    return (winrt::Microsoft::UI::Input::InputKeyboardSource::
                GetKeyStateForCurrentThread(key) &
            winrt::Windows::UI::Core::CoreVirtualKeyStates::Down) !=
           winrt::Windows::UI::Core::CoreVirtualKeyStates::None;
  };
  const bool control = down(winrt::Windows::System::VirtualKey::Control);
  const bool shift = down(winrt::Windows::System::VirtualKey::Shift);
  return control
             ? (shift ? Disposition::kNewTab : Disposition::kNewBackgroundTab)
         : shift ? Disposition::kNewWindow
                 : Disposition::kCurrentTab;
}

std::wstring_view SuggestionGlyph(SuggestionKind kind) noexcept {
  switch (kind) {
    case SuggestionKind::kHistory:
      return L"\uE81C";
    case SuggestionKind::kBookmark:
      return L"\uE734";
    case SuggestionKind::kOpenTab:
      return L"\uE9E9";
    case SuggestionKind::kCommand:
      return L"\uE756";
    case SuggestionKind::kSearch:
    case SuggestionKind::kBang:
      return glyphs::kSearch;
    default:
      return glyphs::kGlobe;
  }
}

}  // namespace

struct SuggestionList::Row {
  std::size_t index = kNoSuggestion;
  controls::ListViewItem item{nullptr};
  controls::Image image{nullptr};
  controls::TextBlock glyph{nullptr};
  controls::RichTextBlock text{nullptr};
  struct StyledRun {
    xaml::Documents::Run run{nullptr};
    Flags<SuggestionTextStyle> style;
    bool secondary = false;
  };
  xaml::Documents::Paragraph paragraph{nullptr};
  std::vector<StyledRun> runs;
  std::uint64_t text_revision = 0;
  bool rich_image = false;
  SuggestionKind kind = SuggestionKind::kUrl;
  bool has_glyph = false;

  Row(ViewContext& context,
      const controls::ListViewItem& container,
      const controls::Grid& content)
      : item(container) {
    const auto& tokens = kTokens.overlay;
    item.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    item.IsTabStop(false);
    item.MinHeight(tokens.row_height);
    item.Padding({tokens.row_padding_inline, tokens.row_padding_block,
                  tokens.row_padding_inline, tokens.row_padding_block});
    item.BorderThickness(UniformThickness(tokens.row_border_width));
    item.BorderBrush(context.brushes.transparent);
    item.CornerRadius(UniformCorners(tokens.row_radius));
    AddColumn(content, 0, xaml::GridUnitType::Auto);
    AddColumn(content, 1, xaml::GridUnitType::Star);
    const auto icon_host =
        content.FindName(L"SuggestionIcon").as<controls::Border>();
    icon_host.Padding(UniformThickness(tokens.icon_padding));
    icon_host.CornerRadius(UniformCorners(tokens.icon_radius));
    icon_host.Margin({0, 0, tokens.icon_gap, 0});
    icon_host.VerticalAlignment(xaml::VerticalAlignment::Center);
    icon_host.Width(tokens.rich_image_size + tokens.icon_padding * 2);
    image = content.FindName(L"SuggestionImage").as<controls::Image>();
    image.Width(kTokens.spacing.icon_size);
    image.Height(kTokens.spacing.icon_size);
    image.Stretch(media::Stretch::Uniform);
    glyph = content.FindName(L"SuggestionGlyph").as<controls::TextBlock>();
    glyph.FontFamily(
        media::FontFamily(winrt::to_hstring(kTokens.type.icon_family)));
    glyph.FontSize(kTokens.spacing.icon_size);
    glyph.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    glyph.VerticalAlignment(xaml::VerticalAlignment::Center);
    text = content.FindName(L"SuggestionText").as<controls::RichTextBlock>();
    text.FontFamily(media::FontFamily(winrt::to_hstring(kTokens.type.family)));
    text.FontSize(kTokens.type.size);
    text.FontWeight(kMedium);
    text.TextWrapping(xaml::TextWrapping::Wrap);
    text.MaxLines(1);
    text.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
    text.IsTextSelectionEnabled(false);
    text.VerticalAlignment(xaml::VerticalAlignment::Center);
    paragraph = text.Blocks().GetAt(0).as<xaml::Documents::Paragraph>();
    paragraph.Margin(UniformThickness(0));
    paragraph.Inlines().Clear();
  }
};

SuggestionList::SuggestionList(ViewContext& context) : context_(context) {
  list_.IsItemClickEnabled(true);
  list_.SelectionMode(controls::ListViewSelectionMode::Single);
  list_.IsTabStop(false);
  list_.Background(context.brushes.transparent);
  list_.BorderThickness(UniformThickness(0));
  list_.Padding(UniformThickness(0));
  controls::ScrollViewer::SetHorizontalScrollBarVisibility(
      list_, controls::ScrollBarVisibility::Disabled);
  controls::ScrollViewer::SetVerticalScrollBarVisibility(
      list_, controls::ScrollBarVisibility::Hidden);
  const auto set = [&](std::wstring_view key, const auto& brush) {
    list_.Resources().Insert(winrt::box_value(winrt::hstring(key)), brush);
  };
  set(L"ListViewItemBackgroundPointerOver", context.brushes.search_hover);
  set(L"ListViewItemBackgroundPressed", context.brushes.search_hover);
  set(L"ListViewItemBackgroundSelected", context.brushes.search_selected);
  set(L"ListViewItemBackgroundSelectedPointerOver",
      context.brushes.search_selected);
  set(L"ListViewItemBackgroundSelectedPressed",
      context.brushes.search_selected);
  clicked_ = list_.ItemClick(
      winrt::auto_revoke,
      [this](auto&&, const controls::ItemClickEventArgs& args) {
        const auto index =
            winrt::unbox_value<std::uint32_t>(args.ClickedItem());
        if (index < records_.size())
          context_.delegate.OnSuggestionChosen(index, ClickDisposition());
      });

  // WinUI owns the containers and template roots. Only realized rows get
  pointer_released_ = list_.PointerReleased(
      winrt::auto_revoke,
      [this](auto&&, const xaml::Input::PointerRoutedEventArgs& args) {
        const auto point = args.GetCurrentPoint(list_);
        if (point.Properties().PointerUpdateKind() !=
            winrt::Microsoft::UI::Input::PointerUpdateKind::
                MiddleButtonReleased)
          return;
        auto source = args.OriginalSource().try_as<xaml::DependencyObject>();
        while (source && !source.try_as<controls::ListViewItem>())
          source = media::VisualTreeHelper::GetParent(source);
        if (!source)
          return;
        const auto index = list_.IndexFromContainer(source);
        if (index >= 0 && static_cast<std::size_t>(index) < records_.size()) {
          context_.delegate.OnSuggestionChosen(static_cast<std::size_t>(index),
                                               Disposition::kNewBackgroundTab);
          args.Handled(true);
        }
      });

  // WinUI owns the containers and template roots. Only realized rows get
  // native controls, and recycling keeps their brushes and text elements.
  list_.ItemTemplate(xaml::Markup::XamlReader::Load(LR"(
    <DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                  xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
      <Grid>
        <Border x:Name="SuggestionIcon">
          <Grid>
            <Image x:Name="SuggestionImage"/>
            <TextBlock x:Name="SuggestionGlyph"/>
          </Grid>
        </Border>
        <RichTextBlock x:Name="SuggestionText" Grid.Column="1">
          <Paragraph><Run/><Run/><Run/></Paragraph>
        </RichTextBlock>
      </Grid>
    </DataTemplate>)")
                         .as<xaml::DataTemplate>());
  realize_ = [this, weak = std::weak_ptr<int>(lifetime_)](
                 const controls::ListViewBase&,
                 const controls::ContainerContentChangingEventArgs& args) {
    if (weak.expired())
      return;
    try {
      const auto container = args.ItemContainer().as<controls::ListViewItem>();
      const auto key = winrt::get_abi(container);
      if (args.InRecycleQueue()) {
        if (const auto found = rows_.find(key); found != rows_.end()) {
          found->second->index = kNoSuggestion;
          found->second->image.Source(nullptr);
        }
        return;
      }
      const auto content =
          container.ContentTemplateRoot().try_as<controls::Grid>();
      if (!content) {
        if (args.Phase() < 2)
          args.RegisterUpdateCallback(args.Phase() + 1, realize_);
        return;
      }
      auto& row = rows_[key];
      if (!row)
        row = std::make_unique<Row>(context_, container, content);
      row->index = winrt::unbox_value<std::uint32_t>(args.Item());
      UpdateRow(*row);
      args.Handled(true);
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "Could not realize a search suggestion");
    }
  };
  containers_ = list_.ContainerContentChanging(winrt::auto_revoke, realize_);
}

SuggestionList::~SuggestionList() {
  lifetime_.reset();
  pointer_released_.revoke();
  clicked_.revoke();
  containers_.revoke();
}

void SuggestionList::UpdateRow(Row& row) {
  if (row.index >= records_.size())
    return;
  const auto& record = records_[row.index];
  if (row.text_revision != record.text_revision) {
    row.paragraph.Inlines().Clear();
    row.runs.clear();
    const auto append = [&](const winrt::hstring& text,
                            std::span<const SuggestionClassification> styles,
                            bool secondary) {
      const auto add = [&](std::uint32_t first, std::uint32_t last,
                           Flags<SuggestionTextStyle> style) {
        xaml::Documents::Run run;
        run.Text(winrt::hstring{
            std::wstring_view{text.c_str() + first, last - first}});
        run.FontWeight(style.Has(SuggestionTextStyle::kMatch) ? kBold
                                                              : kMedium);
        if (style.Has(SuggestionTextStyle::kToolbelt))
          run.FontSize(kTokens.type.size_small);
        row.paragraph.Inlines().Append(run);
        row.runs.push_back({run, style, secondary});
      };
      if (styles.empty()) {
        add(0, text.size(), {});
      } else {
        for (std::size_t i = 0; i < styles.size(); ++i)
          add(styles[i].offset,
              i + 1 < styles.size() ? styles[i + 1].offset : text.size(),
              styles[i].style);
      }
    };
    append(record.title, record.contents_class, false);
    if (!record.detail.empty()) {
      append(winrt::to_hstring(kTokens.overlay.description_separator), {},
             true);
      append(record.detail, record.description_class, true);
    }
    row.text_revision = record.text_revision;
    xaml::Automation::AutomationProperties::SetName(row.item, record.title);
  }
  if (row.rich_image != record.rich_image) {
    const auto size = record.rich_image ? kTokens.overlay.rich_image_size
                                        : kTokens.spacing.icon_size;
    row.image.Width(size);
    row.image.Height(size);
    row.rich_image = record.rich_image;
  }
  if (!row.has_glyph || row.kind != record.kind) {
    row.kind = record.kind;
    row.has_glyph = true;
    row.glyph.Text(winrt::hstring(SuggestionGlyph(record.kind)));
  }
  if (row.image.Source() != record.bitmap)
    row.image.Source(record.bitmap);
  const auto image_visibility =
      record.bitmap ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
  const auto glyph_visibility =
      record.bitmap ? xaml::Visibility::Collapsed : xaml::Visibility::Visible;
  if (row.image.Visibility() != image_visibility)
    row.image.Visibility(image_visibility);
  if (row.glyph.Visibility() != glyph_visibility)
    row.glyph.Visibility(glyph_visibility);
  const auto& foreground = record.selected
                               ? context_.brushes.search_selected_text
                               : context_.brushes.search_text;
  if (row.text.Foreground() != foreground)
    row.text.Foreground(foreground);
  if (row.glyph.Foreground() != foreground)
    row.glyph.Foreground(foreground);
  const auto& detail = record.selected ? context_.brushes.search_selected_text
                                       : context_.brushes.search_description;
  for (const auto& span : row.runs) {
    const auto brush =
        !record.selected && span.style.Has(SuggestionTextStyle::kUrl)
            ? context_.brushes.search_url
        : span.secondary || span.style.Has(SuggestionTextStyle::kDim)
            ? detail
            : foreground;
    if (span.run.Foreground() != brush)
      span.run.Foreground(brush);
  }
}
void SuggestionList::Render(std::span<const SuggestionVm> suggestions,
                            int selected,
                            const ImageStore* images) {
  records_.resize(suggestions.size());
  const int selection =
      selected >= 0 && static_cast<std::size_t>(selected) < suggestions.size()
          ? selected
          : -1;
  for (std::size_t index = 0; index < suggestions.size(); ++index) {
    auto& record = records_[index];
    const auto& suggestion = suggestions[index];
    const bool text_changed =
        record.text_revision == 0 || record.contents != suggestion.contents ||
        record.description != suggestion.description ||
        !std::ranges::equal(record.contents_class, suggestion.contents_class) ||
        !std::ranges::equal(record.description_class,
                            suggestion.description_class);
    if (text_changed) {
      record.contents_class.assign(suggestion.contents_class.begin(),
                                   suggestion.contents_class.end());
      record.description_class.assign(suggestion.description_class.begin(),
                                      suggestion.description_class.end());
      record.text_revision = ++text_revision_;
    }
    if (!record.item)
      record.item = winrt::box_value(static_cast<std::uint32_t>(index));
    if (record.contents != suggestion.contents) {
      record.contents = suggestion.contents;
      record.title = winrt::to_hstring(suggestion.contents);
    }
    if (record.description != suggestion.description) {
      record.description = suggestion.description;
      record.detail = winrt::to_hstring(suggestion.description);
    }
    record.kind = suggestion.kind;
    record.rich_image = suggestion.rich_image;
    record.selected = static_cast<int>(index) == selection;
    record.bitmap = context_.images.Get(suggestion.icon, images);
  }
  const auto items = list_.Items();
  while (items.Size() > records_.size())
    items.RemoveAtEnd();
  while (items.Size() < records_.size())
    items.Append(records_[items.Size()].item);
  for (const auto& [key, row] : rows_)
    UpdateRow(*row);
  const auto visibility = suggestions.empty() ? xaml::Visibility::Collapsed
                                              : xaml::Visibility::Visible;
  if (list_.Visibility() != visibility)
    list_.Visibility(visibility);
  if (list_.SelectedIndex() != selection) {
    list_.SelectedIndex(selection);
    if (selection >= 0)
      list_.ScrollIntoView(records_[selection].item);
  }
}

void SuggestionList::ClearImages() {
  for (auto& record : records_)
    record.bitmap = nullptr;
  for (const auto& [key, row] : rows_)
    if (row->image.Source())
      row->image.Source(nullptr);
}

}  // namespace kr::win
