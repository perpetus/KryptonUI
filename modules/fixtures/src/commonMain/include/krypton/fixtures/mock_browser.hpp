#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "krypton_ui/abi.h"

namespace kr::fixtures {
struct LibraryFixture;

enum class ThemeScheme : std::uint32_t { kSystem, kLight, kDark };

enum class PreviewShortcut {
  kNewTab,
  kReopenTab,
  kNewWindow,
  kReload,
  kHistory,
  kDownloads
};

// Serializable host-owned preferences. The native module receives only the
// effective theme and layout through the public C contract.
struct AppearancePrefs {
  ThemeScheme scheme = ThemeScheme::kSystem;
  std::uint32_t layout_flags = 0;
  float sidebar_width = 0;
  std::uint32_t menu_material = KR_MENU_MATERIAL_DEFAULT;
  std::uint32_t appearance_flags = 0;
  std::uint32_t icon_pack = KR_ICON_PACK_FLUENT;
  std::uint32_t glass_mode = KR_GLASS_OFF;
  std::array<std::uint32_t, 3> workspace_colors{};
  std::array<KrWorkspaceTheme, 3> workspace_themes{};
};

struct MockOptions {
  bool dark = false;
  int extra_tabs = 0;  // stress test: this many more tabs in the first space
  AppearancePrefs appearance;
  bool multi_page = false;
  std::size_t split_panes = 0;
  bool glance = false;
};

// What the mock needs from the window that hosts it.
struct MockHostCallbacks {
  std::function<void(std::uint32_t /*KrWindowCommand*/)> window_command;
  std::function<void()> page_changed;  // card moved or active page changed
  std::function<void()> focus_page;
  std::function<void(std::uint32_t /*KrLogLevel*/, std::string_view)> log;
  std::function<void(const AppearancePrefs&)> appearance_changed;
  std::function<void(const KrGlassBackdropRequest&)> glass_backdrop_requested;
  std::function<void(bool)> fullscreen_changed;
  std::function<void()> new_window;
  std::function<bool(KrTabId, std::uint64_t)> load_requested;
  std::function<bool(const void*)> unhandled_key;
  std::function<void(KrRect)> site_data_anchor_changed;
  // Onboarding. Return true to drive the step yourself (AdvanceImport,
  // CompleteDefaultBrowserRequest); otherwise the mock finishes immediately.
  std::function<bool(KrImportSourceId)> import_requested;
  std::function<bool()> default_browser_requested;
  std::function<void(const KrOnboardingResult&)> onboarding_finished;
};

// A fake browser with spaces, tabs, favicons, suggestions and menus. It plays
// Chromium's part of abi.h: it owns all browser state and pushes it into the
// UI, and it reacts to the intents the UI sends back.
class MockBrowser {
 public:
  struct Page {
    std::string title;
    std::string url;
    std::uint32_t color = 0;  // a tint for the placeholder page
    bool blank = false;
  };
  struct PageSurface {
    KrTabId tab = 0;
    Page page;
    KrRect bounds{};
    bool glance = false;
  };
  struct PageLayout {
    std::array<PageSurface, 5> surfaces{};
    std::size_t count = 0;
    std::array<KrDividerBounds, 3> dividers{};
    std::size_t divider_count = 0;
    KrGlanceGeometry glance{.struct_size = sizeof(KrGlanceGeometry)};
  };

  MockBrowser(MockOptions options, MockHostCallbacks callbacks);
  ~MockBrowser();

  MockBrowser(const MockBrowser&) = delete;
  MockBrowser& operator=(const MockBrowser&) = delete;

  // The host table to pass in KrCreateParams. Valid for the mock's lifetime.
  [[nodiscard]] const KrBrowser& table() const { return table_; }

  // Connect to a created UI and push the initial state.
  void Attach(const KrShellUI& ui);
  void SeedFolderDemo();
  void Detach();
  [[nodiscard]] bool CreateSplit(std::span<const KrTabId> tabs,
                                 KrSplitLayout layout = KR_SPLIT_COLUMNS);
  [[nodiscard]] bool ShowSplitOptions(KrTabId tab, KrRect anchor);
  [[nodiscard]] bool OpenGlance(KrTabId parent, std::string_view url);
  [[nodiscard]] bool PromoteGlance(bool split);
  void CloseGlance();
  [[nodiscard]] const KrGlanceState& glance_state() const { return glance_; }
  [[nodiscard]] std::vector<KrTabId> SplitMembers(KrTabId tab) const;

  void SetWindowState(float width,
                      float height,
                      float scale_factor,
                      bool active,
                      bool maximized);
  void FocusAddress(bool select_all);
  void CloseActiveTab();
  // An explicit host update for the preview's bounded render benchmark.
  void CycleActiveTab();
  // One finite sample load step, driven explicitly by the preview host.
  [[nodiscard]] bool AdvanceLoading(KrTabId tab, std::uint64_t navigation);
  void SetSystemDark(bool dark);
  [[nodiscard]] bool SetUiText(std::string_view key, std::string_view text);
  void SetTextDirection(bool right_to_left);
  void ApplyAppearance(const AppearancePrefs& preferences);
  void ToggleCompactMode();
  void ToggleSidebar();
  void ToggleFullscreen();
  void OpenSettings();
  [[nodiscard]] bool ShowSettings(std::uint32_t section);
  [[nodiscard]] std::optional<std::uint32_t> last_settings_action()
      const noexcept {
    return last_settings_action_;
  }
  void SetSettingsManaged(std::uint64_t controls);
  void SetSettingsCapabilities(std::uint64_t controls, std::uint64_t actions);
  void ShowTaskManager();
  void ShowSitePermissions(std::string_view origin = {});
  void ShowPermissionPrompt();
  [[nodiscard]] bool task_manager_open() const noexcept {
    return task_manager_open_;
  }
  [[nodiscard]] bool permission_manager_open() const noexcept {
    return permission_manager_open_;
  }
  [[nodiscard]] bool permission_prompt_open() const noexcept {
    return permission_prompt_id_ != 0;
  }
  void ShowThemeEditor(KrSpaceId space = 0);
  // Native first-run onboarding, starting at a KrOnboardingStep.
  [[nodiscard]] bool ShowOnboarding(
      std::uint32_t step = KR_ONBOARDING_STEP_IGNITION);
  // One finite sample import step. Returns true while the import runs.
  [[nodiscard]] bool AdvanceImport();
  // Windows' default-apps flow came back.
  void CompleteDefaultBrowserRequest(bool made_default);
  [[nodiscard]] const std::optional<KrOnboardingResult>& onboarding_result()
      const noexcept {
    return onboarding_result_;
  }
  [[nodiscard]] KrSearchEngineId default_search_engine() const noexcept {
    return default_engine_;
  }
  void ShowWorkspaceEditor(KrSpaceId space = 0);
  [[nodiscard]] const KrWorkspaceTheme& background_theme() const noexcept {
    return background_theme_;
  }
  [[nodiscard]] std::uint64_t background_theme_generation() const noexcept {
    return background_theme_generation_;
  }
  void ExecuteShortcut(PreviewShortcut shortcut);
  [[nodiscard]] const AppearancePrefs& appearance() const {
    return options_.appearance;
  }
  bool PreTranslateMessage(const void* native_message);

  [[nodiscard]] Page active_page() const;
  [[nodiscard]] PageLayout page_layout(bool right_to_left = false) const;
  [[nodiscard]] KrRect content_bounds() const { return content_bounds_; }
  [[nodiscard]] KrRect site_data_anchor() const { return site_data_anchor_; }
  [[nodiscard]] KrRect app_menu_anchor() const { return app_menu_anchor_; }
  [[nodiscard]] std::array<KrColor, 2> background_gradient() const noexcept {
    return background_gradient_;
  }
  // Quality glass: paint no frame so the system backdrop shows through.
  [[nodiscard]] bool transparent_chrome() const noexcept {
    return transparent_chrome_;
  }
  [[nodiscard]] float content_radius() const { return content_radius_; }
  [[nodiscard]] std::uint32_t address_flags() const noexcept {
    return address_flags_;
  }
  [[nodiscard]] bool FocusNative(std::uint32_t element);
  [[nodiscard]] const KrAddressState& address_state() const noexcept {
    return address_state_;
  }
  [[nodiscard]] std::optional<std::size_t> selected_suggestion()
      const noexcept {
    return selected_suggestion_;
  }
  [[nodiscard]] const KrShellMetrics& shell_metrics() const noexcept {
    return shell_metrics_;
  }
  [[nodiscard]] std::span<const KrRect> drag_regions() const {
    return drag_regions_;
  }
  [[nodiscard]] bool dark() const { return options_.dark; }
  [[nodiscard]] KrTheme theme_state() const noexcept;
  [[nodiscard]] bool fullscreen() const { return fullscreen_; }
  [[nodiscard]] float page_zoom() const noexcept {
    return static_cast<float>(page_zoom_percent_) / 100;
  }

 private:
  std::unordered_map<KrTabId, std::string> tab_icon_overrides_;
  [[nodiscard]] std::string_view LayoutMenuText(
      std::string_view key,
      std::string_view fallback) const;
  std::vector<std::pair<std::string, std::string>> layout_text_;
  struct MockTab {
    KrTabId id = 0;
    KrSpaceId space = 0;
    std::uint32_t flags = 0;
    std::string title;
    std::string url;
    std::uint32_t color = 0;
    int back_depth = 0;
    int forward_depth = 0;
    std::uint64_t navigation_id = 0;
    std::size_t load_step = 0;
    float load_progress = 0;
    bool transient = false;
    KrFolderId folder = 0;
  };
  struct MockSpace {
    KrSpaceId id = 0;
    std::string name;
    std::string glyph;
    std::uint32_t color = 0;
    KrTabId last_active = 0;
  };

  // Seeding and pushes into the UI.
  void Seed();
  void InitMultiPage();
  void SeedMultiPage();
  void PublishSplitFocus(KrTabId tab);
  void ForgetMultiPageTab(KrTabId tab);
  [[nodiscard]] KrResult MultiPageCommand(const KrMultiPageCommand& command);
  struct MockSplit {
    KrSplitGroupId id = 0;
    KrSpaceId space = 0;
    std::uint64_t revision = 1;
    KrSplitLayout layout = KR_SPLIT_COLUMNS;
    KrTabId active = 0;
    std::size_t count = 0;
    std::array<KrSplitMember, 4> members{};
    std::array<KrSplitWeight, 3> weights{};
    [[nodiscard]] KrSplitGroup Record();
    void Adopt(const KrSplitGroup& record);
  };
  [[nodiscard]] MockSplit* FindSplit(KrTabId tab);
  std::vector<MockSplit> split_groups_;
  KrSplitGroupId next_split_id_ = 1;
  KrGlanceState glance_{.struct_size = sizeof(KrGlanceState)};
  MockTab& AddTab(KrSpaceId space,
                  std::uint32_t flags,
                  std::string title,
                  std::string url,
                  std::uint32_t color);
  void PushAll();
  void PushTab(const MockTab& tab) const;
  void PushSpaces() const;
  void PushNav() const;
  void StartLoading(MockTab& tab);
  void PushLoadProgress(const MockTab& tab) const;
  void PushActions() const;
  void InstallBrowserTools();
  void PushTasks();
  void PushTask(const MockTab& tab) const;
  void PushSitePermissions() const;
  void CancelPermissionPrompt();
  [[nodiscard]] std::string ActiveOrigin() const;
  void PushFavicon(const MockTab& tab) const;
  void PushTheme() const;
  void PushThemeSwatches() const;
  void PushWorkspaceThemes() const;
  void ApplyWorkspaceTheme(const KrWorkspaceTheme& theme, bool commit);
  void ResetWorkspaceTheme(KrSpaceId space);
  void PushLayout() const;
  void PushAppearance() const;
  [[nodiscard]] bool TryOpenSettings(std::string_view url);
  void AppearanceChanged();

  // Browser behaviour behind the intents.
  void Activate(KrTabId id);
  void Close(KrTabId id);
  [[nodiscard]] KrResult ExecuteFolderCommand(const KrFolderCommand& command);
  void PushFolders() const;
  struct MockFolder {
    KrFolderId id = 0;
    KrSpaceId space = 0;
    KrFolderId parent = 0;
    std::string name, glyph;
    std::size_t order = 0;
    bool collapsed = false;
  };
  std::vector<MockFolder> folders_;
  KrFolderId next_folder_id_ = 1;
  void RequestTabPreview(std::uint32_t request_id, KrTabId tab);
  void CancelTabPreview(std::uint32_t request_id);
  void OpenTab(std::string_view text, std::uint32_t disposition);
  void LoadInto(MockTab& tab, std::string_view text);
  void SwitchSpace(KrSpaceId id);
  void EditWorkspace(std::uint32_t kind, const KrSpace& space);
  void Suggest(std::uint32_t request_id, std::string_view text);
  void AcceptSuggestion(std::size_t index, std::uint32_t disposition);
  void ShowMenu(std::uint32_t request_id,
                std::uint32_t kind,
                std::uint64_t target);
  [[nodiscard]] bool ShowChromeMenu(std::uint32_t request_id,
                                    std::uint32_t kind,
                                    std::uint64_t target);
  [[nodiscard]] bool RunChromeCommand(std::int32_t command_id);
  void RunMenuCommand(std::int32_t command_id);
  void Duplicate(KrTabId id);
  void ReopenClosedTab();
  void Log(std::uint32_t level, std::string_view text) const;

  MockTab* Find(KrTabId id);
  [[nodiscard]] const MockTab* Find(KrTabId id) const;
  [[nodiscard]] std::size_t IndexOf(KrTabId id) const;

  static MockBrowser& Self(void* self) {
    return *static_cast<MockBrowser*>(self);
  }

  MockOptions options_;
  MockHostCallbacks callbacks_;
  void InitPrivacyFeatures();
  // Downloads, a sidebar media session and library sections (mock_library).
  void InitLibrary();
  void PushLibrary() const;
  void PushLibrarySection(std::uint32_t section) const;
  void PushDownload(std::size_t index) const;
  void PushMediaSession() const;
  [[nodiscard]] bool ShowLibraryMenu(std::uint32_t request_id,
                                     std::uint32_t kind,
                                     std::uint64_t target);
  [[nodiscard]] bool RunLibraryCommand(std::int32_t command_id);
  std::unique_ptr<LibraryFixture> library_;
  void PushPrivacyFeatures() const;
  // First-run onboarding (onboarding_host).
  void InitSettings();
  void PushSettings();
  void PushBrowserServices() const;
  void CompleteSettings(std::uint64_t request, KrResult result) const;
  bool settings_visible_ = false;
  std::uint32_t settings_section_ = KR_SETTINGS_APPEARANCE,
                settings_startup_ = 0;
  std::uint64_t settings_revision_ = 0, settings_managed_ = 0;
  std::uint64_t settings_controls_ = (UINT64_C(1) << 7) - 1;
  std::uint64_t settings_actions_ = (UINT64_C(1) << 23) - 1;
  bool settings_ask_download_ = false;
  std::string settings_download_ = "C:\\Users\\Preview\\Downloads";
  std::optional<std::uint32_t> last_settings_action_;
  void InitOnboarding();
  void PushOnboarding() const;
  void PushImportProgress() const;
  void PushDefaultBrowser() const;
  void PushIcon(KrImageId id, std::uint32_t argb) const;
  [[nodiscard]] bool TryOpenOnboarding(std::string_view url);
  bool onboarding_visible_ = false;
  KrImportSourceId import_source_ = 0;
  std::uint32_t import_state_ = KR_IMPORT_IDLE;
  std::size_t import_step_ = 0;
  float import_fraction_ = -1;
  std::uint32_t default_browser_flags_ = KR_DEFAULT_BROWSER_CAN_SET;
  KrSearchEngineId default_engine_ = 1;
  std::optional<KrOnboardingResult> onboarding_result_;
  bool privacy_visible_ = false, privacy_widevine_ = false,
       privacy_running_widevine_ = false;
  bool privacy_caption_ = false, privacy_running_caption_ = false;
  bool privacy_autoplay_ = false, privacy_running_autoplay_ = false;
  bool privacy_search_suggestions_ = false;
  KrBrowser table_{};
  KrShellUI ui_{};
  bool attached_ = false;
  struct MockPermission {
    KrPermissionId id;
    std::string origin, label, description;
    std::uint32_t kind, setting, allowed_settings;
    bool managed = false;
  };
  std::vector<MockPermission> permissions_;
  std::vector<KrTaskId> published_tasks_;
  bool task_manager_open_ = false, permission_manager_open_ = false;
  std::string permission_scope_, prompt_origin_;
  std::uint64_t permission_prompt_id_ = 0, next_permission_prompt_id_ = 0;
  KrRect site_data_anchor_{};
  KrRect app_menu_anchor_{};

  std::vector<MockSpace> spaces_;
  std::vector<MockTab> tabs_;
  KrSpaceId active_space_ = 0;
  KrSpaceId next_workspace_id_ = 4;
  std::vector<KrWorkspaceTheme> extra_workspace_themes_;
  KrTabId active_tab_ = 0;
  KrTabId next_tab_id_ = 1;
  std::uint64_t next_navigation_id_ = 1;
  bool bookmarked_ = false;
  bool system_dark_ = false;
  bool fullscreen_ = false;
  float window_width_ = 0;
  float window_height_ = 0;
  float window_scale_ = 1;
  bool window_active_ = true;
  bool window_maximized_ = false;

  struct LastSuggestion {
    std::string text;
    KrTabId switch_to = 0;
  };
  std::vector<LastSuggestion> last_suggestions_;
  std::uint32_t suggestion_request_ = 0;
  std::optional<std::size_t> selected_suggestion_;
  KrTabId menu_target_ = 0;
  std::uint32_t menu_kind_ = KR_MENU_APP;
  KrSpaceId menu_space_ = 0;
  std::uint32_t chrome_menu_request_ = 0;
  std::uint32_t chrome_menu_kind_ = KR_MENU_APP;
  std::uint64_t chrome_menu_target_ = 0;
  int page_zoom_percent_ = 100;
  std::vector<MockTab> closed_tabs_;
  std::uint32_t preview_request_ = 0;
  std::vector<std::uint8_t> preview_pixels_;

  KrRect content_bounds_{};
  std::array<KrColor, 2> background_gradient_{};
  bool transparent_chrome_ = false;
  KrWorkspaceTheme background_theme_{};
  std::uint64_t background_theme_generation_ = 0;
  float content_radius_ = 0;
  KrShellMetrics shell_metrics_{};
  std::uint32_t address_flags_ = 0;
  KrAddressState address_state_{};
  std::string address_text_;
  std::vector<KrRect> drag_regions_;
};

}  // namespace kr::fixtures
