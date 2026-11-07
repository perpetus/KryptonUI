#include "workspace_icon_picker.hpp"

#include <winrt/Microsoft.UI.Xaml.Markup.h>

#include "krypton/ui/model/workspace_symbols.hpp"

namespace kr::win {
namespace {
bool IsIcon(const winrt::hstring& text) {
  return text.size() == 1 && text[0] >= 0xE000 && text[0] <= 0xF8FF;
}
}  // namespace
WorkspaceIconPicker::WorkspaceIconPicker(ViewContext& context, bool allow_empty)
    : context_(context), allow_empty_(allow_empty) {
  const auto& t = kTokens.workspace_picker;
  root_.Spacing(t.gap);
  header_ = Label(L"", kTokens.type.size);
  controls::Grid face;
  face.ColumnSpacing(t.gap);
  AddColumn(face, 1, xaml::GridUnitType::Star);
  AddColumn(face, 0, xaml::GridUnitType::Auto);
  preview_glyph_ = Glyph(L"", t.icon_size);
  face.Children().Append(preview_glyph_);
  auto arrow = Glyph(L"\uE70D", t.chevron_size);
  controls::Grid::SetColumn(arrow, 1);
  face.Children().Append(arrow);
  preview_.Content(face);
  preview_.Width(t.preview_width);
  preview_.Height(t.preview_height);
  preview_.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  root_.Children().Append(header_);
  root_.Children().Append(preview_);
  opened_ = preview_.Click(winrt::auto_revoke, [this](auto&&, auto&&) {
    EnsurePopup();
    Filter();
    flyout_.ShowAt(preview_);
  });
  SetText({});
  SetValue(allow_empty_ ? std::string_view{} : kDefaultWorkspaceSymbol);
}
WorkspaceIconPicker::~WorkspaceIconPicker() {
  lifetime_.reset();
  Close();
}
void WorkspaceIconPicker::Close() noexcept {
  try {
    if (flyout_)
      flyout_.Hide();
  } catch (...) {
  }
}

void WorkspaceIconPicker::SetText(UiTextView text) {
  SetLabels({NativeText(UiText::kWorkspaceGlyph, text),
             NativeText(UiText::kWorkspaceChooseIcon, text),
             NativeText(UiText::kWorkspaceSearchIcons, text),
             NativeText(UiText::kWorkspaceIcons, text),
             NativeText(UiText::kWorkspaceEmoji, text),
             NativeText(UiText::kWorkspaceCustomEmoji, text)});
}
void WorkspaceIconPicker::SetLabels(std::array<winrt::hstring, 6> labels) {
  labels_ = std::move(labels);
  header_.Text(labels_[0]);
  Accessible(preview_, labels_[1]);
  SetToolTip(preview_, labels_[1]);
  if (search_)
    search_.PlaceholderText(labels_[2]);
  if (custom_)
    custom_.Header(winrt::box_value(labels_[5]));
  if (mode_) {
    changing_ = true;
    const auto selected = mode_.SelectedIndex();
    mode_.Items().Clear();
    mode_.Items().Append(winrt::box_value(labels_[3]));
    mode_.Items().Append(winrt::box_value(labels_[4]));
    mode_.SelectedIndex(selected);
    changing_ = false;
  }
}
void WorkspaceIconPicker::SetValue(std::string_view value) {
  value_ = value.empty() && !allow_empty_ ? std::string(kDefaultWorkspaceSymbol)
                                          : std::string(value);
  const auto glyph = winrt::to_hstring(
      value_.empty() ? std::string_view{"\uE8B7"} : std::string_view{value_});
  preview_glyph_.Text(glyph);
  preview_glyph_.Foreground(context_.brushes.icon);
  preview_glyph_.FontFamily(media::FontFamily(winrt::to_hstring(
      IsIcon(glyph) ? kTokens.type.icon_family
                    : kTokens.workspace_picker.emoji_family)));
  const auto mode = IsIcon(glyph) ? 0 : 1;
  if (mode_ && mode_.SelectedIndex() != mode) {
    changing_ = true;
    mode_.SelectedIndex(mode);
    changing_ = false;
    Filter();
  }
  if (custom_ && !IsIcon(glyph) && custom_.Text() != glyph) {
    changing_ = true;
    custom_.Text(glyph);
    changing_ = false;
  }
}
void WorkspaceIconPicker::EnsurePopup() {
  if (flyout_)
    return;
  const auto& t = kTokens.workspace_picker;
  flyout_ = controls::Flyout();
  controls::StackPanel content;
  content.Width(t.width);
  content.Spacing(t.gap);
  search_ = controls::TextBox();
  search_.PlaceholderText(labels_[2]);
  mode_ = controls::RadioButtons();
  mode_.MaxColumns(2);
  mode_.Items().Append(winrt::box_value(labels_[3]));
  mode_.Items().Append(winrt::box_value(labels_[4]));
  mode_.SelectedIndex(IsIcon(winrt::to_hstring(value_)) ? 0 : 1);
  grid_ = controls::GridView();
  grid_.MaxHeight(t.max_height);
  grid_.SelectionMode(controls::ListViewSelectionMode::None);
  grid_.IsItemClickEnabled(true);
  grid_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  grid_.ItemTemplate(
      xaml::Markup::XamlReader::Load(
          LR"(<DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"><TextBlock HorizontalAlignment="Center" VerticalAlignment="Center" IsHitTestVisible="False" /></DataTemplate>)")
          .as<xaml::DataTemplate>());
  items_ = winrt::single_threaded_observable_vector<IInspectable>();
  grid_.ItemsSource(items_);
  realize_ = [this, weak = std::weak_ptr<int>(lifetime_)](
                 auto&&,
                 const controls::ContainerContentChangingEventArgs& args) {
    if (weak.expired() || args.InRecycleQueue())
      return;
    const auto index = winrt::unbox_value<std::uint32_t>(args.Item());
    if (index >= kWorkspaceSymbols.size())
      return;
    const auto& symbol = kWorkspaceSymbols[index];
    const auto container = args.ItemContainer();
    container.Width(kTokens.workspace_picker.cell_size);
    container.Height(kTokens.workspace_picker.cell_size);
    container.Padding(UniformThickness(0));
    container.Margin(UniformThickness(0));
    const auto glyph =
        container.ContentTemplateRoot().try_as<controls::TextBlock>();
    if (!glyph) {
      if (args.Phase() < 2)
        args.RegisterUpdateCallback(args.Phase() + 1, realize_);
      return;
    }
    glyph.Text(winrt::to_hstring(symbol.value));
    glyph.FontSize(kTokens.workspace_picker.icon_size);
    glyph.Foreground(context_.brushes.icon);
    glyph.FontFamily(media::FontFamily(winrt::to_hstring(
        symbol.icon ? kTokens.type.icon_family
                    : kTokens.workspace_picker.emoji_family)));
    glyph.IsHitTestVisible(false);
    glyph.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    glyph.VerticalAlignment(xaml::VerticalAlignment::Center);
    Accessible(container, winrt::to_hstring(symbol.label));
    controls::ToolTipService::SetToolTip(
        container, winrt::box_value(winrt::to_hstring(symbol.label)));
    args.Handled(true);
  };
  containers_ = grid_.ContainerContentChanging(winrt::auto_revoke, realize_);
  selected_ = grid_.ItemClick(
      winrt::auto_revoke,
      [this](auto&&, const controls::ItemClickEventArgs& args) {
        const auto index =
            winrt::unbox_value<std::uint32_t>(args.ClickedItem());
        if (index >= kWorkspaceSymbols.size())
          return;
        SetValue(kWorkspaceSymbols[index].value);
        flyout_.Hide();
      });
  custom_ = controls::TextBox();
  custom_.Header(winrt::box_value(labels_[5]));
  custom_.FontFamily(media::FontFamily(winrt::to_hstring(t.emoji_family)));
  content.Children().Append(search_);
  content.Children().Append(mode_);
  content.Children().Append(grid_);
  content.Children().Append(custom_);
  flyout_.Content(content);
  searched_ = search_.TextChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (!changing_)
      Filter();
  });
  mode_changed_ =
      mode_.SelectionChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
        if (!changing_)
          Filter();
      });
  edited_ = custom_.TextChanged(winrt::auto_revoke, [this](auto&&, auto&&) {
    if (!changing_)
      SetValue(winrt::to_string(custom_.Text()));
  });
  SetValue(value_);
}
void WorkspaceIconPicker::Filter() {
  if (!grid_)
    return;
  const auto query = winrt::to_string(search_.Text());
  const bool icons = mode_.SelectedIndex() == 0;
  custom_.Visibility(icons ? xaml::Visibility::Collapsed
                           : xaml::Visibility::Visible);
  filtered_.clear();
  if (boxed_.size() != kWorkspaceSymbols.size())
    boxed_.resize(kWorkspaceSymbols.size());
  for (std::size_t i = 0; i < kWorkspaceSymbols.size(); ++i) {
    const auto& symbol = kWorkspaceSymbols[i];
    if (symbol.icon == icons && WorkspaceSymbolMatches(symbol, query)) {
      if (!boxed_[i])
        boxed_[i] = winrt::box_value(static_cast<std::uint32_t>(i));
      filtered_.push_back(boxed_[i]);
    }
  }
  items_.ReplaceAll(filtered_);
}
}  // namespace kr::win
