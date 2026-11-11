#include "krypton/fixtures/mock_browser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <format>
#include <utility>

#include "chrome_commands.hpp"
#include "krypton/ui/design/tokens.generated.hpp"
#include "mock_library.hpp"

namespace kr::fixtures {
bool MockBrowser::FocusNative(std::uint32_t element) {
  if (!attached_ || !KR_HAS_SLOT(&ui_, KrShellUI, focus_native) ||
      !ui_.focus_native)
    return false;
  const KrNativeFocus target{sizeof(target), KR_ANCHOR_ELEMENT, element};
  return ui_.focus_native(ui_.self, &target) == KR_OK;
}

namespace {

constexpr auto& kTokens = design::kTokens;
constexpr KrImageId kThemeSwatchBase = KrImageId{1} << 63;
constexpr KrImageId kTabPreviewImage = KrImageId{1} << 62;

// A fixture is still a real ABI host: no callback may unwind into the module.
template <typename Callback>
void AtBoundary(Callback&& callback) noexcept {
  try {
    std::forward<Callback>(callback)();
  } catch (...) {
  }
}

KrStr Str(std::string_view text) {
  return {text.data(), text.size()};
}
std::string ToString(KrStr text) {
  return text.data ? std::string(text.data, text.size) : std::string();
}

std::string Lower(std::string_view text) {
  std::string lower(text);
  std::ranges::transform(lower, lower.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return lower;
}

bool LooksLikeUrl(std::string_view text) {
  return text.find(' ') == std::string_view::npos &&
         (text.find('.') != std::string_view::npos ||
          text.starts_with("http") || text.starts_with("krypton:"));
}

std::string HostOf(std::string_view url) {
  if (const auto scheme = url.find("://"); scheme != std::string_view::npos) {
    url.remove_prefix(scheme + 3);
  }
  url = url.substr(0, url.find('/'));
  if (url.starts_with("www."))
    url.remove_prefix(4);
  return std::string(url);
}

// A 32x32 rounded-square favicon in `argb`, premultiplied BGRA.
std::array<std::uint8_t, 32 * 32 * 4> MakeFavicon(std::uint32_t argb) {
  constexpr int kSize = 32;
  constexpr float kRadius = 8;
  std::array<std::uint8_t, kSize * kSize * 4> pixels{};
  const float r = static_cast<float>((argb >> 16) & 0xFF);
  const float g = static_cast<float>((argb >> 8) & 0xFF);
  const float b = static_cast<float>(argb & 0xFF);
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      // Signed distance to a rounded square, for an anti-aliased edge.
      const float px =
          std::abs(x + 0.5f - kSize / 2.0f) - (kSize / 2.0f - kRadius);
      const float py =
          std::abs(y + 0.5f - kSize / 2.0f) - (kSize / 2.0f - kRadius);
      const float outside = std::hypot(std::max(px, 0.0f), std::max(py, 0.0f)) +
                            std::min(std::max(px, py), 0.0f) - kRadius;
      float alpha = std::clamp(0.5f - outside, 0.0f, 1.0f);
      // A lighter dot in the middle so icons read as icons.
      const float dot =
          std::hypot(x + 0.5f - kSize / 2.0f, y + 0.5f - kSize / 2.0f);
      const float light = dot < 6 ? 0.45f : 0.0f;
      const auto channel = [&](float c) {
        return static_cast<std::uint8_t>((c + (255 - c) * light) * alpha);
      };
      std::uint8_t* p = &pixels[(y * kSize + x) * 4];
      p[0] = channel(b);
      p[1] = channel(g);
      p[2] = channel(r);
      p[3] = static_cast<std::uint8_t>(255 * alpha);
    }
  }
  return pixels;
}

// Segoe Fluent Icons code points; the UI draws glyph text with the icon font.
constexpr std::string_view kGlyphStar = "";
constexpr std::string_view kGlyphStarFilled = "";
constexpr std::string_view kGlyphShield = "";
constexpr std::string_view kGlyphDownload = "";

enum ActionIds : KrActionId {
  kActionBookmark = 1,
  kActionShield = 2,
  kActionDownloads = 3,
  kActionSiteData = 4,
  kActionCopyLink = 5,
};

enum MenuCommands : std::int32_t {
  kCmdNewTab = 1,
  kCmdNewWindow,
  kCmdCloseTab,
  kCmdReopen,
  kCmdHistory,
  kCmdDownloads,
  kCmdSettings,
  kCmdTogglePin,
  kCmdToggleFavorite,
  kCmdDuplicate,
  kCmdMute,
  kCmdCloseOthers,
  kCmdReload,
  kCmdTheme,
  kCmdLayoutMultiple,
  kCmdLayoutSingle,
  kCmdLayoutCollapsed,
  kCmdCompactToggle,
  kCmdCompactSidebar,
  kCmdCompactToolbar,
  kCmdCompactBoth,
  kCmdSidebarRight,
  kCmdSidebarToggle,
  kCmdSchemeSystem,
  kCmdSchemeLight,
  kCmdSchemeDark,
  kCmdThemeReset,
  kCmdFullscreen,
  kCmdBack,
  kCmdForward,
  kCmdCloseLeft,
  kCmdCloseRight,
  kCmdCloseSpace,
  kCmdEditTheme,
  kCmdNewWorkspace,
  kCmdEditWorkspace,
  kCmdSwitchSpaceBase = 100,
  kCmdMoveSpaceBase = 200,
  kCmdThemePresetBase = 300,
};

constexpr std::array<std::string_view, 6> kHistory = {
    "github.com/krypton-browser",
    "news.ycombinator.com",
    "theverge.com",
    "chromium.googlesource.com",
    "learn.microsoft.com/windows/apps",
    "developer.apple.com/design"};

}  // namespace

MockBrowser::MockBrowser(MockOptions options, MockHostCallbacks callbacks)
    : options_(options),
      callbacks_(std::move(callbacks)),
      system_dark_(options.dark) {
  if (options_.appearance.scheme != ThemeScheme::kSystem)
    options_.dark = options_.appearance.scheme == ThemeScheme::kDark;
  table_.struct_size = sizeof(KrBrowser);
  table_.self = this;
  InitPrivacyFeatures();
  InitLibrary();
  InitOnboarding();
  InitSettings();
  InitMultiPage();
  table_.activate_tab = [](void* self, KrTabId tab) noexcept {
    AtBoundary([&] { Self(self).Activate(tab); });
  };
  table_.close_tab = [](void* self, KrTabId tab) noexcept {
    AtBoundary([&] { Self(self).Close(tab); });
  };
  table_.request_tab_preview = [](void* self, std::uint32_t request,
                                  KrTabId tab) noexcept {
    AtBoundary([&] { Self(self).RequestTabPreview(request, tab); });
  };
  table_.cancel_tab_preview = [](void* self, std::uint32_t request) noexcept {
    AtBoundary([&] { Self(self).CancelTabPreview(request); });
  };
  table_.new_tab = [](void* self) noexcept {
    AtBoundary([&] { Self(self).OpenTab("", KR_OPEN_NEW_TAB); });
  };
  table_.move_tab = [](void* self, KrTabId tab, KrSpaceId space,
                       std::size_t to_index) noexcept {
    AtBoundary([&] {
      MockBrowser& mock = Self(self);
      const std::size_t from = mock.IndexOf(tab);
      if (from >= mock.tabs_.size() || to_index >= mock.tabs_.size())
        return;
      MockTab moved = std::move(mock.tabs_[from]);
      moved.space = space ? space : moved.space;
      mock.tabs_.erase(mock.tabs_.begin() + static_cast<std::ptrdiff_t>(from));
      mock.tabs_.insert(
          mock.tabs_.begin() + static_cast<std::ptrdiff_t>(to_index),
          std::move(moved));
      mock.ui_.tab_moved(mock.ui_.self, tab, to_index);
      mock.PushTab(mock.tabs_[to_index]);
    });
  };
  table_.set_tab_flags = [](void* self, KrTabId tab, std::uint32_t mask,
                            std::uint32_t values) noexcept {
    AtBoundary([&] {
      MockBrowser& mock = Self(self);
      if (MockTab* t = mock.Find(tab)) {
        t->flags = (t->flags & ~mask) | (values & mask);
        if (!(t->flags & KR_TAB_PINNED) || (t->flags & KR_TAB_FAVORITE))
          t->folder = 0;
        mock.PushTab(*t);
      }
    });
  };
  table_.switch_space = [](void* self, KrSpaceId space) noexcept {
    AtBoundary([&] { Self(self).SwitchSpace(space); });
  };
  table_.navigate = [](void* self, KrStr text,
                       std::uint32_t disposition) noexcept {
    AtBoundary([&] { Self(self).OpenTab(ToString(text), disposition); });
  };
  table_.nav_command = [](void* self, std::uint32_t command) noexcept {
    AtBoundary([&] {
      MockBrowser& mock = Self(self);
      MockTab* tab = mock.Find(mock.active_tab_);
      if (!tab)
        return;
      if (command == KR_NAV_BACK && tab->back_depth > 0) {
        --tab->back_depth;
        ++tab->forward_depth;
      } else if (command == KR_NAV_FORWARD && tab->forward_depth > 0) {
        --tab->forward_depth;
        ++tab->back_depth;
      }
      if (command == KR_NAV_RELOAD || command == KR_NAV_BACK ||
          command == KR_NAV_FORWARD)
        mock.StartLoading(*tab);
      else if (command == KR_NAV_STOP) {
        tab->flags &= ~KR_TAB_LOADING;
        tab->load_progress = 1;
        mock.PushTab(*tab);
      }
      mock.Log(KR_LOG_INFO, std::format("nav command {}", command));
      mock.PushNav();
    });
  };
  table_.query_suggestions = [](void* self, std::uint32_t request_id,
                                KrStr text) noexcept {
    AtBoundary([&] { Self(self).Suggest(request_id, ToString(text)); });
  };
  table_.address_state_changed = [](void* self,
                                    const KrAddressState* state) noexcept {
    AtBoundary([&] {
      if (state && KR_HAS_FIELD(state, KrAddressState, text)) {
        auto& browser = Self(self);
        browser.address_flags_ = state->flags;
        browser.address_state_ = {};
        std::memcpy(&browser.address_state_, state,
                    std::min(state->struct_size, sizeof(KrAddressState)));
        browser.address_text_ = ToString(state->text);
        browser.address_state_.text = {browser.address_text_.data(),
                                       browser.address_text_.size()};
      }
    });
  };
  table_.accept_suggestion = [](void* self, std::uint32_t, std::size_t index,
                                std::uint32_t disposition) noexcept {
    AtBoundary([&] { Self(self).AcceptSuggestion(index, disposition); });
  };
  table_.accept_suggestion_snapshot =
      [](void* self, std::uint32_t request, std::uint64_t generation,
         std::size_t index, std::uint32_t disposition) noexcept {
        AtBoundary([&] {
          auto& mock = Self(self);
          if (request == mock.suggestion_request_ && generation == 1)
            mock.AcceptSuggestion(index, disposition);
        });
      };
  table_.select_suggestion_snapshot = [](void* self, std::uint32_t request,
                                         std::uint64_t generation,
                                         std::size_t index) noexcept {
    AtBoundary([&] {
      auto& mock = Self(self);
      if (request == mock.suggestion_request_ && generation == 1 &&
          index < mock.last_suggestions_.size())
        mock.selected_suggestion_ = index;
    });
  };
  table_.cancel_suggestions = [](void* self, std::uint32_t request) noexcept {
    AtBoundary([&] {
      auto& mock = Self(self);
      if (mock.suggestion_request_ == request) {
        mock.suggestion_request_ = 0;
        mock.selected_suggestion_.reset();
        mock.last_suggestions_.clear();
      }
    });
  };
  // Fixtures have no external filesystem changes; the optional refresh is
  // inert.
  table_.refresh_downloads = [](void*) noexcept {};
  table_.save_routing_rule = [](void*, const KrRoutingRule*) noexcept {
    return KR_ERR_UNSUPPORTED;
  };
  table_.set_tab_icon = [](void* self, KrTabId tab, KrStr glyph) noexcept {
    AtBoundary([&] {
      if ((!glyph.data && glyph.size) || glyph.size > 128)
        return;
      auto& icons = Self(self).tab_icon_overrides_;
      if (!glyph.size)
        icons.erase(tab);
      else
        icons.insert_or_assign(tab, std::string(glyph.data, glyph.size));
    });
  };
  table_.edit_workspace = [](void* self, std::uint32_t kind,
                             const KrSpace* space) noexcept {
    AtBoundary([&] {
      if (space && KR_HAS_FIELD(space, KrSpace, color))
        Self(self).EditWorkspace(kind, *space);
    });
  };
  table_.invoke_action = [](void* self, KrActionId action,
                            KrRect anchor) noexcept {
    AtBoundary([&] {
      MockBrowser& mock = Self(self);
      if (action == kActionBookmark) {
        mock.bookmarked_ = !mock.bookmarked_;
        mock.PushActions();
      } else if (action == kActionDownloads) {
        mock.OpenTab("krypton://downloads", KR_OPEN_NEW_TAB);
      } else if (action == kActionShield) {
        mock.OpenTab("krypton://settings/privacy", KR_OPEN_NEW_TAB);
      } else if (action == kActionCopyLink) {
        if (KR_HAS_SLOT(&mock.ui_, KrShellUI, show_notification) &&
            mock.ui_.show_notification)
          mock.ui_.show_notification(mock.ui_.self,
                                     Str("Copy link requested (preview)"));
      } else if (action == kActionSiteData) {
        if (KR_HAS_SLOT(&mock.ui_, KrShellUI, request_context_menu) &&
            mock.ui_.request_context_menu)
          mock.ui_.request_context_menu(mock.ui_.self, KR_MENU_SITE_DATA, 0,
                                        anchor);
        else
          mock.OpenTab("krypton://extensions", KR_OPEN_NEW_TAB);
      }
      mock.Log(KR_LOG_INFO, std::format("action {} invoked", action));
    });
  };
  table_.request_menu = [](void* self, std::uint32_t request_id,
                           std::uint32_t kind, std::uint64_t target,
                           KrRect) noexcept {
    AtBoundary([&] { Self(self).ShowMenu(request_id, kind, target); });
  };
  table_.menu_command = [](void* self, std::uint32_t,
                           std::int32_t command) noexcept {
    AtBoundary([&] { Self(self).RunMenuCommand(command); });
  };
  table_.menu_closed = [](void* self, std::uint32_t request) noexcept {
    AtBoundary([&] {
      if (Self(self).chrome_menu_request_ == request)
        Self(self).chrome_menu_request_ = 0;
    });
  };
  table_.window_command = [](void* self, std::uint32_t command) noexcept {
    AtBoundary([&] {
      if (Self(self).callbacks_.window_command) {
        Self(self).callbacks_.window_command(command);
      }
    });
  };
  table_.set_content_bounds = [](void* self, KrRect bounds,
                                 float radius) noexcept {
    AtBoundary([&] {
      MockBrowser& mock = Self(self);
      mock.content_bounds_ = bounds;
      mock.content_radius_ = radius;
      if (mock.callbacks_.page_changed)
        mock.callbacks_.page_changed();
    });
  };
  table_.set_shell_metrics = [](void* self,
                                const KrShellMetrics* metrics) noexcept {
    AtBoundary([&] {
      if (metrics &&
          KR_HAS_FIELD(metrics, KrShellMetrics, minimum_client_height))
        Self(self).shell_metrics_ = *metrics;
    });
  };
  table_.set_window_background = [](void* self, KrColor start,
                                    KrColor end) noexcept {
    AtBoundary([&] {
      auto& mock = Self(self);
      mock.background_gradient_ = {start, end};
      if (mock.callbacks_.page_changed)
        mock.callbacks_.page_changed();
    });
  };
  table_.set_window_backdrop = [](void* self,
                                  const KrWindowBackdrop* backdrop) noexcept {
    AtBoundary([&] {
      if (!backdrop)
        return;
      auto& mock = Self(self);
      const bool transparent =
          KR_HAS_FIELD(backdrop, KrWindowBackdrop, flags) &&
          (backdrop->flags & KR_WINDOW_BACKDROP_TRANSPARENT);
      if (std::exchange(mock.transparent_chrome_, transparent) != transparent &&
          mock.callbacks_.page_changed)
        mock.callbacks_.page_changed();
    });
  };
  table_.set_anchor = [](void* self, std::uint32_t kind, std::uint64_t id,
                         KrRect bounds, bool visible) noexcept {
    AtBoundary([&] {
      if (kind == KR_ANCHOR_ELEMENT && id == KR_ELEMENT_APP_MENU)
        Self(self).app_menu_anchor_ = visible ? bounds : KrRect{};
      if (kind == KR_ANCHOR_ACTION && id == kActionSiteData) {
        auto& mock = Self(self);
        mock.site_data_anchor_ = visible ? bounds : KrRect{};
        if (visible && mock.callbacks_.site_data_anchor_changed)
          mock.callbacks_.site_data_anchor_changed(bounds);
      }
    });
  };
  table_.set_drag_regions = [](void* self, const KrRect* regions,
                               std::size_t count) noexcept {
    AtBoundary([&] {
      auto& target = Self(self).drag_regions_;
      if (count)
        target.assign(regions, regions + count);
      else
        target.clear();
    });
  };
  table_.set_layout_prefs = [](void* self,
                               const KrLayoutPrefs* prefs) noexcept {
    AtBoundary([&] {
      MockBrowser& mock = Self(self);
      if (prefs && KR_HAS_FIELD(prefs, KrLayoutPrefs, sidebar_width)) {
        // Preserve all flag bits, including opt-in workspace swipe gestures.
        mock.options_.appearance.layout_flags = prefs->flags;
        mock.options_.appearance.sidebar_width = prefs->sidebar_width;
        mock.AppearanceChanged();
      }
    });
  };
  table_.focus_page = [](void* self) noexcept {
    AtBoundary([&] {
      if (Self(self).callbacks_.focus_page)
        Self(self).callbacks_.focus_page();
    });
  };
  table_.log = [](void* self, std::uint32_t level, KrStr text) noexcept {
    AtBoundary([&] { Self(self).Log(level, ToString(text)); });
  };

  table_.set_workspace_theme = [](void* self, const KrWorkspaceTheme* theme,
                                  bool commit) noexcept {
    AtBoundary([&] {
      if (theme)
        Self(self).ApplyWorkspaceTheme(*theme, commit);
    });
  };
  table_.set_window_theme = [](void* self, const KrWorkspaceTheme* theme,
                               bool) noexcept {
    AtBoundary([&] {
      if (!theme)
        return;
      auto& mock = Self(self);
      mock.background_theme_ = *theme;
      ++mock.background_theme_generation_;
      if (mock.callbacks_.page_changed)
        mock.callbacks_.page_changed();
    });
  };
  table_.set_appearance_prefs = [](void* self,
                                   const KrAppearancePrefs* prefs) noexcept {
    AtBoundary([&] {
      if (!prefs || !KR_HAS_FIELD(prefs, KrAppearancePrefs, flags) ||
          prefs->color_scheme > KR_COLOR_DARK ||
          prefs->menu_material > KR_MENU_MATERIAL_ACRYLIC_THIN)
        return;
      auto& mock = Self(self);
      mock.options_.appearance.scheme =
          static_cast<ThemeScheme>(prefs->color_scheme);
      mock.options_.appearance.menu_material = prefs->menu_material;
      mock.options_.appearance.appearance_flags =
          prefs->flags &
          (KR_APPEARANCE_HIDE_HOVER_IMAGES | KR_APPEARANCE_HIDE_HOVER_MEMORY |
           KR_APPEARANCE_ATTACHED_SEARCH_BAR |
           KR_APPEARANCE_SEARCH_BAR_FLOATING_ON_TYPE |
           KR_APPEARANCE_SEARCH_BAR_MENU_BACKDROP | KR_APPEARANCE_NEW_TAB_PAGE);
      if (KR_HAS_FIELD(prefs, KrAppearancePrefs, icon_pack) &&
          prefs->icon_pack <= KR_ICON_PACK_RADIX)
        mock.options_.appearance.icon_pack =
            static_cast<std::uint32_t>(prefs->icon_pack);
      if (KR_HAS_FIELD(prefs, KrAppearancePrefs, glass_mode) &&
          prefs->glass_mode <= KR_GLASS_QUALITY)
        mock.options_.appearance.glass_mode =
            static_cast<std::uint32_t>(prefs->glass_mode);
      mock.AppearanceChanged();
    });
  };
  table_.request_glass_backdrop =
      [](void* self, const KrGlassBackdropRequest* request) noexcept {
        AtBoundary([&] {
          auto& mock = Self(self);
          if (request && mock.callbacks_.glass_backdrop_requested)
            mock.callbacks_.glass_backdrop_requested(*request);
        });
      };
  table_.folder_command = [](void* self,
                             const KrFolderCommand* command) noexcept {
    try {
      return command ? Self(self).ExecuteFolderCommand(*command)
                     : KR_ERR_INVALID_ARGUMENT;
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
  table_.unhandled_key = [](void* self, const void* message) noexcept {
    bool consumed = false;
    AtBoundary([&] {
      const auto& callback = Self(self).callbacks_.unhandled_key;
      consumed = message && callback && callback(message);
    });
    return consumed;
  };
  InstallBrowserTools();
  Seed();
}

MockBrowser::~MockBrowser() = default;

// ----------------------------------------------------------------- seeding

void MockBrowser::Seed() {
  spaces_ = {
      {.id = 1,
       .name = "Personal",
       .glyph = "\uE774",
       .color = options_.appearance.workspace_colors[0]},
      {.id = 2,
       .name = "Work",
       .glyph = "\uE821",
       .color = options_.appearance.workspace_colors[1]},
      {.id = 3,
       .name = "Dev",
       .glyph = "\uE943",
       .color = options_.appearance.workspace_colors[2]},
  };
  active_space_ = 1;

  AddTab(1, KR_TAB_FAVORITE, "Mail", "https://mail.example.com", 0xFF2563EB);
  AddTab(1, KR_TAB_FAVORITE, "Calendar", "https://calendar.example.com",
         0xFFF97316);
  AddTab(1, KR_TAB_FAVORITE, "GitHub", "https://github.com", 0xFF24292F);
  AddTab(1, KR_TAB_FAVORITE, "Music", "https://music.example.com", 0xFFEC4899);

  AddTab(1, KR_TAB_PINNED, "Krypton docs", "https://krypton.dev/docs",
         0xFF7C5CFF);
  AddTab(1, KR_TAB_PINNED, "Design notes", "https://notes.example.com/design",
         0xFF0EA5E9);
  // AddTab can reallocate, so keep the id, not a reference.
  MockTab& verge_tab =
      AddTab(1, 0, "The Verge", "https://www.theverge.com", 0xFFA855F7);
  verge_tab.back_depth = 2;
  const KrTabId verge = verge_tab.id;
  AddTab(1, 0, "Hacker News", "https://news.ycombinator.com", 0xFFF97316);
  AddTab(1, KR_TAB_AUDIBLE, "Lo-fi radio", "https://music.example.com/live",
         0xFFEF4444);
  AddTab(1, 0, "Wikipedia — Web standards",
         "https://en.wikipedia.org/wiki/Web_standards", 0xFF64748B);

  AddTab(2, KR_TAB_PINNED, "Linear", "https://linear.app", 0xFF5E6AD2);
  AddTab(2, KR_TAB_PINNED, "Figma", "https://figma.com", 0xFFA259FF);
  AddTab(2, 0, "Sprint board", "https://jira.example.com/board", 0xFF2684FF);
  AddTab(2, KR_TAB_ATTENTION, "Team chat", "https://chat.example.com",
         0xFF4A154B);

  AddTab(3, KR_TAB_PINNED, "Chromium code search",
         "https://source.chromium.org", 0xFF4285F4);
  AddTab(3, 0, "Perfetto UI", "https://ui.perfetto.dev", 0xFF0F9D58);

  for (int i = 0; i < options_.extra_tabs; ++i) {
    AddTab(1, 0, std::format("Stress tab {}", i + 1),
           std::format("https://example.com/stress/{}", i + 1),
           0xFF000000u | static_cast<std::uint32_t>(i * 2654435761u >> 8));
  }

  active_tab_ = verge;
  spaces_[0].last_active = verge;
  spaces_[1].last_active = tabs_[12].id;
  spaces_[2].last_active = tabs_[14].id;
}

MockBrowser::MockTab& MockBrowser::AddTab(KrSpaceId space,
                                          std::uint32_t flags,
                                          std::string title,
                                          std::string url,
                                          std::uint32_t color) {
  return tabs_.emplace_back(MockTab{.id = next_tab_id_++,
                                    .space = space,
                                    .flags = flags,
                                    .title = std::move(title),
                                    .url = std::move(url),
                                    .color = color});
}

void MockBrowser::Attach(const KrShellUI& ui) {
  ui_ = ui;
  attached_ = true;
  PushAll();
  SeedMultiPage();
}

void MockBrowser::Detach() {
  CancelTabPreview(preview_request_);
  attached_ = false;
  ui_ = {};
}

bool MockBrowser::SetUiText(std::string_view key, std::string_view text) {
  if (key.starts_with("layout.")) {
    for (auto& [name, value] : layout_text_)
      if (name == key) {
        value = text;
        return true;
      }
    layout_text_.emplace_back(key, text);
    return true;
  }

  return attached_ && KR_HAS_SLOT(&ui_, KrShellUI, set_ui_text) &&
         ui_.set_ui_text &&
         ui_.set_ui_text(ui_.self, Str(key), Str(text)) == KR_OK;
}

void MockBrowser::SetTextDirection(bool right_to_left) {
  if (attached_ && KR_HAS_SLOT(&ui_, KrShellUI, set_text_direction) &&
      ui_.set_text_direction)
    ui_.set_text_direction(ui_.self, right_to_left);
}

void MockBrowser::PushAll() {
  PushFolders();
  PushTheme();
  PushWorkspaceThemes();
  PushLayout();
  PushThemeSwatches();
  PushSpaces();
  for (const MockTab& tab : tabs_)
    PushFavicon(tab);
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    const MockTab& t = tabs_[i];
    const KrTab tab{.struct_size = sizeof(KrTab),
                    .id = t.id,
                    .space = t.space,
                    .flags = t.flags,
                    .title = Str(t.title),
                    .url = Str(t.url),
                    .favicon = t.id,
                    .folder = t.folder};
    ui_.tab_inserted(ui_.self, i, &tab);
  }
  ui_.set_active_tab(ui_.self, active_tab_);
  PushNav();
  PushActions();
  PushLibrary();
}

void MockBrowser::PushTab(const MockTab& t) const {
  if (!attached_)
    return;
  const KrTab tab{.struct_size = sizeof(KrTab),
                  .id = t.id,
                  .space = t.space,
                  .flags = t.flags,
                  .title = Str(t.title),
                  .url = Str(t.url),
                  .favicon = t.id,
                  .folder = t.folder};
  ui_.tab_updated(ui_.self, &tab);
  PushTask(t);
}

void MockBrowser::PushSpaces() const {
  if (!attached_)
    return;
  std::vector<KrSpace> spaces;
  for (const MockSpace& s : spaces_) {
    spaces.push_back({.struct_size = sizeof(KrSpace),
                      .id = s.id,
                      .name = Str(s.name),
                      .glyph = Str(s.glyph),
                      .color = s.color});
  }
  ui_.set_spaces(ui_.self, spaces.data(), spaces.size(), sizeof(KrSpace),
                 active_space_);
}

void MockBrowser::PushNav() const {
  if (!attached_)
    return;
  const MockTab* tab = Find(active_tab_);
  const std::string host = tab ? HostOf(tab->url) : std::string();
  const KrNavState nav{
      .struct_size = sizeof(KrNavState),
      .can_go_back = tab && tab->back_depth > 0,
      .can_go_forward = tab && tab->forward_depth > 0,
      .is_loading = tab && (tab->flags & KR_TAB_LOADING) != 0,
      .security = static_cast<std::uint32_t>(
          tab && tab->url.starts_with("https") ? KR_SECURITY_SECURE
                                               : KR_SECURITY_NONE),
      .display_text = Str(host),
      .full_url = tab ? Str(tab->url) : Str(""),
  };
  ui_.set_nav_state(ui_.self, &nav);
  if (tab)
    PushLoadProgress(*tab);
}

void MockBrowser::PushActions() const {
  if (!attached_)
    return;
  const std::uint32_t on = KR_ACTION_VISIBLE | KR_ACTION_ENABLED;
  const std::array actions = {
      KrAction{.struct_size = sizeof(KrAction),
               .id = kActionBookmark,
               .kind = KR_ACTION_PAGE,
               .flags = on | (bookmarked_ ? KR_ACTION_ACTIVE : 0u),
               .label = Str("Bookmark"),
               .tooltip = Str("Bookmark this tab"),
               .glyph = Str(bookmarked_ ? kGlyphStarFilled : kGlyphStar),
               .progress = -1},
      KrAction{.struct_size = sizeof(KrAction),
               .id = kActionShield,
               .kind = KR_ACTION_EXTENSION,
               .flags = on,
               .label = Str("uBlock Origin"),
               .tooltip = Str("uBlock Origin: 12 blocked"),
               .glyph = Str(kGlyphShield),
               .badge = Str("12"),
               .badge_color = 0xFFB91C1C,
               .progress = -1,
               .badge_text_color = 0xFFFFFFFF},
      KrAction{.struct_size = sizeof(KrAction),
               .id = kActionCopyLink,
               .kind = KR_ACTION_COPY_LINK,
               .flags = on,
               .label = Str("Copy link"),
               .tooltip = Str("Copy link"),
               .glyph = Str("\uE8C8"),
               .progress = -1},
      KrAction{.struct_size = sizeof(KrAction),
               .id = kActionSiteData,
               .kind = KR_ACTION_SITE_TOOLS,
               .flags = on,
               .label = Str("Site Data"),
               .tooltip = Str("Site data and extensions"),
               .glyph = Str("\uEA86"),
               .progress = -1},
  };
  ui_.set_actions(ui_.self, actions.data(), actions.size(), sizeof(KrAction));
}

void MockBrowser::PushIcon(KrImageId id, std::uint32_t argb) const {
  if (!attached_)
    return;
  const auto pixels = MakeFavicon(argb);
  const KrImage image{.struct_size = sizeof(KrImage),
                      .id = id,
                      .width = 32,
                      .height = 32,
                      .stride = 32 * 4,
                      .pixels = pixels.data()};
  ui_.put_image(ui_.self, &image);
}

void MockBrowser::PushFavicon(const MockTab& tab) const {
  if (!attached_)
    return;
  const auto pixels = MakeFavicon(tab.color);
  const KrImage image{.struct_size = sizeof(KrImage),
                      .id = tab.id,
                      .width = 32,
                      .height = 32,
                      .stride = 32 * 4,
                      .pixels = pixels.data()};
  ui_.put_image(ui_.self, &image);
}

void MockBrowser::RequestTabPreview(std::uint32_t request_id, KrTabId id) {
  const auto* tab = Find(id);
  if (!attached_ || !tab || request_id == 0 ||
      !KR_HAS_SLOT(&ui_, KrShellUI, set_tab_preview) || !ui_.set_tab_preview)
    return;
  CancelTabPreview(preview_request_);
  preview_request_ = request_id;
  // Deterministic CPU-only illustration of the fixture page. This is sample
  // data, never a capture of a web page or a measurement of a renderer.
  const auto& tokens = kTokens.tab_hover;
  const auto& palette = options_.dark ? kTokens.dark : kTokens.light;
  const auto width = static_cast<std::uint32_t>(
      std::max(1.0f, tokens.width - 2 * tokens.border_width));
  const auto height = static_cast<std::uint32_t>(std::max(
      1.0f, tokens.thumbnail_aspect_ratio > 0
                ? static_cast<float>(width) / tokens.thumbnail_aspect_ratio
                : 1.0f));
  preview_pixels_.resize(static_cast<std::size_t>(width) * height * 4);
  const auto inset =
      std::min(static_cast<std::uint32_t>(std::max(0.0f, tokens.padding)),
               std::min(width, height) / 2);
  const auto line =
      static_cast<std::uint32_t>(std::max(1.0f, tokens.detail_size));
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const bool heading =
          y >= inset && y < inset + line && x >= inset && x < width - inset;
      const bool body = y >= inset * 2 + line && x >= inset &&
                        x < width - inset * 2 &&
                        (y - inset * 2 - line) % (line + inset) < inset;
      const auto color = heading ? tab->color
                         : body  ? palette.gradient_base
                                 : palette.page_background;
      const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
      preview_pixels_[offset] = static_cast<std::uint8_t>(color);
      preview_pixels_[offset + 1] = static_cast<std::uint8_t>(color >> 8);
      preview_pixels_[offset + 2] = static_cast<std::uint8_t>(color >> 16);
      preview_pixels_[offset + 3] = 255;
    }
  }
  const KrImage image{.struct_size = sizeof(KrImage),
                      .id = kTabPreviewImage,
                      .width = static_cast<std::int32_t>(width),
                      .height = static_cast<std::int32_t>(height),
                      .stride = static_cast<std::int32_t>(width * 4),
                      .pixels = preview_pixels_.data()};
  ui_.put_image(ui_.self, &image);
  const KrTabPreview preview{
      .struct_size = sizeof(KrTabPreview),
      .tab = id,
      .thumbnail = kTabPreviewImage,
      .memory_usage_bytes = 42'000'000,
      .flags = KR_TAB_PREVIEW_SAMPLE | KR_TAB_PREVIEW_MEMORY_KNOWN,
      // No browser-owned labels: exercise the shared presentation fallback.
      .title = {},
      .domain = {},
      .memory_text = {},
      .status = {}};
  ui_.set_tab_preview(ui_.self, request_id, &preview);
}

void MockBrowser::CancelTabPreview(std::uint32_t request_id) {
  if (request_id == 0 || request_id != preview_request_)
    return;
  preview_request_ = 0;
  if (attached_ && ui_.drop_image)
    ui_.drop_image(ui_.self, kTabPreviewImage);
}

KrTheme MockBrowser::theme_state() const noexcept {
  const auto space = std::ranges::find(spaces_, active_space_, &MockSpace::id);
  return KrTheme{.struct_size = sizeof(KrTheme),
                 .dark = options_.dark,
                 .accent = space == spaces_.end() ? 0 : space->color};
}

void MockBrowser::PushTheme() const {
  if (!attached_)
    return;
  const KrTheme theme = theme_state();
  ui_.set_theme(ui_.self, &theme);
}

void MockBrowser::PushThemeSwatches() const {
  if (!attached_)
    return;
  for (std::size_t index = 0; index < kTokens.theme_presets.size(); ++index) {
    const auto pixels = MakeFavicon(kTokens.theme_presets[index].color);
    const KrImage image{.struct_size = sizeof(KrImage),
                        .id = kThemeSwatchBase + index,
                        .width = 32,
                        .height = 32,
                        .stride = 32 * 4,
                        .pixels = pixels.data()};
    ui_.put_image(ui_.self, &image);
  }
}

void MockBrowser::PushLayout() const {
  PushAppearance();
  if (!attached_)
    return;
  const KrLayoutPrefs prefs{sizeof(KrLayoutPrefs),
                            options_.appearance.layout_flags,
                            options_.appearance.sidebar_width};
  ui_.set_layout_prefs(ui_.self, &prefs);
}

void MockBrowser::AppearanceChanged() {
  for (auto& space : spaces_) {
    if (space.id && space.id <= options_.appearance.workspace_colors.size())
      space.color = options_.appearance.workspace_colors[space.id - 1];
  }
  options_.dark = options_.appearance.scheme == ThemeScheme::kSystem
                      ? system_dark_
                      : options_.appearance.scheme == ThemeScheme::kDark;
  PushTheme();
  PushWorkspaceThemes();
  PushLayout();
  PushSpaces();
  if (callbacks_.page_changed)
    callbacks_.page_changed();
  if (callbacks_.appearance_changed)
    callbacks_.appearance_changed(options_.appearance);
}

// ----------------------------------------------------------------- host API

void MockBrowser::SetWindowState(float width,
                                 float height,
                                 float scale_factor,
                                 bool active,
                                 bool maximized) {
  window_width_ = width;
  window_height_ = height;
  window_scale_ = scale_factor;
  window_active_ = active;
  window_maximized_ = maximized;
  if (!attached_)
    return;
  const KrWindowState state{
      .struct_size = sizeof(KrWindowState),
      .flags = (active ? KR_WINDOW_ACTIVE : 0u) |
               (maximized ? KR_WINDOW_MAXIMIZED : 0u) |
               (fullscreen_ ? KR_WINDOW_FULLSCREEN : 0u),
      .scale_factor = scale_factor,
      .client = {0, 0, width, height},
  };
  ui_.set_window_state(ui_.self, &state);
}

void MockBrowser::FocusAddress(bool select_all) {
  if (attached_)
    ui_.focus_address(ui_.self, select_all);
}

void MockBrowser::CloseActiveTab() {
  Close(active_tab_);
}

void MockBrowser::SetSystemDark(bool dark) {
  if (system_dark_ == dark)
    return;
  system_dark_ = dark;
  if (options_.appearance.scheme == ThemeScheme::kSystem)
    AppearanceChanged();
}

void MockBrowser::ApplyAppearance(const AppearancePrefs& preferences) {
  options_.appearance = preferences;
  AppearanceChanged();
}

void MockBrowser::ToggleCompactMode() {
  options_.appearance.layout_flags ^= KR_LAYOUT_COMPACT_MODE;
  AppearanceChanged();
}

void MockBrowser::ToggleSidebar() {
  if (options_.appearance.layout_flags & KR_LAYOUT_TOOLBAR_COLLAPSED)
    options_.appearance.layout_flags &=
        ~(KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY);
  else if ((options_.appearance.layout_flags & KR_LAYOUT_TOOLBAR_SINGLE) &&
           !(options_.appearance.layout_flags & KR_LAYOUT_SIDEBAR_ICON_ONLY)) {
    // The icon rail needs the independent toolbar for navigation and window
    // controls. Collapse the complete single-toolbar layout into that mode.
    options_.appearance.layout_flags &= ~KR_LAYOUT_TOOLBAR_SINGLE;
    options_.appearance.layout_flags |=
        KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY;
  } else
    options_.appearance.layout_flags ^= KR_LAYOUT_SIDEBAR_ICON_ONLY;
  AppearanceChanged();
}

void MockBrowser::ToggleFullscreen() {
  fullscreen_ = !fullscreen_;
  if (callbacks_.fullscreen_changed)
    callbacks_.fullscreen_changed(fullscreen_);
  SetWindowState(window_width_, window_height_, window_scale_, window_active_,
                 window_maximized_);
}

void MockBrowser::OpenSettings() {
  if (!ShowSettings(KR_SETTINGS_CURRENT))
    OpenTab("krypton://settings/appearance", KR_OPEN_NEW_TAB);
}

void MockBrowser::ExecuteShortcut(PreviewShortcut shortcut) {
  switch (shortcut) {
    case PreviewShortcut::kNewTab:
      OpenTab("", KR_OPEN_NEW_TAB);
      FocusAddress(true);
      break;
    case PreviewShortcut::kReopenTab:
      ReopenClosedTab();
      break;
    case PreviewShortcut::kNewWindow:
      if (callbacks_.new_window)
        callbacks_.new_window();
      break;
    case PreviewShortcut::kReload:
      table_.nav_command(this, KR_NAV_RELOAD);
      break;
    case PreviewShortcut::kHistory:
      OpenTab("krypton://history", KR_OPEN_NEW_TAB);
      break;
    case PreviewShortcut::kDownloads:
      OpenTab("krypton://downloads", KR_OPEN_NEW_TAB);
      break;
  }
}

void MockBrowser::CycleActiveTab() {
  if (tabs_.empty())
    return;
  const std::size_t active = IndexOf(active_tab_);
  for (std::size_t offset = 1; offset <= tabs_.size(); ++offset) {
    const MockTab& tab = tabs_[(active + offset) % tabs_.size()];
    if (tab.space == active_space_ && !(tab.flags & KR_TAB_FAVORITE)) {
      Activate(tab.id);
      return;
    }
  }
}

bool MockBrowser::PreTranslateMessage(const void* native_message) {
  return attached_ && KR_HAS_SLOT(&ui_, KrShellUI, pre_translate_message) &&
         ui_.pre_translate_message &&
         ui_.pre_translate_message(ui_.self, native_message);
}

MockBrowser::Page MockBrowser::active_page() const {
  if (const MockTab* tab = Find(active_tab_)) {
    return {tab->title, tab->url, tab->color, tab->url == "krypton://blank"};
  }
  return {"New Tab", "", 0xFF7C5CFF};
}

// ------------------------------------------------------- browser behaviour

void MockBrowser::Activate(KrTabId id) {
  if (glance_.phase != KR_GLANCE_CLOSED && id != glance_.child)
    CloseGlance();
  MockTab* tab = Find(id);
  if (!tab)
    return;
  if (active_tab_ != id)
    CancelPermissionPrompt();
  active_tab_ = id;
  PublishSplitFocus(id);
  // Favorites are shared by every space; anything else lives in one.
  if (!(tab->flags & KR_TAB_FAVORITE) && tab->space != active_space_) {
    active_space_ = tab->space;
    PushSpaces();
    PushTheme();
  }
  for (MockSpace& space : spaces_) {
    if (space.id == active_space_)
      space.last_active = id;
  }
  if (attached_)
    ui_.set_active_tab(ui_.self, id);
  PushNav();
  if (callbacks_.page_changed)
    callbacks_.page_changed();
}

void MockBrowser::Close(KrTabId id) {
  ForgetMultiPageTab(id);
  const std::size_t index = IndexOf(id);
  if (index >= tabs_.size())
    return;
  const KrSpaceId space = active_space_;
  if (id == active_tab_)
    CancelPermissionPrompt();
  if (!tabs_[index].transient) {
    closed_tabs_.push_back(tabs_[index]);
    closed_tabs_.back().flags &= ~KR_TAB_LOADING;
    closed_tabs_.back().load_progress = 1;
  }
  tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
  if (attached_) {
    ui_.tab_removed(ui_.self, id);
    ui_.drop_image(ui_.self, id);
    const auto task = KrTaskId{1000} + id;
    if (std::ranges::find(published_tasks_, task) != published_tasks_.end() &&
        KR_HAS_SLOT(&ui_, KrShellUI, remove_task) && ui_.remove_task) {
      (void)ui_.remove_task(ui_.self, task);
      std::erase(published_tasks_, task);
    }
  }
  if (id != active_tab_)
    return;

  // Pick a neighbour in the same space, like browsers do.
  KrTabId next = 0;
  for (std::size_t i = std::min(index, tabs_.size()); i-- > 0;) {
    if (tabs_[i].space == space || (tabs_[i].flags & KR_TAB_FAVORITE)) {
      next = tabs_[i].id;
      break;
    }
  }
  if (!next) {
    for (const MockTab& tab : tabs_) {
      if (tab.space == space || (tab.flags & KR_TAB_FAVORITE)) {
        next = tab.id;
        break;
      }
    }
  }
  active_tab_ = 0;
  if (next) {
    Activate(next);
  } else if (!tabs_.empty()) {
    OpenTab({}, KR_OPEN_NEW_TAB);
  } else {
    if (attached_)
      ui_.set_active_tab(ui_.self, 0);
    PushNav();
    if (callbacks_.page_changed)
      callbacks_.page_changed();
  }
}

void MockBrowser::OpenTab(std::string_view text, std::uint32_t disposition) {
  if (TryOpenSettings(text) || TryOpenOnboarding(text))
    return;
  MockTab* current = Find(active_tab_);
  if (disposition == KR_OPEN_CURRENT_TAB && current) {
    LoadInto(*current, text);
    PushTab(*current);
    PushNav();
    if (callbacks_.page_changed)
      callbacks_.page_changed();
    StartLoading(*current);
    return;
  }
  MockTab& tab =
      AddTab(active_space_, 0, "New Tab", "krypton://newtab", 0xFF94A3B8);
  if (!text.empty())
    LoadInto(tab, text);
  const KrTab created{.struct_size = sizeof(KrTab),
                      .id = tab.id,
                      .space = tab.space,
                      .flags = tab.flags,
                      .title = Str(tab.title),
                      .url = Str(tab.url),
                      .favicon = tab.id};
  if (attached_) {
    PushFavicon(tab);
    ui_.tab_inserted(ui_.self, tabs_.size() - 1, &created);
    if (task_manager_open_) {
      PushTask(tab);
      published_tasks_.push_back(KrTaskId{1000} + tab.id);
    }
  }
  if (disposition != KR_OPEN_NEW_BACKGROUND_TAB)
    Activate(tab.id);
  StartLoading(tab);
}

void MockBrowser::LoadInto(MockTab& tab, std::string_view text) {
  if (tab.id == active_tab_)
    CancelPermissionPrompt();
  if (LooksLikeUrl(text)) {
    tab.url = text.starts_with("http") || text.starts_with("krypton:")
                  ? std::string(text)
                  : "https://" + std::string(text);
    tab.title = tab.url == "krypton://blank" ? "Krypton" : HostOf(tab.url);
  } else {
    tab.url = "https://search.example.com/?q=" + std::string(text);
    tab.title = std::format("{} — Search", text);
  }
  ++tab.back_depth;
  tab.forward_depth = 0;
}

void MockBrowser::SwitchSpace(KrSpaceId id) {
  const auto space = std::ranges::find(spaces_, id, &MockSpace::id);
  if (space == spaces_.end() || id == active_space_)
    return;
  active_space_ = id;
  PushSpaces();
  PushTheme();
  if (space->last_active && Find(space->last_active)) {
    Activate(space->last_active);
  } else {
    const auto tab = std::ranges::find(tabs_, id, &MockTab::space);
    if (tab != tabs_.end()) {
      Activate(tab->id);
    } else {
      active_tab_ = 0;
      if (attached_)
        ui_.set_active_tab(ui_.self, 0);
      PushNav();
      if (callbacks_.page_changed)
        callbacks_.page_changed();
    }
  }
}

void MockBrowser::Suggest(std::uint32_t request_id, std::string_view text) {
  selected_suggestion_.reset();
  suggestion_request_ = request_id;
  last_suggestions_.clear();
  std::vector<std::string> contents;
  std::vector<std::string> descriptions;
  std::vector<KrSuggestion> items;
  const std::string needle = Lower(text);

  auto add = [&](std::uint32_t kind, std::uint32_t flags, std::string content,
                 std::string description, KrTabId switch_to = 0,
                 KrImageId icon = 0) {
    last_suggestions_.push_back({content, switch_to});
    contents.push_back(std::move(content));
    descriptions.push_back(std::move(description));
    items.push_back({.struct_size = sizeof(KrSuggestion),
                     .kind = kind,
                     .flags = flags,
                     .icon = icon});
  };

  if (LooksLikeUrl(text)) {
    add(KR_SUGGESTION_URL, KR_SUGGESTION_DEFAULT, std::string(text),
        "Open this address");
  }
  add(KR_SUGGESTION_SEARCH, LooksLikeUrl(text) ? 0 : KR_SUGGESTION_DEFAULT,
      std::string(text), "Search the web");
  if (privacy_search_suggestions_ && !text.empty() && !LooksLikeUrl(text) &&
      !tabs_.empty())
    add(KR_SUGGESTION_SEARCH, KR_SUGGESTION_RICH_IMAGE,
        std::string(text) + " browser", "Browser", 0, tabs_.front().id);
  for (const MockTab& tab : tabs_) {
    if (items.size() >= 6)
      break;
    if (Lower(tab.title).contains(needle) || Lower(tab.url).contains(needle)) {
      add(KR_SUGGESTION_OPEN_TAB, 0, tab.title, "Switch to tab", tab.id,
          tab.id);
    }
  }
  for (std::string_view entry : kHistory) {
    if (items.size() >= 8)
      break;
    if (entry.contains(needle)) {
      add(KR_SUGGESTION_HISTORY, KR_SUGGESTION_DELETABLE, std::string(entry),
          "From your history");
    }
  }
  for (std::size_t i = 0; i < items.size(); ++i) {
    items[i].contents = Str(contents[i]);
    items[i].description = Str(descriptions[i]);
  }
  if (attached_) {
    if (KR_HAS_SLOT(&ui_, KrShellUI, set_suggestion_snapshot) &&
        ui_.set_suggestion_snapshot)
      ui_.set_suggestion_snapshot(ui_.self, request_id, 1, items.data(),
                                  items.size(), sizeof(KrSuggestion));
    else
      ui_.set_suggestions(ui_.self, request_id, items.data(), items.size(),
                          sizeof(KrSuggestion));
  }
}

void MockBrowser::AcceptSuggestion(std::size_t index,
                                   std::uint32_t disposition) {
  if (index >= last_suggestions_.size())
    return;
  const LastSuggestion chosen = last_suggestions_[index];
  if (chosen.switch_to && Find(chosen.switch_to)) {
    Activate(chosen.switch_to);
  } else {
    OpenTab(chosen.text, disposition);
  }
}

void MockBrowser::ShowMenu(std::uint32_t request_id,
                           std::uint32_t kind,
                           std::uint64_t target) {
  if (!attached_)
    return;
  if (ShowChromeMenu(request_id, kind, target) ||
      ShowLibraryMenu(request_id, kind, target))
    return;
  chrome_menu_request_ = 0;
  menu_kind_ = kind;
  menu_target_ = kind == KR_MENU_TAB ? target : 0;
  menu_space_ = kind == KR_MENU_SPACE && target ? target : active_space_;
  using Items = std::vector<KrMenuItem>;
  enum MenuIndex : std::int32_t {
    kRoot,
    kLayout,
    kHorizontalLayout,
    kVerticalLayout,
    kCompact,
    kTheme,
    kLightPresets,
    kDarkPresets,
    kSettings,
    kSpaces,
    kHistoryMenu,
    kPageNavigation,
    kMenuCount
  };
  std::vector<Items> menus(kMenuCount);
  const auto command = [](std::int32_t id, std::string_view label,
                          std::string_view accelerator = {}) {
    return KrMenuItem{.struct_size = sizeof(KrMenuItem),
                      .command_id = id,
                      .type = KR_MENU_ITEM_COMMAND,
                      .flags = KR_MENU_ITEM_ENABLED,
                      .label = Str(label),
                      .accelerator = Str(accelerator),
                      .submenu = -1};
  };
  const auto checked = [&](std::int32_t id, std::string_view label, bool value,
                           bool radio = false,
                           std::string_view accelerator = {}) {
    auto item = command(id, label, accelerator);
    item.type = radio ? KR_MENU_ITEM_RADIO : KR_MENU_ITEM_CHECK;
    if (value)
      item.flags |= KR_MENU_ITEM_CHECKED;
    return item;
  };
  const auto submenu = [&](std::string_view label, MenuIndex index) {
    auto item = command(0, label);
    item.type = KR_MENU_ITEM_SUBMENU;
    item.submenu = index;
    return item;
  };
  const KrMenuItem separator{.struct_size = sizeof(KrMenuItem),
                             .type = KR_MENU_ITEM_SEPARATOR,
                             .submenu = -1};
  const auto flags = options_.appearance.layout_flags;
  const bool collapsed =
      (flags & (KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY)) !=
      0;
  const bool single = (flags & KR_LAYOUT_TOOLBAR_SINGLE) != 0 && !collapsed;
  const bool compact = (flags & KR_LAYOUT_COMPACT_MODE) != 0;
  const bool hides_sidebar = (flags & KR_LAYOUT_COMPACT_HIDE_SIDEBAR) != 0;
  const bool hides_toolbar = (flags & KR_LAYOUT_COMPACT_HIDE_TOOLBAR) != 0;
  constexpr auto horizontal_mask =
      KR_LAYOUT_TABS_CLASSIC | KR_LAYOUT_TABS_COMPACT | KR_LAYOUT_TABS_DYNAMIC;
  const bool vertical = (flags & horizontal_mask) == 0;
  const auto layout_label = [this](std::string_view key,
                                   std::string_view fallback) {
    return LayoutMenuText(key, fallback);
  };
  menus[kHorizontalLayout] = {
      checked(std::to_underlying(ChromeCommand::kLayoutClassic),
              layout_label("layout.classic", "Classic"),
              (flags & KR_LAYOUT_TABS_CLASSIC) != 0, true),
      checked(std::to_underlying(ChromeCommand::kLayoutCompact),
              layout_label("layout.compact", "Compact"),
              (flags & KR_LAYOUT_TABS_COMPACT) != 0, true),
      checked(std::to_underlying(ChromeCommand::kLayoutDynamic),
              layout_label("layout.dynamic", "Dynamic"),
              (flags & KR_LAYOUT_TABS_DYNAMIC) != 0, true)};
  menus[kVerticalLayout] = {
      checked(kCmdLayoutSingle, "Only Sidebar",
              vertical && single && !collapsed, true),
      checked(kCmdLayoutMultiple, "Sidebar and Top Toolbar",
              vertical && !single && !collapsed, true),
      checked(kCmdLayoutCollapsed, "Collapsed Sidebar", vertical && collapsed,
              true)};
  menus[kLayout] = {
      submenu(layout_label("layout.horizontal_tabs", "Horizontal Tabs"),
              kHorizontalLayout),
      submenu(layout_label("layout.vertical_tabs", "Vertical Tabs"),
              kVerticalLayout),
      separator,
      checked(std::to_underlying(ChromeCommand::kCenteredAddress),
              layout_label("layout.centered_address", "Centered Address Bar"),
              (flags & KR_LAYOUT_CENTERED_ADDRESS) != 0),
      checked(std::to_underlying(ChromeCommand::kMinimalAddress),
              layout_label("layout.minimal_address", "Minimal Address Bar"),
              (flags & KR_LAYOUT_MINIMAL_ADDRESS) != 0),
      checked(std::to_underlying(ChromeCommand::kRoundedFrame),
              layout_label("layout.rounded_frame", "Rounded Frame"),
              (flags & KR_LAYOUT_SQUARE_FRAME) == 0)};
  menus[kCompact] = {
      checked(kCmdCompactToggle, "Enable Compact Mode", compact, false,
              "Ctrl+Alt+C"),
      separator,
      checked(kCmdCompactSidebar, "Hide Tab bar", !hides_toolbar, true),
      checked(kCmdCompactToolbar, "Hide Top bar",
              hides_toolbar && !hides_sidebar, true),
      checked(kCmdCompactBoth, "Hide Both", hides_sidebar && hides_toolbar,
              true)};
  menus[kCompact].push_back(separator);
  const bool explicit_targets =
      (flags & KR_LAYOUT_COMPACT_TARGETS_EXPLICIT) != 0;
  menus[kCompact].push_back(checked(
      std::to_underlying(ChromeCommand::kPinTop),
      layout_label("layout.pin_top", "Pin Top Chrome"),
      !(hides_toolbar || (!vertical && !explicit_targets && !hides_sidebar))));
  if (vertical)
    menus[kCompact].push_back(
        checked(std::to_underlying(ChromeCommand::kPinSide),
                layout_label("layout.pin_side", "Pin Side Chrome"),
                !(hides_sidebar || (!explicit_targets && !hides_toolbar))));
  const auto compact_entry =
      single && !collapsed ? checked(kCmdCompactToggle, "Enable Compact Mode",
                                     compact, false, "Ctrl+Alt+C")
                           : submenu("Compact Mode", kCompact);
  const auto scheme = options_.appearance.scheme;
  const auto theme_entry =
      KR_HAS_SLOT(&ui_, KrShellUI, show_theme_editor) && ui_.show_theme_editor
          ? command(kCmdEditTheme, "Change Theme Colors")
          : submenu("Change Theme Colors", kTheme);
  menus[kTheme] = {
      command(kCmdEditTheme, "Edit Theme..."),
      separator,
      checked(kCmdSchemeSystem, "System", scheme == ThemeScheme::kSystem, true),
      checked(kCmdSchemeLight, "Light", scheme == ThemeScheme::kLight, true),
      checked(kCmdSchemeDark, "Dark", scheme == ThemeScheme::kDark, true),
      separator,
      submenu("Light colors", kLightPresets),
      submenu("Dark colors", kDarkPresets),
      separator,
      command(kCmdThemeReset, "Reset workspace colors")};
  const auto space = std::ranges::find(spaces_, menu_space_, &MockSpace::id);
  const auto color = space == spaces_.end() ? 0 : space->color;
  for (std::size_t index = 0; index < kTokens.theme_presets.size(); ++index) {
    const auto& preset = kTokens.theme_presets[index];
    auto item = checked(kCmdThemePresetBase + static_cast<std::int32_t>(index),
                        preset.name, color == preset.color, true);
    item.icon = kThemeSwatchBase + index;
    menus[index < kTokens.theme_presets.size() / 2 ? kLightPresets
                                                   : kDarkPresets]
        .push_back(item);
  }
  menus[kSettings] = {
      submenu("Browser Layout", kLayout),
      compact_entry,
      checked(kCmdSidebarToggle, "Collapse sidebar",
              (flags & KR_LAYOUT_SIDEBAR_ICON_ONLY) != 0 || collapsed),
      checked(kCmdSidebarRight, "Sidebar on the right",
              (flags & KR_LAYOUT_SIDEBAR_RIGHT) != 0),
      separator,
      theme_entry,
      separator,
      checked(kCmdFullscreen, "Full screen", fullscreen_, false, "F11")};
  for (std::size_t index = 0; index < spaces_.size(); ++index) {
    menus[kSpaces].push_back(checked(
        (kind == KR_MENU_TAB ? kCmdMoveSpaceBase : kCmdSwitchSpaceBase) +
            static_cast<std::int32_t>(index),
        spaces_[index].name,
        kind == KR_MENU_TAB
            ? Find(target) && Find(target)->space == spaces_[index].id
            : active_space_ == spaces_[index].id,
        true));
  }
  if (KR_HAS_SLOT(&ui_, KrShellUI, show_workspace_editor) &&
      ui_.show_workspace_editor && kind != KR_MENU_TAB) {
    menus[kSpaces].push_back(separator);
    menus[kSpaces].push_back(command(kCmdNewWorkspace, "New workspace"));
    menus[kSpaces].push_back(command(kCmdEditWorkspace, "Edit workspace"));
  }
  menus[kHistoryMenu] = {
      command(kCmdReopen, "Reopen Closed Tab", "Ctrl+Shift+T"),
      command(kCmdHistory, "History", "Ctrl+H")};
  if (closed_tabs_.empty())
    menus[kHistoryMenu][0].flags = 0;

  if (kind == KR_MENU_TAB) {
    const MockTab* tab = Find(target);
    if (!tab)
      return;
    menus[kRoot] = {
        command(kCmdNewTab, "New Tab", "Ctrl+T"),
        command(kCmdReload, "Reload Tab", "Ctrl+R"),
        command(kCmdDuplicate, "Duplicate Tab"),
        separator,
        checked(kCmdTogglePin, "Pin Tab", (tab->flags & KR_TAB_PINNED) != 0),
        checked(kCmdToggleFavorite, "Essential Tab",
                (tab->flags & KR_TAB_FAVORITE) != 0),
        checked(kCmdMute, "Mute Tab", (tab->flags & KR_TAB_MUTED) != 0),
        submenu("Move to Workspace", kSpaces),
        separator,
        command(kCmdCloseTab, "Close Tab", "Ctrl+W"),
        command(kCmdCloseOthers, "Close Other Tabs"),
        command(kCmdCloseLeft, "Close Tabs to the Left"),
        command(kCmdCloseRight, "Close Tabs to the Right")};
  } else if (kind == KR_MENU_THEME) {
    menus[kRoot] = menus[kTheme];
  } else if (kind == KR_MENU_SETTINGS) {
    menus[kRoot] = menus[kSettings];
  } else if (kind == KR_MENU_SIDEBAR || kind == KR_MENU_SPACE) {
    menus[kRoot] = {command(kCmdNewTab, "New Tab", "Ctrl+T"),
                    command(kCmdReopen, "Reopen Closed Tab", "Ctrl+Shift+T"),
                    separator,
                    theme_entry,
                    submenu("Browser Layout", kLayout),
                    compact_entry,
                    submenu("Workspaces", kSpaces),
                    separator,
                    submenu("Settings", kSettings)};
    if (closed_tabs_.empty())
      menus[kRoot][1].flags = 0;
  } else if (kind == KR_MENU_BACK_HISTORY || kind == KR_MENU_FORWARD_HISTORY) {
    menus[kRoot] = {
        command(kind == KR_MENU_BACK_HISTORY ? kCmdBack : kCmdForward,
                kind == KR_MENU_BACK_HISTORY ? "Back" : "Forward")};
  } else if (kind == KR_MENU_RELOAD) {
    menus[kRoot] = {command(kCmdReload, "Reload", "Ctrl+R")};
  } else if (kind == KR_MENU_PAGE) {
    menus[kRoot] = {command(kCmdBack, "Back"),
                    command(kCmdForward, "Forward"),
                    command(kCmdReload, "Reload", "Ctrl+R"),
                    separator,
                    command(kCmdNewTab, "New Tab", "Ctrl+T"),
                    separator,
                    submenu("Workspaces", kSpaces),
                    submenu("Settings", kSettings)};
    if (const auto* active = Find(active_tab_)) {
      if (active->back_depth == 0)
        menus[kRoot][0].flags = 0;
      if (active->forward_depth == 0)
        menus[kRoot][1].flags = 0;
    }
    if (KR_HAS_SLOT(&ui_, KrShellUI, show_menu_panel_with_tiles) &&
        ui_.show_menu_panel_with_tiles) {
      menus[kPageNavigation].assign(menus[kRoot].begin(),
                                    menus[kRoot].begin() + 3);
      auto bookmark = command(std::to_underlying(ChromeCommand::kBookmarkPage),
                              "Bookmark This Page", "Ctrl+D");
      bookmark.type = KR_MENU_ITEM_CHECK;
      if (bookmarked_)
        bookmark.flags |= KR_MENU_ITEM_CHECKED;
      bookmark.glyph = Str(bookmarked_ ? kGlyphStarFilled : kGlyphStar);
      menus[kPageNavigation].push_back(bookmark);
      menus[kRoot].erase(menus[kRoot].begin(), menus[kRoot].begin() + 3);
      auto navigation = submenu("", kPageNavigation);
      navigation.type = KR_MENU_ITEM_CONTROLS;
      menus[kRoot].insert(menus[kRoot].begin(), navigation);
    }
  } else if (kind == KR_MENU_CREATE) {
    menus[kRoot] = {command(kCmdNewTab, "New Tab", "Ctrl+T"),
                    command(kCmdNewWindow, "New Window", "Ctrl+N")};
  } else {
    menus[kRoot] = {
        command(kCmdNewTab, "New Tab", "Ctrl+T"),
        command(kCmdNewWindow, "New Window", "Ctrl+N"),
        separator,
        submenu("History", kHistoryMenu),
        command(kCmdDownloads, "Downloads", "Ctrl+J"),
        submenu("Workspaces", kSpaces),
        separator,
        submenu("Browser Layout", kLayout),
        compact_entry,
        submenu("Change Theme Colors", kTheme),
        submenu("Settings", kSettings),
        separator,
        checked(kCmdFullscreen, "Full screen", fullscreen_, false, "F11")};
  }
  std::vector<std::vector<KrMenuItemMetadata>> metadata(menus.size());
  for (std::size_t menu = 0; menu < menus.size(); ++menu) {
    metadata[menu].resize(menus[menu].size());
    for (std::size_t index = 0; index < menus[menu].size(); ++index) {
      auto& item = menus[menu][index];
      metadata[menu][index] = {
          .struct_size = sizeof(KrMenuItemMetadata),
          .source_type = -1,  // The fixture is not a Chromium MenuModel.
          .source_command_id = item.command_id,
          .source_index = index,
          .source_button_type = -1,
          .flags = KR_MENU_META_VISIBLE | KR_MENU_META_MAY_HAVE_MNEMONICS,
          .accessible_name = item.label,
      };
      item.metadata = &metadata[menu][index];
    }
  }
  const bool page_panel =
      kind == KR_MENU_PAGE &&
      KR_HAS_SLOT(&ui_, KrShellUI, show_menu_panel_with_tiles) &&
      ui_.show_menu_panel_with_tiles;
  if (page_panel || (KR_HAS_SLOT(&ui_, KrShellUI, show_menu_with_glyphs) &&
                     ui_.show_menu_with_glyphs)) {
    const auto glyph_for = [](const KrMenuItem& item) -> std::string_view {
      if (item.type == KR_MENU_ITEM_SUBMENU) {
        switch (item.submenu) {
          case kSpaces:
            return "\uE774";
          case kTheme:
            return "\uE790";
          case kSettings:
            return "\uE713";
          case kLayout:
            return "\uE8A9";
          case kHistoryMenu:
            return "\uE81C";
          default:
            return {};
        }
      }
      switch (item.command_id) {
        case kCmdNewTab:
          return "\uE710";
        case kCmdNewWindow:
          return "\uE78B";
        case kCmdCloseTab:
          return "\uE8BB";
        case kCmdTogglePin:
          return "\uE718";
        case kCmdToggleFavorite:
          return "\uE734";
        case kCmdDuplicate:
          return "\uE8C8";
        case kCmdMute:
          return (item.flags & KR_MENU_ITEM_CHECKED) ? "\uE767" : "\uE74F";
        case kCmdReload:
          return "\uE72C";
        case kCmdBack:
          return "\uE72B";
        case kCmdForward:
          return "\uE72A";
        case kCmdSettings:
          return "\uE713";
        case kCmdTheme:
        case kCmdEditTheme:
          return "\uE790";
        default:
          return {};
      }
    };
    std::vector<std::vector<KrMenuItem>> items(menus.size());
    std::vector<KrMenu> flat;
    flat.reserve(menus.size());
    for (std::size_t index = 0; index < menus.size(); ++index) {
      items[index].reserve(menus[index].size());
      for (auto item : menus[index]) {
        if (!item.glyph.size)
          item.glyph = Str(glyph_for(item));
        items[index].push_back(item);
      }
      flat.push_back({sizeof(KrMenu), items[index].data(), items[index].size(),
                      sizeof(KrMenuItem)});
    }
    if (page_panel)
      ui_.show_menu_panel_with_tiles(ui_.self, request_id, flat.data(),
                                     flat.size(), sizeof(KrMenu));
    else
      ui_.show_menu_with_glyphs(ui_.self, request_id, flat.data(), flat.size(),
                                sizeof(KrMenu));
    return;
  }
  std::vector<KrMenu> flat;
  flat.reserve(menus.size());
  for (const Items& items : menus)
    flat.push_back(
        {sizeof(KrMenu), items.data(), items.size(), sizeof(KrMenuItem)});
  ui_.show_menu(ui_.self, request_id, flat.data(), flat.size(), sizeof(KrMenu));
}

void MockBrowser::RunMenuCommand(std::int32_t command_id) {
  if (RunChromeCommand(command_id) || RunLibraryCommand(command_id))
    return;
  const KrTabId target_id =
      menu_kind_ == KR_MENU_TAB ? menu_target_ : active_tab_;
  MockTab* target = Find(target_id);
  auto& flags = options_.appearance.layout_flags;
  constexpr std::uint32_t layout_mask =
      KR_LAYOUT_TOOLBAR_SINGLE | KR_LAYOUT_TOOLBAR_COLLAPSED |
      KR_LAYOUT_SIDEBAR_ICON_ONLY | KR_LAYOUT_SIDEBAR_AUTOHIDE |
      KR_LAYOUT_SIDEBAR_COLLAPSED;
  constexpr std::uint32_t compact_mask =
      KR_LAYOUT_COMPACT_HIDE_SIDEBAR | KR_LAYOUT_COMPACT_HIDE_TOOLBAR;
  if (command_id >= kCmdThemePresetBase &&
      static_cast<std::size_t>(command_id - kCmdThemePresetBase) <
          kTokens.theme_presets.size()) {
    const auto color = kTokens
                           .theme_presets[static_cast<std::size_t>(
                               command_id - kCmdThemePresetBase)]
                           .color;
    if (menu_space_ > 0 &&
        menu_space_ <= options_.appearance.workspace_colors.size())
      options_.appearance.workspace_colors[menu_space_ - 1] = color;
    if (const auto space =
            std::ranges::find(spaces_, menu_space_, &MockSpace::id);
        space != spaces_.end())
      space->color = color;
    ResetWorkspaceTheme(menu_space_);
    AppearanceChanged();
    return;
  }
  if (command_id >= kCmdSwitchSpaceBase &&
      static_cast<std::size_t>(command_id - kCmdSwitchSpaceBase) <
          spaces_.size()) {
    SwitchSpace(
        spaces_[static_cast<std::size_t>(command_id - kCmdSwitchSpaceBase)].id);
    return;
  }
  if (command_id >= kCmdMoveSpaceBase &&
      static_cast<std::size_t>(command_id - kCmdMoveSpaceBase) <
          spaces_.size()) {
    if (target) {
      target->space =
          spaces_[static_cast<std::size_t>(command_id - kCmdMoveSpaceBase)].id;
      PushTab(*target);
      if (active_tab_ == target->id)
        SwitchSpace(target->space);
    }
    return;
  }
  switch (command_id) {
    case kCmdNewWorkspace:
      ShowWorkspaceEditor();
      break;
    case kCmdEditWorkspace:
      ShowWorkspaceEditor(active_space_);
      break;
    case kCmdNewTab:
      OpenTab("", KR_OPEN_NEW_TAB);
      FocusAddress(true);
      break;
    case kCmdNewWindow:
      if (callbacks_.new_window)
        callbacks_.new_window();
      break;
    case kCmdReopen:
      ReopenClosedTab();
      break;
    case kCmdDownloads:
      OpenTab("krypton://downloads", KR_OPEN_NEW_TAB);
      break;
    case kCmdHistory:
      OpenTab("krypton://history", KR_OPEN_NEW_TAB);
      break;
    case kCmdSettings:
      OpenSettings();
      break;
    case kCmdDuplicate:
      Duplicate(target_id);
      break;
    case kCmdCloseTab:
      Close(target_id);
      break;
    case kCmdTogglePin:
    case kCmdToggleFavorite:
      if (target) {
        target->flags ^=
            command_id == kCmdTogglePin ? KR_TAB_PINNED : KR_TAB_FAVORITE;
        PushTab(*target);
      }
      break;
    case kCmdMute:
      if (target) {
        target->flags ^= KR_TAB_MUTED;
        PushTab(*target);
      }
      break;
    case kCmdCloseOthers:
    case kCmdCloseLeft:
    case kCmdCloseRight: {
      const auto position = IndexOf(target_id);
      std::vector<KrTabId> doomed;
      for (std::size_t index = 0; index < tabs_.size(); ++index) {
        const auto& tab = tabs_[index];
        const bool in_range =
            command_id == kCmdCloseOthers ||
            (command_id == kCmdCloseLeft && index < position) ||
            (command_id == kCmdCloseRight && index > position);
        if (target && in_range && tab.id != target_id &&
            tab.space == target->space &&
            !(tab.flags & (KR_TAB_PINNED | KR_TAB_FAVORITE)))
          doomed.push_back(tab.id);
      }
      for (KrTabId id : doomed)
        Close(id);
      break;
    }
    case kCmdReload:
      table_.nav_command(this, KR_NAV_RELOAD);
      break;
    case kCmdBack:
    case kCmdForward:
      table_.nav_command(this,
                         command_id == kCmdBack ? KR_NAV_BACK : KR_NAV_FORWARD);
      break;
    case kCmdLayoutMultiple:
    case kCmdLayoutSingle:
    case kCmdLayoutCollapsed:
      flags &= ~(layout_mask | KR_LAYOUT_TABS_CLASSIC | KR_LAYOUT_TABS_COMPACT |
                 KR_LAYOUT_TABS_DYNAMIC);
      if (command_id == kCmdLayoutSingle)
        flags |= KR_LAYOUT_TOOLBAR_SINGLE;
      if (command_id == kCmdLayoutCollapsed)
        flags |= KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY;
      AppearanceChanged();
      break;
    case kCmdCompactToggle:
      ToggleCompactMode();
      break;
    case kCmdCompactSidebar:
    case kCmdCompactToolbar:
    case kCmdCompactBoth:
      flags &= ~compact_mask;
      if (command_id != kCmdCompactToolbar)
        flags |= KR_LAYOUT_COMPACT_HIDE_SIDEBAR;
      if (command_id != kCmdCompactSidebar)
        flags |= KR_LAYOUT_COMPACT_HIDE_TOOLBAR;
      flags |= KR_LAYOUT_COMPACT_MODE;
      AppearanceChanged();
      break;
    case kCmdSidebarRight:
      flags ^= KR_LAYOUT_SIDEBAR_RIGHT;
      AppearanceChanged();
      break;
    case kCmdSidebarToggle:
      ToggleSidebar();
      break;
    case kCmdTheme:
      options_.appearance.scheme =
          options_.dark ? ThemeScheme::kLight : ThemeScheme::kDark;
      AppearanceChanged();
      break;
    case kCmdEditTheme:
      ShowThemeEditor(menu_space_);
      break;
    case kCmdSchemeSystem:
    case kCmdSchemeLight:
    case kCmdSchemeDark:
      options_.appearance.scheme =
          command_id == kCmdSchemeSystem  ? ThemeScheme::kSystem
          : command_id == kCmdSchemeLight ? ThemeScheme::kLight
                                          : ThemeScheme::kDark;
      AppearanceChanged();
      break;
    case kCmdThemeReset:
      ResetWorkspaceTheme(menu_space_);
      if (menu_space_ > 0 &&
          menu_space_ <= options_.appearance.workspace_colors.size())
        options_.appearance.workspace_colors[menu_space_ - 1] = 0;
      if (const auto space =
              std::ranges::find(spaces_, menu_space_, &MockSpace::id);
          space != spaces_.end())
        space->color = 0;
      AppearanceChanged();
      break;
    case kCmdFullscreen:
      ToggleFullscreen();
      break;
    default:
      break;
  }
  menu_target_ = 0;
}

void MockBrowser::Duplicate(KrTabId id) {
  const auto* source = Find(id);
  if (!source)
    return;
  const auto copy = *source;
  auto& duplicate =
      AddTab(copy.space, copy.flags & ~(KR_TAB_FAVORITE | KR_TAB_LOADING),
             copy.title, copy.url, copy.color);
  if (attached_) {
    PushFavicon(duplicate);
    const KrTab tab{.struct_size = sizeof(KrTab),
                    .id = duplicate.id,
                    .space = duplicate.space,
                    .flags = duplicate.flags,
                    .title = Str(duplicate.title),
                    .url = Str(duplicate.url),
                    .favicon = duplicate.id};
    ui_.tab_inserted(ui_.self, tabs_.size() - 1, &tab);
  }
  Activate(duplicate.id);
  StartLoading(duplicate);
}

void MockBrowser::ReopenClosedTab() {
  if (closed_tabs_.empty())
    return;
  auto restored = std::move(closed_tabs_.back());
  closed_tabs_.pop_back();
  const auto id = restored.id;
  tabs_.push_back(std::move(restored));
  if (attached_) {
    PushFavicon(tabs_.back());
    const auto& tab = tabs_.back();
    const KrTab value{.struct_size = sizeof(KrTab),
                      .id = tab.id,
                      .space = tab.space,
                      .flags = tab.flags,
                      .title = Str(tab.title),
                      .url = Str(tab.url),
                      .favicon = id};
    ui_.tab_inserted(ui_.self, tabs_.size() - 1, &value);
  }
  Activate(id);
  StartLoading(tabs_.back());
}

void MockBrowser::Log(std::uint32_t level, std::string_view text) const {
  if (callbacks_.log)
    callbacks_.log(level, text);
}

MockBrowser::MockTab* MockBrowser::Find(KrTabId id) {
  const auto it = std::ranges::find(tabs_, id, &MockTab::id);
  return it == tabs_.end() ? nullptr : &*it;
}

const MockBrowser::MockTab* MockBrowser::Find(KrTabId id) const {
  const auto it = std::ranges::find(tabs_, id, &MockTab::id);
  return it == tabs_.end() ? nullptr : &*it;
}

std::size_t MockBrowser::IndexOf(KrTabId id) const {
  const auto it = std::ranges::find(tabs_, id, &MockTab::id);
  return static_cast<std::size_t>(it - tabs_.begin());
}

}  // namespace kr::fixtures
