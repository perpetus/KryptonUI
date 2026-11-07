#pragma once

// What to draw, derived from the Store by the Presenter.
//
// Everything here borrows (string_view, span, pointers) from the Store and
// the Presenter's own buffers. A view model is valid only during the
// ShellView::Render call it is passed to; views copy what they keep.

#include <cstdint>
#include <span>
#include <string_view>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/core/flags.hpp"
#include "krypton/ui/core/ids.hpp"
#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/layout/shell_layout.hpp"
#include "krypton/ui/model/model.hpp"
#include "krypton/ui/model/workspace_editor.hpp"
#include "krypton/ui/presentation/browser_tools.hpp"
#include "krypton/ui/presentation/library.hpp"
#include "krypton/ui/presentation/onboarding.hpp"
#include "krypton/ui/presentation/privacy_features.hpp"
#include "krypton/ui/presentation/settings.hpp"
#include "krypton/ui/state/image_store.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr {

struct TabItemVm {
  TabId id;
  std::string_view title;
  std::string_view url;
  ImageId favicon;
  bool active = false;
  bool audible = false;
  bool muted = false;
  bool crashed = false;
  bool attention = false;
  FolderId folder;
  bool essential = false;
};

struct FolderVm {
  FolderId id;
  FolderId parent;
  std::string_view name;
  std::string_view glyph;
  std::size_t order = 0;
  bool collapsed = false;
  bool can_add_child = true;
};
struct FolderRowVm {
  FolderId folder;
  TabId tab;
  std::size_t depth = 0;
  bool contains_active = false;
};

struct SpaceVm {
  SpaceId id;
  std::string_view name;
  std::string_view glyph;
  Color color;
  Color icon_color;
  bool active = false;
};

struct ActionVm {
  ActionId id;
  ActionKind kind = ActionKind::kPage;
  std::string_view label;
  std::string_view tooltip;
  std::string_view glyph;
  std::string_view badge;
  ImageId icon;
  Color badge_color;
  Color badge_text_color;
  float progress = -1;
  bool enabled = true;
  bool active = false;
  bool attention = false;
};

struct HeaderVm {
  bool show_window_controls = true;
  bool show_compact_toggle = true;
  bool compact = false;
  bool can_go_back = false;
  bool can_go_forward = false;
  bool is_loading = false;
  bool maximized = false;
  bool fullscreen = false;
  SecurityLevel security = SecurityLevel::kNone;
  std::string_view address_text;
  bool show_site_security = true;
  bool window_active = true;
};

struct SuggestionVm {
  SuggestionKind kind = SuggestionKind::kUrl;
  std::string_view contents;
  std::string_view description;
  ImageId icon;
  bool rich_image = false;
  std::span<const SuggestionClassification> contents_class, description_class;
};

struct CommandBarVm {
  bool open = false;
  bool floating = true;
  bool menu_backdrop = false;
  std::string_view text;
  bool select_all = false;
  AddressSelection selection;
  std::uint64_t edit_revision = 0;
  std::uint32_t open_serial = 0;
  std::span<const SuggestionVm> suggestions;
  int selected = 0;
};

struct TabPreviewVm {
  bool show_image = true;
  TabId tab;
  std::uint32_t request_id = 0;
  std::string_view title;
  std::string_view domain;
  ImageId favicon;
  ImageId thumbnail;
  std::string_view memory_label;
  std::string_view memory_value;
  std::string_view memory_text;
  std::string_view status;
  bool memory_known = false;
};

struct ThemeVm {
  GlassMode glass_mode = GlassMode::kOff;
  MenuMaterialKind menu_material = MenuMaterialKind::kDefault;
  IconPack icon_pack = IconPack::kFluent;
  bool incognito = false;
  const WorkspaceTheme* workspace_theme = nullptr;
  const design::Palette* palette = &design::kTokens.light;
  bool dark = false;
  bool high_contrast = false;
  // Quality glass: the host frame and chrome islands paint nothing so the
  // platform's system backdrop shows through.
  bool transparent_chrome = false;
  // Window background: a gradient from top-left to bottom-right of the whole
  // client area. Views draw their part of it in window coordinates so
  // separately composited surfaces line up.
  Color gradient_start;
  Color gradient_end;
  Color accent;
};

struct ShellViewModel {
  TabId active_tab_row;
  struct SplitChooserVm {
    bool open = false;
    SplitLayout layout = SplitLayout::kColumns;
    std::span<const TabId> selected;
    std::span<const TabItemVm> candidates;
    bool snap_layouts = false;
    bool layout_only = false;
    Rect anchor;
  } split_chooser;
  OnboardingVm onboarding;
  PrivacyFeaturesVm privacy_features;
  LibraryVm library;
  BrowserToolsVm browser_tools;
  WorkspaceEditorVm workspace_editor;
  const BoostEditorState* boost_editor = nullptr;
  struct SettingsVm {
    std::span<const SettingsEntry> entries;
    const BrowserSettingsState* browser = nullptr;
    const BrowserServicesState* services = nullptr;
    std::span<const Space> workspaces;
    SpaceId active_workspace;
    bool can_edit_workspaces = false;
    bool can_choose_search = false, can_make_default = false;
    bool can_permissions = false;
    bool appearance_visible = false;
    SettingsState state;
    AppearancePrefs appearance;
    LayoutPrefs layout;
    LayoutChoice selected_layout = LayoutChoice::kSidebarAndToolbar;
    float minimum_sidebar_width = 0;
    std::span<const SettingsSectionItem> sections;
    std::span<const LayoutChoiceItem> layouts;
  } settings;
  std::span<const TabItemVm>
      horizontal_tabs;  // host order, active space + favorites
  struct LoadingProgressVm {
    LoadingProgressState state;
    bool visible = false;
    Color primary, secondary, foreground;
  } loading_progress;
  struct ThemeEditorVm {
    bool open = false;
    SpaceId space;
    std::string_view name;
    const WorkspaceTheme* value = nullptr;
    std::size_t page = 0;
    std::size_t first_preset = 0;
    std::span<const design::EditorPreset> presets;
    bool can_add = false, can_remove = false, can_harmonize = false;
    bool previous_page = false, next_page = false;
  } theme_editor;
  UiTextView text;
  ShellLayout layout;
  Rect client;
  float scale_factor = 1;
  ThemeVm theme;
  HeaderVm header;

  std::span<const TabItemVm> favorites;  // grid at the top, all spaces
  std::size_t favorite_columns = 0;
  std::span<const TabItemVm> pinned;  // active space, pinned
  std::span<const TabItemVm> today;   // active space, everything else
  bool today_idle = false;  // no Today tab is in use: Clear shows its arrow
  std::span<const FolderVm> folders;
  std::span<const TabItemVm> folder_tabs;
  std::span<const FolderRowVm> folder_rows;
  bool can_folders = false;
  std::span<const ActionVm> page_actions;     // inside the address pill
  std::span<const ActionVm> toolbar_actions;  // extensions, pinned, toolbar
  std::span<const SpaceVm> spaces;

  CommandBarVm command_bar;
  TabPreviewVm tab_preview;

  const ImageStore* images = nullptr;
  std::span<const ImageId> dropped_images;

  // What changed since the previous render. Views skip untouched sections.
  Flags<Dirty> changed;
};

}  // namespace kr
