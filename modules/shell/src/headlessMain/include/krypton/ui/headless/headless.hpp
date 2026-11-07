#pragma once

// Test hooks for the headless actuals. Only commonTest (and headless builds)
// see this header.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "krypton/ui/layout/shell_layout.hpp"
#include "krypton/ui/model/model.hpp"
#include "krypton/ui/platform/shell_view.hpp"
#include "krypton/ui/state/store.hpp"

namespace kr::headless {

// An owned copy of the last view model, safe to inspect after Render.
struct RenderSnapshot {
  std::vector<FolderRowVm> folder_rows;
  std::vector<std::string> folders;
  struct OnboardingSnapshot {
    bool open = false, finishing = false, can_go_back = false;
    OnboardingOutcome outcome = OnboardingOutcome::kCompleted;
    OnboardingStep step = OnboardingStep::kIgnition;
    int direction = 1;
    std::uint32_t step_serial = 0, morph_serial = 0;
    std::vector<OnboardingStep> rail;
    std::size_t rail_index = 0;
    UiText title = UiText::kOnboardingIgnitionTitle;
    std::string progress;
    bool ghost = false, chrome_veiled = false;
    ShellLayout ghost_layout;
    LayoutChoice layout = LayoutChoice::kSidebarAndToolbar;
    bool sidebar_right = false, side_available = false;
    std::optional<WorkspaceTheme> theme;
    std::size_t preset_count = 0, first_preset = 0;
    Rect color_field;
    Point color_point;
    OnboardingReveal reveal;
    std::vector<std::string> import_sources;
    ImportSourceId import_source;
    Flags<ImportData> import_items;
    ImportProgress import;
    bool import_requested = false;
    std::string import_status;
    std::vector<std::string> engines;
    SearchEngineId engine;
    std::vector<std::string> privacy;
    bool privacy_restart = false;
    Flags<DefaultBrowserFlag> default_browser;
    bool default_requested = false;
    OnboardingFlight flight;
    Rect ready_target;
  } onboarding;
  struct PrivacySnapshot {
    struct Row {
      PrivacyFeature value;
      bool editable;
    };
    std::vector<Row> rows;
    std::optional<PrivacyFeature> confirmation;
    bool restart_pending = false, can_relaunch = false, failed = false;
  } privacy_features;
  struct LibrarySnapshot {
    struct Row {
      LibraryRowKind kind;
      std::string title, subtitle;
      bool active = false;
    };
    bool open = false, downloading = false, media_opt_in = false;
    bool loading = false;
    LibrarySection section = LibrarySection::kHistory;
    std::vector<LibrarySection> sections;
    std::vector<std::string> stack, stack_status, media;
    std::vector<MediaCardVm> media_cards;  // string views cleared
    std::vector<Row> rows, tiles;
    UiText empty = UiText::kCount;
    LibraryPaletteVm palette;
  } library;
  struct TaskSnapshot {
    TaskId id;
    std::string title, memory, cpu, network, process;
    bool selected;
  };
  struct PermissionSnapshot {
    PermissionId id;
    std::string origin, label;
    PermissionSetting setting;
    bool editable, managed;
  };
  bool tasks_open = false, permissions_open = false, can_end_tasks = false;
  std::vector<TaskSnapshot> tasks;
  std::vector<PermissionSnapshot> permissions;
  std::optional<PermissionPrompt> permission_prompt;
  std::string task_error, permission_error;
  bool workspace_editor_open = false;
  bool workspace_can_remove = false;
  std::string workspace_name;
  struct SettingsSnapshot : ShellViewModel::SettingsVm {
    BrowserSettingsState owned_browser;
    BrowserServicesState owned_services;
    std::vector<Space> owned_workspaces;
    std::vector<SettingsEntry> owned_entries;
    SettingsSnapshot() { Rebind(); }
    void Rebind() noexcept {
      browser = &owned_browser;
      services = &owned_services;
      workspaces = owned_workspaces;
      entries = owned_entries;
    }
    void Assign(const ShellViewModel::SettingsVm& source) {
      static_cast<ShellViewModel::SettingsVm&>(*this) = source;
      owned_browser = source.browser ? *source.browser : BrowserSettingsState{};
      owned_services =
          source.services ? *source.services : BrowserServicesState{};
      owned_workspaces.assign(source.workspaces.begin(),
                              source.workspaces.end());
      owned_entries.assign(source.entries.begin(), source.entries.end());
      Rebind();
    }
    SettingsSnapshot(const SettingsSnapshot& other) { Assign(other); }
    SettingsSnapshot& operator=(const SettingsSnapshot& other) {
      if (this != &other)
        Assign(other);
      return *this;
    }
    SettingsSnapshot(SettingsSnapshot&& other) noexcept
        : ShellViewModel::SettingsVm(other),
          owned_browser(std::move(other.owned_browser)),
          owned_services(std::move(other.owned_services)),
          owned_workspaces(std::move(other.owned_workspaces)),
          owned_entries(std::move(other.owned_entries)) {
      Rebind();
    }
    SettingsSnapshot& operator=(SettingsSnapshot&& other) noexcept {
      if (this != &other) {
        static_cast<ShellViewModel::SettingsVm&>(*this) = other;
        owned_browser = std::move(other.owned_browser);
        owned_services = std::move(other.owned_services);
        owned_workspaces = std::move(other.owned_workspaces);
        owned_entries = std::move(other.owned_entries);
        Rebind();
      }
      return *this;
    }
    SettingsSnapshot& operator=(const ShellViewModel::SettingsVm& source) {
      Assign(source);
      return *this;
    }
  } settings;
  std::vector<std::string> horizontal_tabs;
  std::array<std::string, kUiTextDefaults.size()> text;
  bool right_to_left = false;
  ShellLayout layout;
  std::vector<std::string> favorites;
  std::vector<std::string> pinned;
  std::vector<std::string> today;
  bool today_idle = false;
  std::vector<std::string> spaces;
  std::string active_title;
  std::string address;
  bool can_go_back = false;
  bool command_bar_open = false;
  std::string command_text;
  AddressSelection address_selection;
  std::uint64_t address_edit_revision = 0;
  std::optional<Anchor> focused_native;
  std::vector<std::string> suggestions;
  int selected = 0;
  std::size_t page_actions = 0;
  std::size_t toolbar_actions = 0;
  struct ActionBadge {
    std::string text;
    Color background, foreground;
  };
  std::vector<ActionBadge> page_action_badges;  // one per page action
  Color gradient_start;
  IconPack icon_pack = IconPack::kFluent;
  bool incognito = false;
  LoadingProgressState loading_progress;
  bool loading_progress_visible = false;
  TabId preview_tab;
  std::uint32_t preview_request = 0;
  ImageId preview_thumbnail;
  std::string preview_title, preview_domain, preview_memory_label,
      preview_memory_value, preview_memory_text, preview_status;
  bool preview_memory_known = false;
  Flags<Dirty> changed;
};

struct ShownMenu {
  std::uint32_t request_id = 0;
  MenuTree menu;
  Rect anchor;
  std::vector<Image> images;
};

// Runs tasks posted with ShellView::PostTask. Returns how many ran.
std::size_t RunPendingTasks();
// Advance the deterministic timer queue without sleeping.
std::size_t AdvanceTime(std::chrono::milliseconds elapsed);
[[nodiscard]] std::size_t PendingTaskCount();

[[nodiscard]] std::size_t RenderCount();
[[nodiscard]] bool InitialThemeIsDark();
[[nodiscard]] const RenderSnapshot& LastRender();
[[nodiscard]] const std::optional<ShownMenu>& LastMenu();
// The text of the last ShellView::ShowNotification, empty for none.
[[nodiscard]] const std::string& LastNotification();

// The delegate of the most recently created view: lets tests act as the user.
[[nodiscard]] ShellViewDelegate* Delegate();

// Make CheckSupport() fail, to exercise the host's fallback path.
void SetSupported(bool supported);
// Fail after support checking, to exercise creation error diagnostics.
void SetViewCreationEnabled(bool enabled);

// Forget everything; call at the start of each test.
void Reset();

}  // namespace kr::headless
