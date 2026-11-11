#include "krypton/ui/presentation/library.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/layout/horizontal_layout.hpp"
#include "krypton/ui/presentation/view_model.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr {
namespace {
constexpr auto& kLibrary = design::kTokens.library;
constexpr auto& kMediaTokens = design::kTokens.media_controls;

bool RoutingMatches(const LibraryItem& item, std::string_view query) noexcept {
  const auto contains = [&](std::string_view text) {
    return query.empty() ||
           std::search(text.begin(), text.end(), query.begin(), query.end(),
                       [](unsigned char a, unsigned char b) {
                         const auto fold = [](unsigned char c) {
                           return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
                         };
                         return fold(a) == fold(b);
                       }) != text.end();
  };
  return contains(item.title) || contains(item.subtitle) ||
         contains(item.group);
}

[[nodiscard]] float ToLinear(std::uint8_t channel) noexcept {
  const float c = static_cast<float>(channel) / 255.0f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] std::uint8_t FromLinear(float c) noexcept {
  c = std::clamp(c, 0.0f, 1.0f);
  const float encoded =
      c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::lround(encoded * 255.0f));
}

// Keys keep native rows stable across renders: the kind in the top byte, the
// host id (or position, for headings) below it.
[[nodiscard]] std::uint64_t RowKey(LibraryRowKind kind,
                                   std::uint64_t value) noexcept {
  return static_cast<std::uint64_t>(kind) << 56 |
         (value & 0x00FF'FFFF'FFFF'FFFFull);
}

[[nodiscard]] LibraryRowVm ItemRow(const LibraryItem& item) {
  return {.kind = LibraryRowKind::kItem,
          .key = RowKey(LibraryRowKind::kItem, item.id.value),
          .title = item.title,
          .subtitle = item.subtitle,
          .image = item.image,
          .item = item.id,
          .video = item.flags.Has(LibraryItemFlag::kVideo),
          .file = item.flags.Has(LibraryItemFlag::kFile),
          .enabled = item.flags.Has(LibraryItemFlag::kEnabled)};
}
}  // namespace

UiText LibrarySectionText(LibrarySection section) noexcept {
  switch (section) {
    case LibrarySection::kMedia:
      return UiText::kLibraryMedia;
    case LibrarySection::kDownloads:
      return UiText::kLibraryDownloads;
    case LibrarySection::kBoosts:
      return UiText::kLibraryBoosts;
    case LibrarySection::kSpaces:
      return UiText::kLibrarySpaces;
    case LibrarySection::kRouting:
      return UiText::kLibraryRouting;
    case LibrarySection::kHistory:
      break;
  }
  return UiText::kLibraryHistory;
}

Color AccentButtonColor(Color primary, bool dark) noexcept {
  return ClampLightness(
      primary, dark ? kLibrary.accent_dark_min : kLibrary.accent_light_min,
      dark ? kLibrary.accent_dark_max : kLibrary.accent_light_max);
}

Color ClampLightness(Color primary, float low, float high) noexcept {
  // sRGB -> OKLab -> clamp lightness (chroma and hue unchanged) -> sRGB.
  const float r = ToLinear(primary.r()), g = ToLinear(primary.g()),
              b = ToLinear(primary.b());
  const float l =
      std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
  const float m =
      std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
  const float s =
      std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
  float lightness = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
  const float a = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
  const float bb = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
  lightness = std::clamp(lightness, low, high);
  const float l2 = lightness + 0.3963377774f * a + 0.2158037573f * bb;
  const float m2 = lightness - 0.1055613458f * a - 0.0638541728f * bb;
  const float s2 = lightness - 0.0894841775f * a - 1.2914855480f * bb;
  const float l3 = l2 * l2 * l2, m3 = m2 * m2 * m2, s3 = s2 * s2 * s2;
  return Color::FromArgb(
      0xFF,
      FromLinear(4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3),
      FromLinear(-1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3),
      FromLinear(-0.0041960863f * l3 - 0.7034186147f * m3 +
                 1.7076147010f * s3));
}

std::string FormatMediaTime(double seconds) {
  if (!std::isfinite(seconds) || seconds <= 0)
    return "0:00";
  const auto total = static_cast<std::uint64_t>(std::ceil(seconds));
  const auto hours = total / 3600, minutes = total % 3600 / 60,
             rest = total % 60;
  return hours ? std::format("{}:{:02}:{:02}", hours, minutes, rest)
               : std::format("{}:{:02}", minutes, rest);
}

void LibraryPresenter::Present(const Store& store,
                               const ThemeVm& theme,
                               LibraryChanges changed,
                               LibraryVm& out) {
  out.routing_form_max_height = std::max(
      0.0f, store.window().client.height * kLibrary.max_client_fraction -
                kLibrary.header_padding_top - kLibrary.row_height * 2);
  const auto& state = store.library();
  const bool horizontal =
      TabLayoutOf(store.layout_prefs().flags) != TabLayout::kVertical;
  const bool spaces_shown =
      state.open && state.section == LibrarySection::kSpaces;
  if (!changed.library && !changed.theme && !changed.text &&
      horizontal_ == horizontal &&
      !(changed.tabs && (!state.media.empty() || spaces_shown)) &&
      !(changed.spaces && spaces_shown) && revision_ != ~std::uint64_t{0})
    return;
  ++revision_;
  horizontal_ = horizontal;
  PresentDownloads(state, store.text());
  PresentMedia(store);

  const Color primary =
      theme.accent.a() ? theme.accent.WithAlpha(0xFF) : Color{0xFF808080u};
  const Color white{0xFFFFFFFFu}, black{0xFF000000u};
  out.palette = {
      .accent = AccentButtonColor(primary, theme.dark),
      .accent_text = white,
      .card = Mix(theme.dark ? black : white, primary,
                  kMediaTokens.card_primary_mix),
      .card_outline =
          theme.dark ? Color{kMediaTokens.card_outline_dark} : Color{},
      .badge = Mix(primary, theme.dark ? Color{0x80000000u} : white, 0.8f),
      .badge_track =
          Mix(primary, theme.dark ? white : Color{0xCC000000u}, 0.7f),
      .progress = Mix(primary, theme.dark ? white : black, 0.3f),
      .opt_in =
          Color{theme.dark ? kLibrary.opt_in_dark : kLibrary.opt_in_light},
      .opt_in_border = Color{theme.dark ? kLibrary.opt_in_border_dark
                                        : kLibrary.opt_in_border_light},
  };
  out.palette.icon = ClampLightness(
      primary, theme.dark ? kLibrary.icon_dark_min : kLibrary.icon_light_min,
      theme.dark ? kLibrary.icon_dark_max : kLibrary.icon_light_max);
  out.palette.icon_fill =
      Mix(out.palette.icon, theme.dark ? black : white, kLibrary.icon_fill_mix);
  const Color text{theme.palette->text};
  const auto mix = [&](float alpha) {
    return text.WithAlpha(static_cast<std::uint8_t>(
        std::lround(alpha * static_cast<float>(text.a()))));
  };
  out.palette.pill = mix(kLibrary.pill_alpha);
  out.palette.pill_hover = mix(kLibrary.pill_hover_alpha);
  out.palette.tab_hover = mix(kLibrary.tab_hover_alpha);
  out.palette.tab_active = mix(kLibrary.tab_active_alpha);

  const auto& stack = design::kTokens.download_stack;
  const auto shown =
      std::min(downloads_.size(), static_cast<std::size_t>(stack.max_entries));
  out.stack = std::span{downloads_}.last(shown);
  out.downloading = !downloads_.empty() && downloads_.back().pending;
  out.download_start_serial = state.download_start_serial;
  out.download_progress = out.downloading ? downloads_.back().progress : 0;
  out.media = media_;
  out.open = state.open;
  out.section = state.section;
  out.routing_query = state.routing_query;
  out.routing_editor = &state.routing_editor;
  sections_.clear();
  for (const auto section :
       {LibrarySection::kRouting, LibrarySection::kMedia,
        LibrarySection::kDownloads, LibrarySection::kBoosts,
        LibrarySection::kSpaces, LibrarySection::kHistory})
    if (!state.Hidden(section))
      sections_.push_back(section);
  out.sections = sections_;
  PresentSection(store, out);
  out.rows = rows_;
  out.tiles = tiles_;
  out.revision = revision_;
}

void LibraryPresenter::PresentDownloads(const LibraryState& state,
                                        UiTextView text) {
  downloads_.clear();
  for (const auto& download : state.downloads) {
    if (download.state == DownloadState::kComplete &&
        !download.flags.Has(DownloadFlag::kFileExists))
      continue;
    const bool running = download.state == DownloadState::kInProgress;
    const bool known = download.flags.Has(DownloadFlag::kProgressKnown);
    downloads_.push_back({
        .id = download.id,
        .file_name = download.file_name,
        .status = running && download.flags.Has(DownloadFlag::kOpenWhenDone)
                      ? text[UiText::kDownloadOpenWhenDone]
                      : std::string_view(download.status),
        .icon = download.icon,
        .progress = known ? download.progress : 0,
        .pending = download.Pending(),
        .progress_known = known,
        .can_cancel = running,
        .can_show = download.state == DownloadState::kComplete &&
                    download.flags.Has(DownloadFlag::kFileExists),
        .can_open = download.state != DownloadState::kInProgress ||
                    download.flags.Has(DownloadFlag::kBlocked),
    });
  }
}

void LibraryPresenter::PresentMedia(const Store& store) {
  media_.clear();
  const auto limit = static_cast<std::size_t>(kMediaTokens.max_cards);
  const auto& entries = store.library().media;
  // Newest media sits at the front of the stack.
  for (auto it = entries.rbegin();
       it != entries.rend() && media_.size() < limit; ++it) {
    const auto& value = it->value;
    const bool sharing = value.flags.Has(MediaFlag::kSharing);
    if (!it->revealed ||
        (!sharing && (value.flags.Has(MediaFlag::kPictureInPicture) ||
                      (!horizontal_ && value.tab == store.active_tab()))))
      continue;
    ImageId icon = value.icon;
    if (!icon)
      if (const auto* tab = store.FindTab(value.tab))
        icon = tab->favicon;
    const auto has = [&](MediaFlag flag) { return value.flags.Has(flag); };
    media_.push_back({
        .id = value.id,
        .tab = value.tab,
        .title = value.title,
        .artist = value.artist,
        .icon = icon,
        .position = value.position,
        .duration = value.duration,
        .playback_rate = value.playback_rate,
        .playing = has(MediaFlag::kPlaying),
        .muted = has(MediaFlag::kMuted),
        .sharing = sharing,
        .can_play_pause = has(MediaFlag::kCanPlayPause),
        .can_previous = has(MediaFlag::kCanPrevious),
        .can_next = has(MediaFlag::kCanNext),
        .can_seek = has(MediaFlag::kCanSeek),
        .can_picture_in_picture = has(MediaFlag::kCanPictureInPicture),
        .microphone_muted = has(MediaFlag::kMicrophoneMuted),
        .camera_muted = has(MediaFlag::kCameraMuted),
        .can_toggle_microphone = has(MediaFlag::kCanToggleMicrophone),
        .can_toggle_camera = has(MediaFlag::kCanToggleCamera),
        .position_visible =
            !sharing && value.duration < kMediaTokens.hidden_position_seconds,
    });
  }
}

void LibraryPresenter::PresentSection(const Store& store, LibraryVm& out) {
  rows_.clear();
  tiles_.clear();
  out.media_opt_in = false;
  out.loading = false;
  out.empty = UiText::kCount;
  const auto& state = store.library();
  if (!state.open)
    return;
  const auto flags = state.sections[SectionIndex(state.section)];
  out.loading = flags.Has(LibrarySectionFlag::kLoading);
  const auto& items = state.items[SectionIndex(state.section)];
  switch (state.section) {
    case LibrarySection::kMedia:
      if (!flags.Has(LibrarySectionFlag::kEnabled)) {
        out.media_opt_in = true;
        out.loading = false;
        return;
      }
      for (const auto& item : items)
        tiles_.push_back(ItemRow(item));
      if (out.loading)
        out.empty = UiText::kLibraryMediaLoading;
      else if (tiles_.empty())
        out.empty = UiText::kLibraryMediaEmpty;
      return;
    case LibrarySection::kDownloads:
      for (auto it = downloads_.rbegin(); it != downloads_.rend(); ++it)
        rows_.push_back({.kind = LibraryRowKind::kDownload,
                         .key = RowKey(LibraryRowKind::kDownload, it->id.value),
                         .title = it->file_name,
                         .subtitle = it->status,
                         .image = it->icon,
                         .download = &*it});
      if (rows_.empty())
        out.empty = UiText::kLibraryDownloadsEmpty;
      return;
    case LibrarySection::kSpaces:
      for (const auto& space : store.spaces()) {
        rows_.push_back({.kind = LibraryRowKind::kSpace,
                         .key = RowKey(LibraryRowKind::kSpace, space.id.value),
                         .title = space.name,
                         .glyph = space.glyph,
                         .color = space.color,
                         .space = space.id,
                         .active = space.id == store.active_space()});
        for (const auto& tab : store.tabs())
          if (tab.space == space.id && !tab.flags.Has(TabFlag::kFavorite))
            rows_.push_back({.kind = LibraryRowKind::kTab,
                             .key = RowKey(LibraryRowKind::kTab, tab.id.value),
                             .title = tab.title,
                             .subtitle = tab.url,
                             .image = tab.favicon,
                             .tab = tab.id,
                             .active = tab.id == store.active_tab()});
      }
      return;
    case LibrarySection::kRouting:
      for (const auto& item : items)
        if (RoutingMatches(item, state.routing_query))
          rows_.push_back(ItemRow(item));
      if (rows_.empty())
        out.empty = state.routing_query.empty()
                        ? UiText::kLibraryRoutingEmpty
                        : UiText::kLibraryRoutingNoResults;
      return;
    case LibrarySection::kBoosts:
    case LibrarySection::kHistory: {
      std::string_view group;
      std::uint64_t headings = 0;
      for (const auto& item : items) {
        if (!item.group.empty() && item.group != group) {
          rows_.push_back({.kind = LibraryRowKind::kHeading,
                           .key = RowKey(LibraryRowKind::kHeading, headings++),
                           .title = item.group});
        }
        group = item.group;
        rows_.push_back(ItemRow(item));
      }
      if (rows_.empty() && !out.loading)
        out.empty = state.section == LibrarySection::kBoosts
                        ? UiText::kLibraryBoostsEmpty
                        : UiText::kLibraryHistoryEmpty;
      return;
    }
  }
}
}  // namespace kr
