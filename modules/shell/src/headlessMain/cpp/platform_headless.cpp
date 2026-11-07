// headless actuals: a ShellView that records instead of drawing, and a task
// queue that tests drain by hand.

#include <algorithm>
#include <cstdio>
#include <deque>
#include <print>

#include "krypton/ui/headless/headless.hpp"
#include "krypton/ui/platform/platform.hpp"

namespace kr {
namespace {

struct HeadlessState {
  bool initial_dark = false;
  struct Delayed {
    std::chrono::milliseconds due;
    Task task;
  };
  std::deque<Task> tasks;
  std::vector<Delayed> delayed;
  std::chrono::milliseconds now{};
  std::size_t render_count = 0;
  headless::RenderSnapshot last_render;
  std::optional<Anchor> focused_native;
  std::optional<headless::ShownMenu> last_menu;
  std::string last_notification;
  ShellViewDelegate* delegate = nullptr;
  bool supported = true;
  bool view_creation_enabled = true;
};

HeadlessState& State() {
  static HeadlessState state;
  return state;
}

std::vector<std::string> Titles(std::span<const TabItemVm> items) {
  std::vector<std::string> titles;
  titles.reserve(items.size());
  for (const TabItemVm& item : items)
    titles.emplace_back(item.title);
  return titles;
}

class HeadlessShellView final : public ShellView {
 public:
  Result<void> RegisterGlassBackdrop(const GlassBackdropResource&) override {
    return Fail(ErrorCode::kUnsupported, "Headless graphics");
  }
  Result<void> SubmitGlassBackdrop(const GlassBackdropFrame&) override {
    return Fail(ErrorCode::kUnsupported, "Headless graphics");
  }
  Result<void> DropGlassBackdrop(std::uint64_t) override {
    return Fail(ErrorCode::kUnsupported, "Headless graphics");
  }
  void FocusNative(Anchor target) override {
    State().focused_native = target;
    State().last_render.focused_native = target;
  }
  void ShowNotification(std::string_view text) override {
    State().last_notification = text;
  }
  explicit HeadlessShellView(ShellViewDelegate& delegate) {
    State().delegate = &delegate;
  }
  ~HeadlessShellView() override { State().delegate = nullptr; }

  void Render(const ShellViewModel& vm) override {
    headless::RenderSnapshot snapshot;
    snapshot.focused_native = State().focused_native;
    snapshot.tasks_open = vm.browser_tools.tasks_open;
    snapshot.permissions_open = vm.browser_tools.permissions_open;
    snapshot.can_end_tasks = vm.browser_tools.can_end;
    snapshot.task_error = vm.browser_tools.task_error;
    snapshot.permission_error = vm.browser_tools.permission_error;
    for (const auto& task : vm.browser_tools.tasks)
      snapshot.tasks.push_back({task.id, std::string(task.title),
                                std::string(task.memory), std::string(task.cpu),
                                std::string(task.network),
                                std::string(task.process), task.selected});
    for (const auto& item : vm.browser_tools.permissions)
      snapshot.permissions.push_back({item.id, std::string(item.origin),
                                      std::string(item.label), item.setting,
                                      item.editable, item.managed});
    if (vm.browser_tools.prompt)
      snapshot.permission_prompt = *vm.browser_tools.prompt;
    {
      const auto& in = vm.onboarding;
      auto& out = snapshot.onboarding;
      out.open = in.open;
      out.finishing = in.finishing;
      out.can_go_back = in.can_go_back;
      out.outcome = in.outcome;
      out.step = in.step;
      out.direction = in.direction;
      out.step_serial = in.step_serial;
      out.morph_serial = in.morph_serial;
      out.rail.assign(in.rail.begin(), in.rail.end());
      out.rail_index = in.rail_index;
      out.title = in.title;
      out.progress = in.progress;
      out.ghost = in.ghost;
      out.chrome_veiled = in.chrome_veiled;
      out.ghost_layout = in.ghost_layout;
      out.layout = in.layout;
      out.sidebar_right = in.sidebar_right;
      out.side_available = in.side_available;
      out.theme = in.theme ? std::optional(*in.theme) : std::nullopt;
      out.preset_count = in.presets.size();
      out.first_preset = in.first_preset;
      out.color_field = in.color_field;
      out.color_point = in.color_point;
      out.reveal = in.reveal;
      for (const auto& source : in.import_sources)
        out.import_sources.push_back(source.name);
      out.import_source = in.import_source;
      out.import_items = in.import_items;
      out.import = in.import;
      out.import_requested = in.import_requested;
      out.import_status = in.import_status;
      for (const auto& engine : in.engines)
        out.engines.push_back(engine.name);
      out.engine = in.engine;
      for (const auto& row : in.privacy)
        out.privacy.push_back(row.value->title);
      out.privacy_restart = in.privacy_restart;
      out.default_browser = in.default_browser;
      out.default_requested = in.default_requested;
      out.flight = in.flight;
      out.ready_target = in.ready_target;
    }
    snapshot.settings = vm.settings;
    snapshot.privacy_features.rows.clear();
    for (const auto& row : vm.privacy_features.rows)
      snapshot.privacy_features.rows.push_back({*row.value, row.editable});
    snapshot.privacy_features.confirmation =
        vm.privacy_features.confirmation
            ? std::optional(*vm.privacy_features.confirmation)
            : std::nullopt;
    snapshot.privacy_features.restart_pending =
        vm.privacy_features.restart_pending;
    snapshot.privacy_features.can_relaunch = vm.privacy_features.can_relaunch;
    snapshot.privacy_features.failed = vm.privacy_features.failed;
    {
      auto& library = snapshot.library;
      library = {};
      library.open = vm.library.open;
      library.downloading = vm.library.downloading;
      library.media_opt_in = vm.library.media_opt_in;
      library.loading = vm.library.loading;
      library.section = vm.library.section;
      library.sections.assign(vm.library.sections.begin(),
                              vm.library.sections.end());
      for (const auto& entry : vm.library.stack) {
        library.stack.emplace_back(entry.file_name);
        library.stack_status.emplace_back(entry.status);
      }
      for (auto card : vm.library.media) {
        library.media.emplace_back(card.title);
        card.title = card.artist = {};
        library.media_cards.push_back(card);
      }
      for (const auto& row : vm.library.rows)
        library.rows.push_back({row.kind, std::string(row.title),
                                std::string(row.subtitle), row.active});
      for (const auto& row : vm.library.tiles)
        library.tiles.push_back({row.kind, std::string(row.title),
                                 std::string(row.subtitle), row.active});
      library.empty = vm.library.empty;
      library.palette = vm.library.palette;
    }
    snapshot.workspace_editor_open = vm.workspace_editor.open;
    snapshot.workspace_name = vm.workspace_editor.name;
    snapshot.workspace_can_remove = vm.workspace_editor.can_remove;
    for (std::size_t index = 0; index < snapshot.text.size(); ++index)
      snapshot.text[index] = vm.text[static_cast<UiText>(index)];
    snapshot.right_to_left = vm.text.right_to_left;
    snapshot.layout = vm.layout;
    snapshot.favorites = Titles(vm.favorites);
    snapshot.pinned = Titles(vm.pinned);
    snapshot.today = Titles(vm.today);
    snapshot.folder_rows.assign(vm.folder_rows.begin(), vm.folder_rows.end());
    for (const auto& folder : vm.folders)
      snapshot.folders.emplace_back(folder.name);
    snapshot.today_idle = vm.today_idle;
    snapshot.horizontal_tabs = Titles(vm.horizontal_tabs);
    for (const SpaceVm& space : vm.spaces)
      snapshot.spaces.emplace_back(space.name);
    for (auto section : {vm.favorites, vm.pinned, vm.today, vm.folder_tabs}) {
      for (const TabItemVm& item : section) {
        if (item.active)
          snapshot.active_title = item.title;
      }
    }
    snapshot.address = vm.header.address_text;
    snapshot.can_go_back = vm.header.can_go_back;
    snapshot.command_bar_open = vm.command_bar.open;
    snapshot.command_text = vm.command_bar.text;
    snapshot.address_selection = vm.command_bar.selection;
    snapshot.address_edit_revision = vm.command_bar.edit_revision;
    for (const SuggestionVm& s : vm.command_bar.suggestions) {
      snapshot.suggestions.emplace_back(s.contents);
    }
    snapshot.selected = vm.command_bar.selected;
    snapshot.page_actions = vm.page_actions.size();
    snapshot.toolbar_actions = vm.toolbar_actions.size();
    for (const ActionVm& action : vm.page_actions)
      snapshot.page_action_badges.push_back(
          {.text = std::string(action.badge),
           .background = action.badge_color,
           .foreground = action.badge_text_color});
    snapshot.gradient_start = vm.theme.gradient_start;
    snapshot.icon_pack = vm.theme.icon_pack;
    snapshot.incognito = vm.theme.incognito;
    snapshot.loading_progress = vm.loading_progress.state;
    snapshot.loading_progress_visible = vm.loading_progress.visible;
    snapshot.preview_tab = vm.tab_preview.tab;
    snapshot.preview_request = vm.tab_preview.request_id;
    snapshot.preview_thumbnail = vm.tab_preview.thumbnail;
    snapshot.preview_title = vm.tab_preview.title;
    snapshot.preview_domain = vm.tab_preview.domain;
    snapshot.preview_memory_label = vm.tab_preview.memory_label;
    snapshot.preview_memory_value = vm.tab_preview.memory_value;
    snapshot.preview_memory_text = vm.tab_preview.memory_text;
    snapshot.preview_status = vm.tab_preview.status;
    snapshot.preview_memory_known = vm.tab_preview.memory_known;
    snapshot.changed = vm.changed;

    State().last_render = std::move(snapshot);
    ++State().render_count;
  }

  void ShowMenu(std::uint32_t request_id,
                const MenuTree& menu,
                Rect anchor,
                const ImageStore& images,
                MenuKind) override {
    State().last_menu = headless::ShownMenu{request_id, menu, anchor};
    for (const auto& items : menu.menus) {
      for (const MenuItem& item : items) {
        if (const Image* image = images.Find(item.icon))
          State().last_menu->images.push_back(*image);
      }
    }
  }

  void DismissMenu(std::uint32_t request) override {
    if (State().last_menu && State().last_menu->request_id == request)
      State().last_menu.reset();
  }
  void PostTask(Task task) override {
    State().tasks.push_back(std::move(task));
  }
  void PostDelayedTask(Task task, std::chrono::milliseconds delay) override {
    if (delay <= std::chrono::milliseconds::zero()) {
      PostTask(std::move(task));
      return;
    }
    State().delayed.push_back({State().now + delay, std::move(task)});
  }
};

}  // namespace

namespace platform {
Result<void> ShutdownRuntime() noexcept {
  return {};
}

std::string_view Name() noexcept {
  return "headless";
}

Result<void> CheckSupport() noexcept {
  if (!State().supported) {
    return Fail(ErrorCode::kUnsupported, "headless support disabled by test");
  }
  return {};
}

Result<std::unique_ptr<ShellView>> CreateShellView(
    const ShellViewParams& params,
    ShellViewDelegate& delegate) noexcept {
  if (!State().view_creation_enabled) {
    return Fail(ErrorCode::kPlatform,
                "headless view creation disabled by test");
  }
  State().initial_dark = params.theme.dark;
  return std::make_unique<HeadlessShellView>(delegate);
}

void WriteDefaultLog(LogLevel level, std::string_view text) noexcept {
  static constexpr const char* kNames[] = {"verbose", "info", "warning",
                                           "error"};
  std::println(stderr, "[krypton-ui {}] {}", kNames[static_cast<int>(level)],
               text);
}

}  // namespace platform

namespace headless {
bool InitialThemeIsDark() {
  return State().initial_dark;
}

std::size_t RunPendingTasks() {
  std::size_t ran = 0;
  // Tasks may post more tasks; run until the queue is empty, like a loop turn
  // that keeps draining.
  while (!State().tasks.empty()) {
    Task task = std::move(State().tasks.front());
    State().tasks.pop_front();
    task();
    ++ran;
  }
  return ran;
}

std::size_t PendingTaskCount() {
  return State().tasks.size();
}

std::size_t AdvanceTime(std::chrono::milliseconds elapsed) {
  auto& state = State();
  state.now += std::max(elapsed, std::chrono::milliseconds::zero());
  for (auto& pending : state.delayed)
    if (pending.due <= state.now)
      state.tasks.push_back(std::move(pending.task));
  std::erase_if(state.delayed, [now = state.now](const auto& pending) {
    return pending.due <= now;
  });
  return RunPendingTasks();
}
std::size_t RenderCount() {
  return State().render_count;
}
const RenderSnapshot& LastRender() {
  return State().last_render;
}
const std::optional<ShownMenu>& LastMenu() {
  return State().last_menu;
}
const std::string& LastNotification() {
  return State().last_notification;
}
ShellViewDelegate* Delegate() {
  return State().delegate;
}
void SetSupported(bool supported) {
  State().supported = supported;
}
void SetViewCreationEnabled(bool enabled) {
  State().view_creation_enabled = enabled;
}

void Reset() {
  HeadlessState& state = State();
  state.tasks.clear();
  state.delayed.clear();
  state.now = {};
  state.render_count = 0;
  state.last_render = {};
  state.last_menu.reset();
  state.last_notification.clear();
  state.supported = true;
  state.view_creation_enabled = true;
}

}  // namespace headless
}  // namespace kr
