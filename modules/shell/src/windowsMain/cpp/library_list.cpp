#include "library_list.hpp"

#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>

#include <algorithm>
#include <cmath>

#include "icon_view.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "native_theme.hpp"

namespace kr::win {
namespace {
constexpr auto& kLibrary = design::kTokens.library;

void Show(const xaml::UIElement& element, bool visible) {
  const auto value =
      visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
  if (element.Visibility() != value)
    element.Visibility(value);
}
}  // namespace

struct LibraryList::Row {
  std::uint64_t token = 0;
  controls::Primitives::SelectorItem container{nullptr};
  controls::Grid root{nullptr};
  // Created on first use for the kinds this container shows.
  controls::TextBlock heading{nullptr};
  controls::Grid item{nullptr};
  controls::Image image{nullptr};
  controls::TextBlock glyph{nullptr}, title{nullptr}, subtitle{nullptr};
  controls::Border tile{nullptr}, play{nullptr};
  controls::Image picture{nullptr};
  std::unique_ptr<DownloadEntry> download;
  std::unique_ptr<IconView> play_icon;
  xaml::UIElement::RightTapped_revoker context;
};

LibraryList::LibraryList(ViewContext& context, bool tiles, Bounds bounds)
    : context_(context), tiles_(tiles), bounds_(std::move(bounds)) {
  list_ = tiles ? controls::ListViewBase(controls::GridView())
                : controls::ListViewBase(controls::ListView());
  list_.IsItemClickEnabled(true);
  list_.SelectionMode(controls::ListViewSelectionMode::None);
  list_.Background(context.brushes.transparent);
  list_.Padding(tiles ? UniformThickness(kLibrary.grid_padding)
                      : xaml::Thickness{kLibrary.grid_padding, 0,
                                        kLibrary.grid_padding,
                                        kLibrary.grid_padding});
  controls::ScrollViewer::SetHorizontalScrollBarVisibility(
      list_, controls::ScrollBarVisibility::Disabled);
  const auto resources = list_.Resources();
  for (const auto* key :
       {L"ListViewItemBackgroundPointerOver", L"ListViewItemBackgroundPressed",
        L"GridViewItemBackgroundPointerOver", L"GridViewItemBackgroundPressed"})
    resources.Insert(
        winrt::box_value(winrt::hstring(key)),
        tiles ? context.brushes.transparent : context.brushes.hover);
  list_.ItemTemplate(xaml::Markup::XamlReader::Load(LR"(
    <DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
      <Grid/>
    </DataTemplate>)")
                         .as<xaml::DataTemplate>());
  items_ = winrt::single_threaded_observable_vector<IInspectable>();
  list_.ItemsSource(items_);
  clicked_ = list_.ItemClick(
      winrt::auto_revoke,
      [this](auto&&, const controls::ItemClickEventArgs& args) {
        const auto token =
            winrt::unbox_value<std::uint64_t>(args.ClickedItem());
        if (const auto* record = FindRecord(token))
          Activate(*record);
      });
  realize_ = [this, weak = std::weak_ptr<int>(lifetime_)](
                 const controls::ListViewBase&,
                 const controls::ContainerContentChangingEventArgs& args) {
    if (weak.expired())
      return;
    try {
      const auto container =
          args.ItemContainer().as<controls::Primitives::SelectorItem>();
      const auto key = winrt::get_abi(container);
      if (args.InRecycleQueue()) {
        if (const auto found = rows_.find(key); found != rows_.end())
          found->second->token = 0;
        return;
      }
      const auto root =
          container.ContentTemplateRoot().try_as<controls::Grid>();
      if (!root) {
        if (args.Phase() < 2)
          args.RegisterUpdateCallback(args.Phase() + 1, realize_);
        return;
      }
      auto& row = rows_[key];
      if (!row) {
        row = std::make_unique<Row>();
        row->container = container;
        row->root = root;
        container.HorizontalContentAlignment(
            xaml::HorizontalAlignment::Stretch);
        container.Padding(UniformThickness(0));
        container.MinHeight(0);
        container.CornerRadius(UniformCorners(kLibrary.row_radius));
        if (tiles_)
          container.Margin(UniformThickness(kLibrary.grid_gap / 2));
        row->context = container.RightTapped(
            winrt::auto_revoke,
            [this, native = row.get()](
                auto&&, const xaml::Input::RightTappedRoutedEventArgs& args) {
              const auto* current = FindRecord(native->token);
              if (!current)
                return;
              const auto& record = *current;
              const auto anchor = bounds_(native->container);
              if (record.kind == LibraryRowKind::kItem)
                context_.request_menu(MenuKind::kLibraryItem, record.item.value,
                                      anchor);
              else if (record.kind == LibraryRowKind::kTab)
                context_.request_menu(MenuKind::kTab, record.tab.value, anchor);
              else if (record.kind == LibraryRowKind::kSpace)
                context_.request_menu(MenuKind::kSpace, record.space.value,
                                      anchor);
              else
                return;
              args.Handled(true);
            });
      }
      row->token = winrt::unbox_value<std::uint64_t>(args.Item());
      UpdateRow(*row);
      args.Handled(true);
    } catch (...) {
      platform::WriteDefaultLog(LogLevel::kWarning,
                                "Could not realize a library row");
    }
  };
  containers_ = list_.ContainerContentChanging(winrt::auto_revoke, realize_);
}

LibraryList::~LibraryList() {
  lifetime_.reset();
  containers_.revoke();
}

void LibraryList::SetWidth(double width) {
  if (!tiles_ || width <= 0)
    return;
  // Zen's grid: repeat(auto-fill, minmax(110px, 1fr)) with 12px gaps.
  const double inner = width - 2 * kLibrary.grid_padding;
  const double columns =
      std::max(1.0, std::floor((inner + kLibrary.grid_gap) /
                               (kLibrary.grid_tile + kLibrary.grid_gap)));
  const double tile =
      std::floor((inner - (columns - 1) * kLibrary.grid_gap) / columns);
  if (tile == tile_)
    return;
  tile_ = tile;
  for (auto& [key, row] : rows_)
    if (row->tile) {
      row->tile.Width(tile_);
      row->tile.Height(tile_);
    }
}

std::size_t LibraryList::RecordKeyHash::operator()(
    RecordKey key) const noexcept {
  const auto kind = static_cast<std::size_t>(key.kind);
  const auto id = std::hash<std::uint64_t>{}(key.id);
  return id ^ (kind + 0x9E3779B9u + (id << 6) + (id >> 2));
}

LibraryList::RecordKey LibraryList::KeyFor(const LibraryRowVm& row) noexcept {
  switch (row.kind) {
    case LibraryRowKind::kDownload:
      return {row.kind, row.download ? row.download->id.value : row.key};
    case LibraryRowKind::kItem:
      return {row.kind, row.item.value};
    case LibraryRowKind::kSpace:
      return {row.kind, row.space.value};
    case LibraryRowKind::kTab:
      return {row.kind, row.tab.value};
    case LibraryRowKind::kHeading:
      return {row.kind, row.key};
  }
  return {row.kind, row.key};
}

LibraryList::Record* LibraryList::FindRecord(std::uint64_t token) noexcept {
  const auto found = tokens_.find(token);
  return found == tokens_.end() ? nullptr : found->second;
}

void LibraryList::Render(std::span<const LibraryRowVm> rows,
                         LibrarySection section,
                         const LibraryPaletteVm& palette,
                         const ImageStore* images,
                         UiTextView text) {
  section_ = section;
  palette_ = palette;
  images_ = images;
  text_ = text;
  ++generation_;
  desired_.clear();
  desired_.reserve(rows.size());
  const auto update_text = [](std::string_view value, std::string& owned,
                              winrt::hstring& native) {
    if (owned == value)
      return;
    owned.assign(value);
    native = winrt::to_hstring(value);
  };
  for (const auto& row : rows) {
    const auto [found, inserted] = records_.try_emplace(KeyFor(row));
    auto& record = found->second;
    if (inserted) {
      record.token = ++next_token_;
      record.native_item = winrt::box_value(record.token);
      tokens_.emplace(record.token, &record);
    }
    record.seen = generation_;
    record.kind = row.kind;
    update_text(row.title, record.title_text, record.title);
    update_text(row.subtitle, record.subtitle_text, record.subtitle);
    update_text(row.glyph, record.glyph_text, record.glyph);
    record.image = row.image;
    record.color = row.color;
    record.item = row.item;
    record.space = row.space;
    record.tab = row.tab;
    record.active = row.active;
    record.video = row.video;
    record.file = row.file;
    record.enabled = row.enabled;
    if (row.download) {
      record.file_name = row.download->file_name;
      record.status = row.download->status;
      record.download = *row.download;
      record.download.file_name = record.file_name;
      record.download.status = record.status;
    }
    desired_.push_back(record.token);
  }

  // Drop obsolete keys first, so one removal does not move every later row.
  for (std::size_t index = displayed_.size(); index-- > 0;) {
    const auto* record = FindRecord(displayed_[index]);
    if (!record || record->seen != generation_) {
      items_.RemoveAt(static_cast<std::uint32_t>(index));
      displayed_.erase(displayed_.begin() + index);
    }
  }
  for (std::size_t index = 0; index < desired_.size(); ++index) {
    if (index < displayed_.size() && displayed_[index] == desired_[index])
      continue;
    const auto existing =
        std::find(displayed_.begin() + std::min(index, displayed_.size()),
                  displayed_.end(), desired_[index]);
    if (existing == displayed_.end()) {
      const auto* record = FindRecord(desired_[index]);
      items_.InsertAt(static_cast<std::uint32_t>(index), record->native_item);
      displayed_.insert(displayed_.begin() + index, desired_[index]);
      continue;
    }

    // A row moved toward the end should cost one move, rather than moving
    // each row in the intervening range toward the front.
    const auto target = std::find(desired_.begin() + index + 1, desired_.end(),
                                  displayed_[index]);
    const auto target_index =
        static_cast<std::size_t>(target - desired_.begin());
    if (existing == displayed_.begin() + index + 1 &&
        target_index < displayed_.size() &&
        std::equal(displayed_.begin() + index + 1,
                   displayed_.begin() + target_index + 1,
                   desired_.begin() + index)) {
      const auto token = displayed_[index];
      const auto* record = FindRecord(token);
      items_.RemoveAt(static_cast<std::uint32_t>(index));
      items_.InsertAt(static_cast<std::uint32_t>(target_index),
                      record->native_item);
      std::rotate(displayed_.begin() + index, displayed_.begin() + index + 1,
                  displayed_.begin() + target_index + 1);
    } else {
      const auto existing_index =
          static_cast<std::uint32_t>(existing - displayed_.begin());
      const auto* record = FindRecord(*existing);
      items_.RemoveAt(existing_index);
      items_.InsertAt(static_cast<std::uint32_t>(index), record->native_item);
      std::rotate(displayed_.begin() + index, existing, existing + 1);
    }
  }
  std::erase_if(records_, [this](const auto& pair) {
    if (pair.second.seen == generation_)
      return false;
    tokens_.erase(pair.second.token);
    return true;
  });
  for (auto& [key, row] : rows_)
    if (row->token)
      UpdateRow(*row);
}

void LibraryList::UpdateRow(Row& row) {
  const auto* current = FindRecord(row.token);
  if (!current)
    return;
  const auto& record = *current;
  const bool heading = record.kind == LibraryRowKind::kHeading;
  const bool download = record.kind == LibraryRowKind::kDownload;
  const bool item = !heading && !download && !tiles_;
  row.container.IsHitTestVisible(!heading);
  if (heading && !row.heading) {
    row.heading = Label(L"", kLibrary.group_size);
    row.heading.FontWeight(kSemiBold);
    row.heading.Opacity(kLibrary.empty_opacity);
    row.heading.Foreground(context_.brushes.text);
    row.heading.Margin({kLibrary.row_padding_inline, kLibrary.row_gap,
                        kLibrary.row_padding_inline, 4});
    row.root.Children().Append(row.heading);
  }
  if (download && !row.download) {
    row.download = std::make_unique<DownloadEntry>(context_, true, bounds_);
    row.download->SetText(text_);
    row.root.Children().Append(row.download->Root());
  }
  if (item && !row.item) {
    row.item = controls::Grid();
    row.item.MinHeight(kLibrary.row_height);
    row.item.Padding(
        {kLibrary.row_padding_inline, 4, kLibrary.row_padding_inline, 4});
    row.item.ColumnSpacing(kLibrary.row_gap);
    AddColumn(row.item, kLibrary.row_icon_size, xaml::GridUnitType::Pixel);
    AddColumn(row.item, 1, xaml::GridUnitType::Star);
    row.image = controls::Image();
    row.image.Width(kLibrary.row_icon_size);
    row.image.Height(kLibrary.row_icon_size);
    row.glyph = Glyph(L"", kLibrary.row_icon_size);
    controls::StackPanel text;
    text.VerticalAlignment(xaml::VerticalAlignment::Center);
    row.title = Label(L"", kLibrary.title_size - 1);
    row.title.Foreground(context_.brushes.text);
    row.subtitle = Label(L"", kTokens.type.size_small);
    row.subtitle.Foreground(context_.brushes.text);
    row.subtitle.Opacity(kLibrary.empty_opacity);
    text.Children().Append(row.title);
    text.Children().Append(row.subtitle);
    controls::Grid::SetColumn(text, 1);
    row.item.Children().Append(row.image);
    row.item.Children().Append(row.glyph);
    row.item.Children().Append(text);
    row.root.Children().Append(row.item);
  }
  if (tiles_ && !row.tile) {
    row.tile = controls::Border();
    row.tile.CornerRadius(UniformCorners(kLibrary.tile_radius));
    row.tile.Padding(UniformThickness(kLibrary.tile_padding));
    row.tile.Width(tile_);
    row.tile.Height(tile_);
    controls::Grid frame;
    row.picture = controls::Image();
    row.picture.Stretch(media::Stretch::Uniform);
    frame.Children().Append(row.picture);
    row.play = controls::Border();
    row.play.Width(kLibrary.play_badge);
    row.play.Height(kLibrary.play_badge);
    row.play.CornerRadius(UniformCorners(kLibrary.play_badge / 2));
    row.play.Padding(UniformThickness(kLibrary.play_padding));
    row.play.Background(context_.brushes.For(Color{0x73000000u}));
    row.play.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    row.play.VerticalAlignment(xaml::VerticalAlignment::Center);
    row.play_icon = std::make_unique<IconView>(
        context_.icons, IconRole::kPlay,
        kLibrary.play_badge - 2 * kLibrary.play_padding,
        context_.brushes.For(Color{0xFFFFFFFFu}));
    row.play.Child(row.play_icon->Root());
    frame.Children().Append(row.play);
    row.tile.Child(frame);
    row.root.Children().Append(row.tile);
  }
  if (row.heading)
    Show(row.heading, heading);
  if (row.download)
    Show(row.download->Root(), download);
  if (row.item)
    Show(row.item, item);
  if (heading) {
    if (row.heading.Text() != record.title)
      row.heading.Text(record.title);
    return;
  }
  if (download) {
    auto value = record.download;
    value.file_name = record.file_name;
    value.status = record.status;
    row.download->Update(value, palette_,
                         value.icon ? context_.images.Get(value.icon, images_)
                                    : media::ImageSource{nullptr});
    return;
  }
  const auto picture = record.image ? context_.images.Get(record.image, images_)
                                    : media::ImageSource{nullptr};
  Accessible(row.container, record.title);
  if (tiles_) {
    row.tile.Background(context_.brushes.For(palette_.pill));
    row.picture.Source(picture);
    Show(row.play, record.video);
    SetToolTip(row.container, record.title);
    return;
  }
  if (row.title.Text() != record.title)
    row.title.Text(record.title);
  if (row.subtitle.Text() != record.subtitle)
    row.subtitle.Text(record.subtitle);
  Show(row.subtitle, !record.subtitle.empty());
  const bool space = record.kind == LibraryRowKind::kSpace;
  row.title.FontWeight(space || record.active ? kSemiBold : kMedium);
  row.image.Source(picture);
  Show(row.image, static_cast<bool>(picture));
  Show(row.glyph, !picture);
  if (!picture) {
    const auto glyph = !record.glyph.empty() ? record.glyph
                       : space ? winrt::hstring(library_glyphs::kSpaces)
                               : winrt::hstring(LibrarySectionGlyph(section_));
    if (row.glyph.Text() != glyph)
      row.glyph.Text(glyph);
    row.glyph.Foreground(record.color.a() ? context_.brushes.For(record.color)
                                          : context_.brushes.icon);
  }
  row.item.Opacity(record.kind == LibraryRowKind::kItem &&
                           (section_ == LibrarySection::kBoosts ||
                            section_ == LibrarySection::kRouting) &&
                           !record.enabled
                       ? kLibrary.empty_opacity
                       : 1.0);
  row.container.Background(record.active ? context_.brushes.active
                                         : context_.brushes.transparent);
}

void LibraryList::Activate(const Record& record) {
  switch (record.kind) {
    case LibraryRowKind::kItem:
      context_.delegate.OnLibraryCommand(section_, record.item,
                                         section_ == LibrarySection::kBoosts
                                             ? LibraryCommand::kToggle
                                             : LibraryCommand::kOpen);
      return;
    case LibraryRowKind::kSpace:
      context_.delegate.OnSpaceSelected(record.space);
      return;
    case LibraryRowKind::kTab:
      context_.delegate.OnTabActivated(record.tab);
      return;
    case LibraryRowKind::kHeading:
    case LibraryRowKind::kDownload:  // the entry handles its own clicks
      return;
  }
}
}  // namespace kr::win
