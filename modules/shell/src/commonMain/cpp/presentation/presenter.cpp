#include "krypton/ui/presentation/presenter.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>

#include "krypton/ui/design/tokens.hpp"
#include "krypton/ui/layout/horizontal_layout.hpp"
#include "krypton/ui/presentation/clear_tabs.hpp"
#include "krypton/ui/presentation/favorite_layout.hpp"
#include "krypton/ui/presentation/multi_page.hpp"
#include "krypton/ui/presentation/settings.hpp"
#include "krypton/ui/presentation/tab_preview.hpp"
#include "krypton/ui/presentation/theme_editor.hpp"

namespace kr {

TabItemVm PresentTabItem(const Tab& tab, TabId active) {
  return {
      .id = tab.id,
      .title = tab.title,
      .url = tab.url,
      .favicon = tab.favicon,
      .active = tab.id == active,
      .audible = tab.flags.Has(TabFlag::kAudible),
      .muted = tab.flags.Has(TabFlag::kMuted),
      .crashed = tab.flags.Has(TabFlag::kCrashed),
      .attention = tab.flags.Has(TabFlag::kAttention),
      .folder = tab.folder,
      .essential = tab.flags.Has(TabFlag::kFavorite),
  };
}

namespace {

ActionVm ToActionItem(const Action& action) {
  return {
      .id = action.id,
      .kind = action.kind,
      .label = action.label,
      .tooltip = action.tooltip,
      .glyph = action.glyph,
      .badge = action.badge,
      .icon = action.icon,
      .badge_color = action.badge_color,
      .badge_text_color = action.badge_text_color,
      .progress = action.progress,
      .enabled = action.flags.Has(ActionFlag::kEnabled),
      .active = action.flags.Has(ActionFlag::kActive),
      .attention = action.flags.Has(ActionFlag::kAttention),
  };
}

}  // namespace

const ShellViewModel& Presenter::Present(const Store& store,
                                         const ShellLayout& layout,
                                         Flags<Dirty> changed,
                                         bool snap_layouts,
                                         bool folders) {
  vm_.can_folders = folders;
  vm_.changed = {};
  const bool new_source = source_ != &store;
  if (new_source || changed.Has(Dirty::kPrivacyFeatures))
    PresentPrivacyFeatures(store.privacy_features());
  source_ = &store;
  const auto& chooser = store.ui().split_chooser;
  if (chooser.open && (new_source || !vm_.split_chooser.open ||
                       changed.HasAny(Dirty::kTabs | Dirty::kSpaces))) {
    split_candidates_.clear();
    for (const auto& tab : store.tabs()) {
      if (!store.IsTransientTab(tab.id) &&
          (tab.flags.Has(TabFlag::kFavorite) || !store.active_space() ||
           tab.space == store.active_space()))
        split_candidates_.push_back(PresentTabItem(tab, {}));
    }
  }
  vm_.split_chooser = {chooser.open,
                       chooser.layout,
                       {chooser.selected.data(), chooser.count},
                       chooser.open
                           ? std::span<const TabItemVm>(split_candidates_)
                           : std::span<const TabItemVm>{}};
  vm_.split_chooser.snap_layouts = snap_layouts;
  vm_.split_chooser.layout_only = chooser.layout_only;
  vm_.split_chooser.anchor = chooser.anchor;
  if (new_source) {
    task_revision_ = ~std::uint64_t{0};
    permission_revision_ = ~std::uint64_t{0};
  }
  if (new_source || changed.Has(Dirty::kBrowserTools))
    PresentBrowserTools(store.browser_tools());
  const bool horizontal =
      TabLayoutOf(store.layout_prefs().flags) != TabLayout::kVertical;
  if (new_source || changed.Has(Dirty::kTabs) || changed.Has(Dirty::kSpaces) ||
      horizontal_ != horizontal) {
    horizontal_ = horizontal;
    PresentTabs(store);
    today_idle_ = UnpinnedTabsIdle(store);
  } else if (changed.Has(Dirty::kActiveTab)) {
    if (!PresentActiveTab(store))
      today_idle_ = UnpinnedTabsIdle(store);
    if (!store.folders().empty())
      PresentFolders(store);
  }
  if (new_source || changed.Has(Dirty::kActions) || changed.Has(Dirty::kLayout))
    PresentActions(store);
  vm_.theme = PresentTheme(store);
  if (new_source || changed.HasAny(Dirty::kSpaces | Dirty::kTheme))
    PresentSpaces(store);
  if (new_source || changed.HasAny(Dirty::kCommandBar | Dirty::kTheme))
    PresentCommandBar(store);
  PresentTabPreview(store);
  vm_.boost_editor = &store.ui().boost_editor;
  const auto& workspace = store.ui().workspace_editor;
  vm_.workspace_editor = {workspace.open,       workspace.can_remove,
                          workspace.serial,     workspace.value.id,
                          workspace.value.name, workspace.value.glyph,
                          workspace.icon_tab};
  const auto& editor = store.ui().theme_editor;
  const auto* editor_space = store.FindSpace(editor.space);
  std::size_t first = 0, count = 0;
  for (const auto& preset : design::kTokens.editor_presets) {
    if (static_cast<std::size_t>(preset.page) < editor.page)
      ++first;
    else if (static_cast<std::size_t>(preset.page) == editor.page)
      ++count;
  }
  vm_.theme_editor = {
      .open = editor.open,
      .space = editor.space,
      .name = editor_space ? std::string_view(editor_space->name)
                           : std::string_view{},
      .value = &editor.value,
      .page = editor.page,
      .first_preset = first,
      .presets =
          std::span{design::kTokens.editor_presets}.subspan(first, count),
      .can_add = editor.value.point_count > 0 &&
                 editor.value.point_count < kMaxThemePoints,
      .can_remove = editor.value.point_count > 0,
      .can_harmonize = editor.value.point_count > 1,
      .previous_page = editor.page > 0,
      .next_page = editor.page + 1 < kThemePresetPageCount};

  const NavState& nav = store.nav();
  const WindowState& window = store.window();
  if (new_source ||
      changed.HasAny(Dirty::kSettings | Dirty::kOnboarding | Dirty::kSpaces |
                     Dirty::kLayout | Dirty::kPrivacyFeatures))
    PresentSettings(store);
  vm_.settings = {.entries = settings_entries_,
                  .browser = &store.browser_settings(),
                  .services = &store.browser_services(),
                  .workspaces = store.spaces(),
                  .active_workspace = store.active_space(),
                  .can_edit_workspaces = store.ui().settings.workspace_editable,
                  .can_choose_search = store.ui().settings.search_editable,
                  .can_make_default = store.ui().settings.default_editable,
                  .can_permissions = store.ui().settings.permissions_available,
                  .appearance_visible = store.ui().settings.section ==
                                        SettingsSection::kAppearance,
                  .state = store.ui().settings,
                  .appearance = store.appearance_prefs(),
                  .layout = store.layout_prefs(),
                  .selected_layout = SelectedLayout(store.layout_prefs()),
                  .minimum_sidebar_width = MinSidebarWidth(
                      store.layout_prefs().flags, design::kTokens,
                      layout.native_titlebar.enabled),
                  .sections = settings_sections_,
                  .layouts = SettingsLayouts()};
  if (new_source ||
      changed.HasAny(Dirty::kOnboarding | Dirty::kTheme | Dirty::kText |
                     Dirty::kLayout | Dirty::kWindow | Dirty::kSpaces |
                     Dirty::kPrivacyFeatures))
    PresentOnboarding(store, layout);
  vm_.layout = layout;
  vm_.text = store.text();
  vm_.client = window.client;
  vm_.scale_factor = window.scale_factor;
  library_.Present(
      store, vm_.theme,
      {.library = new_source || changed.Has(Dirty::kLibrary),
       .tabs = changed.Has(Dirty::kTabs) || changed.Has(Dirty::kActiveTab),
       .spaces = changed.Has(Dirty::kSpaces),
       .theme = changed.Has(Dirty::kTheme),
       .text = changed.Has(Dirty::kText)},
      vm_.library);
  Color loading_primary = vm_.theme.accent;
  const Color loading_foreground =
      Color{vm_.theme.palette->text}.WithAlpha(255);
  Color loading_secondary =
      loading_primary.a() ? Mix(loading_primary, loading_foreground,
                                design::kTokens.loading_progress.secondary_mix)
                          : Color{};
  if (const auto* value = vm_.theme.workspace_theme;
      value && value->point_count > 1 &&
      value->point_count <= value->points.size())
    loading_secondary = value->points[value->point_count - 1].color;
  if (vm_.theme.dark) {
    const float mix = design::kTokens.loading_progress.dark_foreground_mix;
    if (loading_primary.a())
      loading_primary = Mix(loading_primary, loading_foreground, mix);
    if (loading_secondary.a())
      loading_secondary = Mix(loading_secondary, loading_foreground, mix);
  }
  vm_.loading_progress = {
      .state = store.loading_progress(),
      .visible = design::kTokens.loading_progress.enabled &&
                 store.loading_progress().enabled &&
                 store.loading_progress().phase != LoadPhase::kIdle &&
                 !store.ui().address_focused && !store.ui().command_bar.open &&
                 IsProgressPage(nav.full_url) &&
                 (layout.toolbar_layout == ToolbarLayout::kSingle
                      ? layout.sidebar_visible
                      : layout.toolbar_visible),
      .primary = loading_primary,
      .secondary = loading_secondary,
      .foreground = loading_foreground};
  vm_.header = {
      .show_window_controls = !layout.native_titlebar.enabled,
      .show_compact_toggle =
          layout.toolbar_layout != ToolbarLayout::kSingle ||
          layout.sidebar.width >=
              2 * design::kTokens.spacing.sidebar_padding +
                  5 * (design::kTokens.spacing.button_size +
                       2 * design::kTokens.spacing.button_margin) +
                  (layout.native_titlebar.enabled
                       ? 0
                       : 3 * design::kTokens.caption.button_width),
      .compact = store.layout_prefs().flags.Has(LayoutFlag::kCompactMode),
      .can_go_back = nav.can_go_back,
      .can_go_forward = nav.can_go_forward,
      .is_loading = nav.is_loading,
      .maximized = window.flags.Has(WindowFlag::kMaximized),
      .fullscreen = window.flags.HasAny(WindowFlag::kFullscreen |
                                        WindowFlag::kTabFullscreen),
      .security = nav.security,
      .address_text =
          store.layout_prefs().flags.Has(LayoutFlag::kMinimalAddress) &&
                  !store.ui().address_focused && !store.ui().command_bar.open &&
                  IsProgressPage(nav.full_url)
              ? TabPreviewDomain(nav.full_url)
              : std::string_view(nav.display_text),
      .window_active = window.flags.Has(WindowFlag::kActive),
  };
  vm_.header.show_site_security =
      horizontal || std::ranges::none_of(page_actions_, [](const auto& action) {
        return action.kind == ActionKind::kSiteTools;
      });
  vm_.favorites = favorites_;
  vm_.horizontal_tabs = horizontal_tabs_;
  vm_.favorite_columns =
      layout.sidebar_icon_only
          ? (favorites_.empty() ? 0u : 1u)
          : FavoriteColumns(layout.sidebar_width -
                                2 * design::kTokens.spacing.sidebar_padding,
                            favorites_.size());
  vm_.pinned = pinned_;
  vm_.folders = folders_;
  vm_.folder_tabs = folder_tabs_;
  vm_.folder_rows = folder_rows_;
  vm_.today = today_;
  vm_.today_idle = today_idle_;
  vm_.page_actions = page_actions_;
  vm_.toolbar_actions = toolbar_actions_;
  vm_.spaces = spaces_;
  vm_.images = &store.images();
  vm_.dropped_images = store.images().dropped();
  vm_.changed |= changed;
  return vm_;
}

void Presenter::PresentTabs(const Store& store) {
  folders_.clear();
  folder_tabs_.clear();
  for (const auto& folder : store.folders())
    if (folder.space == store.active_space())
      folders_.push_back({folder.id, folder.parent, folder.name, folder.glyph,
                          folder.order, folder.collapsed});
  std::ranges::sort(folders_, {}, &FolderVm::order);
  horizontal_tabs_.clear();
  favorites_.clear();
  pinned_.clear();
  today_.clear();
  const TabId active = store.presented_tab();
  const SpaceId space = store.active_space();
  const bool horizontal =
      TabLayoutOf(store.layout_prefs().flags) != TabLayout::kVertical;
  for (const Tab& tab : store.tabs()) {
    if (!tab.flags.Has(TabFlag::kFavorite) && space && tab.space != space)
      continue;
    if (store.multi_page()) {
      if (store.IsTransientTab(tab.id))
        continue;
      if (const auto* group = store.SplitForTab(tab.id);
          group && SplitRepresentative(*group) != tab.id)
        continue;
    }
    const auto item = store.multi_page() ? PresentMultiPageTab(store, tab)
                                         : PresentTabItem(tab, active);
    if (horizontal &&
        (tab.flags.Has(TabFlag::kFavorite) || !space || tab.space == space))
      horizontal_tabs_.push_back(item);
    if (tab.flags.Has(TabFlag::kFavorite)) {
      favorites_.push_back(item);
    } else if (space && tab.space != space) {
      // A host without spaces never sets one: then every tab is listed.
      continue;
    } else if (tab.folder && store.FindFolder(tab.folder)) {
      folder_tabs_.push_back(item);
    } else if (tab.flags.Has(TabFlag::kPinned)) {
      pinned_.push_back(item);
    } else {
      today_.push_back(item);
    }
  }
  PresentFolders(store);
  CacheActiveGroupRows(store);
}

void Presenter::CacheActiveGroupRows(const Store& store) {
  active_group_ = {};
  active_group_rows_.fill(nullptr);
  const auto active = store.presented_tab();
  vm_.active_tab_row = active;
  const auto* group = store.SplitForTab(active);
  if (!group)
    return;
  active_group_ = group->id;
  vm_.active_tab_row = SplitRepresentative(*group);
  std::size_t slot = 0;
  for (auto* section : {&pinned_, &today_, &folder_tabs_, &horizontal_tabs_}) {
    for (auto& item : *section) {
      if (item.id == vm_.active_tab_row && slot < active_group_rows_.size())
        active_group_rows_[slot++] = &item;
    }
  }
}

bool Presenter::PresentActiveTab(const Store& store) {
  const auto active = store.presented_tab();
  const auto* group = store.SplitForTab(active);
  if (group && group->id == active_group_) {
    const auto* tab = store.FindTab(SplitRepresentative(*group));
    if (tab) {
      const auto row = PresentMultiPageTab(store, *tab);
      for (auto* cached : active_group_rows_)
        if (cached)
          *cached = row;
      return true;
    }
  }
  for (auto* section :
       {&favorites_, &pinned_, &today_, &folder_tabs_, &horizontal_tabs_})
    for (auto& item : *section) {
      if (store.multi_page()) {
        if (const auto* tab = store.FindTab(item.id))
          item = PresentMultiPageTab(store, *tab);
      } else {
        item.active = item.id == active;
      }
    }
  CacheActiveGroupRows(store);
  return false;
}

void Presenter::PresentActions(const Store& store) {
  page_actions_.clear();
  toolbar_actions_.clear();
  const bool consolidate =
      TabLayoutOf(store.layout_prefs().flags) == TabLayout::kVertical &&
      std::ranges::any_of(store.actions(), [](const auto& action) {
        return action.kind == ActionKind::kSiteTools &&
               action.flags.Has(ActionFlag::kVisible);
      });
  for (const Action& action : store.actions()) {
    if (!action.flags.Has(ActionFlag::kVisible))
      continue;
    const bool in_address_pill = action.kind == ActionKind::kSiteTools ||
                                 action.kind == ActionKind::kPage ||
                                 action.kind == ActionKind::kSiteSetting ||
                                 action.kind == ActionKind::kExtension ||
                                 action.kind == ActionKind::kCopyLink;
    if (consolidate && in_address_pill &&
        action.kind != ActionKind::kSiteTools &&
        action.kind != ActionKind::kExtension &&
        action.kind != ActionKind::kCopyLink)
      continue;
    (in_address_pill ? page_actions_ : toolbar_actions_)
        .push_back(ToActionItem(action));
  }
}

void Presenter::PresentSpaces(const Store& store) {
  spaces_.clear();
  for (const Space& space : store.spaces()) {
    spaces_.push_back({
        .id = space.id,
        .name = space.name,
        .glyph = space.glyph,
        .color = space.color,
        .icon_color = vm_.theme.high_contrast
                          ? Color{}
                          : Color{vm_.theme.palette->workspace_icon},
        .active = space.id == store.active_space(),
    });
  }
}

void Presenter::PresentCommandBar(const Store& store) {
  const CommandBarState& bar = store.ui().command_bar;
  const auto behavior = SelectedSearchBarBehavior(store.appearance_prefs());
  suggestions_.clear();
  for (const Suggestion& s : bar.suggestions) {
    suggestions_.push_back({
        .kind = s.kind,
        .contents = s.contents,
        .description = s.description,
        .icon = s.icon,
        .rich_image = s.rich_image,
        .contents_class = s.contents_class,
        .description_class = s.description_class,
    });
  }
  vm_.command_bar = {
      .open = bar.open,
      .floating = behavior == SearchBarBehavior::kAlwaysFloating ||
                  (behavior == SearchBarBehavior::kFloatingOnType &&
                   (!bar.pointer_open || bar.typed)),
      .menu_backdrop = store.appearance_prefs().flags.Has(
          AppearanceFlag::kSearchBarMenuBackdrop),
      .text = bar.text,
      .select_all = bar.select_all,
      .selection = bar.selection,
      .edit_revision = bar.edit_revision,
      .open_serial = bar.open_serial,
      .suggestions = suggestions_,
      .selected = bar.selected,
  };
}

void Presenter::PresentTabPreview(const Store& store) {
  vm_.tab_preview = {};
  const auto& preview = store.ui().tab_preview;
  const auto* tab = store.FindTab(preview.tab);
  if (!tab)
    return;
  const auto text = store.text();
  const auto flags = preview.data.flags;
  const bool known = flags.Has(TabPreviewFlag::kMemoryKnown);
  preview_memory_.clear();
  if (known) {
    constexpr std::array units{UiText::kBytes,     UiText::kKilobytes,
                               UiText::kMegabytes, UiText::kGigabytes,
                               UiText::kTerabytes, UiText::kPetabytes,
                               UiText::kExabytes};
    constexpr double bytes_per_unit = 1000;
    double amount = static_cast<double>(preview.data.memory_usage_bytes);
    std::size_t unit = 0;
    while (amount >= bytes_per_unit && unit + 1 < units.size()) {
      amount /= bytes_per_unit;
      ++unit;
    }
    if (unit == 0)
      std::format_to(std::back_inserter(preview_memory_), "{} {}",
                     preview.data.memory_usage_bytes, text[units[unit]]);
    else
      std::format_to(std::back_inserter(preview_memory_), "{:.1f} {}", amount,
                     text[units[unit]]);
  } else {
    preview_memory_.assign(text[UiText::kMemoryUnavailable]);
  }
  const auto memory_label = text[flags.Has(TabPreviewFlag::kMemoryEstimated)
                                     ? UiText::kMemoryEstimated
                                     : UiText::kMemoryUsage];
  preview_memory_text_.clear();
  if (known) {
    preview_memory_text_.append(memory_label);
    preview_memory_text_.append(": ");
  }
  preview_memory_text_.append(preview_memory_);
  preview_status_.clear();
  const auto add = [&](bool shown, UiText key) {
    if (!shown)
      return;
    if (!preview_status_.empty())
      preview_status_.append(" · ");
    preview_status_.append(text[key]);
  };
  add(flags.Has(TabPreviewFlag::kSample), UiText::kPreviewSample);
  add(flags.Has(TabPreviewFlag::kSleeping), UiText::kSleeping);
  add(tab->flags.Has(TabFlag::kCrashed), UiText::kTabCrashed);
  add(tab->flags.Has(TabFlag::kLoading), UiText::kTabLoading);
  add(tab->flags.Has(TabFlag::kMuted), UiText::kAudioMuted);
  add(tab->flags.Has(TabFlag::kAudible) && !tab->flags.Has(TabFlag::kMuted),
      UiText::kPlayingAudio);
  add(tab->flags.Has(TabFlag::kRecording), UiText::kRecording);
  add(tab->flags.Has(TabFlag::kCapturing), UiText::kCapturing);
  const auto appearance = store.appearance_prefs().flags;
  const bool labels = flags.Has(TabPreviewFlag::kLabelsProvided);
  vm_.tab_preview = {
      .show_image = !appearance.Has(AppearanceFlag::kHideHoverImages) &&
                    !flags.Has(TabPreviewFlag::kNoImage),
      .tab = tab->id,
      .request_id = preview.request_id,
      .title = labels ? std::string_view(preview.data.title) : tab->title,
      .domain = labels ? std::string_view(preview.data.domain)
                       : TabPreviewDomain(tab->url),
      .favicon = tab->favicon,
      .thumbnail = appearance.Has(AppearanceFlag::kHideHoverImages) ||
                           flags.Has(TabPreviewFlag::kNoImage)
                       ? ImageId{}
                       : preview.data.thumbnail,
      .memory_label = memory_label,
      .memory_value = preview_memory_,
      .memory_text = appearance.Has(AppearanceFlag::kHideHoverMemory)
                         ? std::string_view{}
                     : labels ? std::string_view(preview.data.memory_text)
                              : std::string_view(preview_memory_text_),
      .status =
          labels ? std::string_view(preview.data.status) : preview_status_,
      .memory_known = known,
  };
}

ThemeVm Presenter::PresentTheme(const Store& store) {
  const Theme& theme = store.theme();
  const design::Palette& palette = design::PaletteFor(theme.dark);
  const Color base = design::ToColor(palette.gradient_base);

  // Glass takes its color from the backdrop. Workspace tint and grain remain
  // available for Off; incognito and high contrast also keep a neutral base.
  Color tint = theme.accent;
  if (const Space* space = store.FindSpace(store.active_space())) {
    tint = space->color;
  }
  const bool glass = store.appearance_prefs().glass_mode != GlassMode::kOff;
  const bool neutral =
      glass || theme.incognito || theme.high_contrast || tint.a() == 0;
  const auto* workspace = store.WorkspaceThemeFor(store.active_space());
  const bool customized = workspace && workspace->customized &&
                          !theme.incognito && !theme.high_contrast;
  const bool custom = customized && !glass;
  if (customized && workspace->point_count)
    tint = workspace->points[0].color;

  return {
      .glass_mode = store.appearance_prefs().glass_mode,
      .menu_material = store.appearance_prefs().menu_material,
      .icon_pack = store.appearance_prefs().icon_pack,
      .incognito = theme.incognito,
      .workspace_theme = custom ? workspace : nullptr,
      .palette = &palette,
      .dark = theme.dark,
      .high_contrast = theme.high_contrast,
      .transparent_chrome =
          GlassTransparentChrome(store.appearance_prefs().glass_mode) &&
          !theme.high_contrast,
      .gradient_start =
          custom    ? SampleWorkspaceTheme(*workspace, theme.dark, {0, 0})
          : neutral ? base
                    : Mix(base, tint, palette.gradient_mix_top),
      .gradient_end = custom
                          ? SampleWorkspaceTheme(*workspace, theme.dark, {1, 1})
                      : neutral ? base
                                : Mix(base, tint, palette.gradient_mix_bottom),
      .accent = tint,
  };
}

}  // namespace kr
