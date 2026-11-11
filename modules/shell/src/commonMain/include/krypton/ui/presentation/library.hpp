#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/model/library.hpp"
#include "krypton/ui/model/ui_text.hpp"

namespace kr {
class Store;
struct ThemeVm;

struct DownloadVm {
  DownloadId id;
  std::string_view file_name;
  std::string_view status;  // "Opens when finished" replaces the host's
  ImageId icon;
  float progress = 0;
  bool pending = false;  // badge draws the progress ring
  bool progress_known = false;
  bool can_cancel = false;
  bool can_show = false;
  bool can_open = false;
};

struct MediaCardVm {
  MediaId id;
  TabId tab;
  std::string_view title, artist;
  ImageId icon;
  double position = 0, duration = 0, playback_rate = 1;
  bool playing = false, muted = false, sharing = false;
  bool can_play_pause = false, can_previous = false, can_next = false;
  bool can_seek = false, can_picture_in_picture = false;
  bool microphone_muted = false, camera_muted = false;
  bool can_toggle_microphone = false, can_toggle_camera = false;
  bool position_visible = false;
};

enum class LibraryRowKind : std::uint8_t {
  kHeading,
  kDownload,
  kItem,
  kSpace,
  kTab
};

// One row or tile of the open section, keyed so native rows can be reused.
struct LibraryRowVm {
  LibraryRowKind kind = LibraryRowKind::kItem;
  std::uint64_t key = 0;
  std::string_view title, subtitle, glyph;
  ImageId image;
  Color color;
  const DownloadVm* download = nullptr;
  LibraryItemId item;
  SpaceId space;
  TabId tab;
  bool active = false, video = false, file = false, enabled = false;
};

// Theme-derived colors, following Zen's color-mix() rules on the primary
// (workspace) color.
struct LibraryPaletteVm {
  Color accent;       // .zen-big-accent-button.primary background
  Color accent_text;  // white
  Color card;         // --zen-sidebar-notification-bg
  Color card_outline;
  Color badge;        // pending download badge fill
  Color badge_track;  // pending download badge border and ring track
  Color progress;     // media progress and thumb
  Color opt_in;
  Color opt_in_border;
  Color icon;       // --zen-accent-button-color: sprite strokes
  Color icon_fill;  // active sprite fills
  // currentColor mixes: tiles, tiles on hover, rail tabs on hover, active.
  Color pill, pill_hover, tab_hover, tab_active;
};

struct LibraryVm {
  bool open = false;
  LibrarySection section = LibrarySection::kHistory;
  std::span<const LibrarySection> sections;  // rail, in Zen's order
  std::span<const DownloadVm> stack;         // newest four, oldest first
  bool downloading = false;                  // the newest download is pending
  float download_progress = 0;
  std::uint64_t download_start_serial = 0;
  std::span<const MediaCardVm> media;   // shown cards, front first
  std::span<const LibraryRowVm> rows;   // list sections
  std::span<const LibraryRowVm> tiles;  // media grid
  bool media_opt_in = false;            // media section asks to be enabled
  bool loading = false;
  UiText empty = UiText::kCount;  // kCount: the section has content
  std::string_view routing_query;
  const RoutingEditorState* routing_editor = nullptr;
  float routing_form_max_height = 0;
  LibraryPaletteVm palette;
  std::uint64_t revision = 0;
};

[[nodiscard]] UiText LibrarySectionText(LibrarySection section) noexcept;
// Zen's .zen-big-accent-button: the primary color with its OKLCH lightness
// clamped for the scheme.
[[nodiscard]] Color AccentButtonColor(Color primary, bool dark) noexcept;
// The same color with its OKLCH lightness clamped to [low, high].
[[nodiscard]] Color ClampLightness(Color color, float low, float high) noexcept;
// Zen's formatSecondsToTime: m:ss, or h:mm:ss from an hour.
[[nodiscard]] std::string FormatMediaTime(double seconds);

// Which Store domains changed since the last render.
struct LibraryChanges {
  bool library = false, tabs = false, spaces = false, theme = false,
       text = false;
};

class LibraryPresenter {
 public:
  void Present(const Store& store,
               const ThemeVm& theme,
               LibraryChanges changed,
               LibraryVm& out);

 private:
  void PresentDownloads(const LibraryState& state, UiTextView text);
  void PresentMedia(const Store& store);
  void PresentSection(const Store& store, LibraryVm& out);

  std::vector<DownloadVm> downloads_;  // host order, newest last
  std::vector<MediaCardVm> media_;
  std::vector<LibraryRowVm> rows_, tiles_;
  std::vector<LibrarySection> sections_;
  std::uint64_t revision_ = ~std::uint64_t{0};
  bool horizontal_ = false;
};
}  // namespace kr
