#include "krypton/ui/shell/shell.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

#include "krypton/ui/core/env.hpp"
#include "krypton/ui/core/log.hpp"
#include "krypton/ui/layout/horizontal_layout.hpp"
#include "krypton/ui/model/tab_drop.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "krypton/ui/presentation/clear_tabs.hpp"
#include "krypton/ui/presentation/multi_page.hpp"

namespace kr {

std::size_t Shell::AnchorKeyHash::operator()(
    const AnchorKey& key) const noexcept {
  return std::hash<std::uint64_t>{}(key.id) ^
         (static_cast<std::size_t>(key.kind) << 1);
}

Result<std::unique_ptr<Shell>> Shell::Create(void* parent_window,
                                             const KrBrowser& browser,
                                             const Theme& initial_theme) {
  const auto started = std::chrono::steady_clock::now();
  auto shell = std::make_unique<Shell>(PassKey{}, browser);
  shell->store_.SetTheme(initial_theme);
  const auto& initial = shell->presenter_.Present(
      shell->store_, shell->CurrentLayout(), Dirty::kAll);
  auto view = platform::CreateShellView(
      {.parent_window = parent_window, .theme = initial.theme}, *shell);
  if (!view)
    return std::unexpected(std::move(view.error()));
  shell->view_ = std::move(*view);
  shell->ScheduleRender();
  if (EnvFlag("KR_UI_STATS"))
    Log(LogLevel::kInfo, "shell creation: {:.3f} ms",
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started)
            .count());
  return shell;
}

Shell::Shell(PassKey, const KrBrowser& browser)
    : browser_(browser), stats_(EnvFlag("KR_UI_STATS")) {
  if (browser_.can_log())
    SetLogSink(&Shell::LogToHost, this);
}

Shell::~Shell() {
  if (store_.ui().boost_editor.open)
    (void)browser_.EditBoost(BoostEdit::kCancel,
                             store_.ui().boost_editor.draft);
  if (store_.ui().settings.open)
    browser_.SettingsVisibilityChanged(store_.ui().settings.section, false);
  if (const auto& onboarding = store_.onboarding(); onboarding.open) {
    if (!onboarding.reported)
      browser_.OnboardingFinished(OnboardingOutcome::kClosed, onboarding.step);
    browser_.OnboardingVisibilityChanged(false);
  }
  if (store_.privacy_features().visible)
    browser_.PrivacyFeaturesVisibilityChanged(false);
  lifetime_.reset();
  render_scheduled_ = true;
  if (store_.browser_tools().tasks_open)
    browser_.TaskManagerVisibilityChanged(false);
  if (store_.browser_tools().permissions_open)
    browser_.SitePermissionsVisibilityChanged(
        store_.browser_tools().permission_origin, false);
  if (store_.browser_tools().prompt)
    browser_.RespondToPermissionPrompt(store_.browser_tools().prompt->id,
                                       PermissionDecision::kDismiss);
  CloseAddress();
  while (!menu_anchors_.empty())
    DismissMenu(menu_anchors_.begin()->first);
  OnThemeEditorDismissed();
  if (const auto request = store_.ui().tab_preview.request_id; request != 0) {
    store_.EndTabPreview();
    browser_.CancelTabPreview(request);
  }
  // The view may still reference the delegate while it tears down.
  view_.reset();
  ClearLogSink(this);
}

void Shell::LogToHost(void* self, LogLevel level, std::string_view text) {
  static_cast<Shell*>(self)->browser_.Log(level, text);
}

// ------------------------------------------------------------------ state in

void Shell::SetWindowState(const WindowState& state) {
  const bool entering_fullscreen =
      state.flags.HasAny(WindowFlag::kFullscreen |
                         WindowFlag::kTabFullscreen) &&
      !store_.window().flags.HasAny(WindowFlag::kFullscreen |
                                    WindowFlag::kTabFullscreen);
  if (!state.flags.Has(WindowFlag::kActive) &&
      store_.window().flags.Has(WindowFlag::kActive))
    DismissBoostEditor();
  if (state.flags.Has(WindowFlag::kActive) &&
      !store_.window().flags.Has(WindowFlag::kActive))
    browser_.RefreshDownloads();
  store_.SetWindow(state);
  if (entering_fullscreen || !state.flags.Has(WindowFlag::kActive))
    OnLibraryDismissed();
  ReportContentBounds(CurrentLayout());
  ScheduleRender();
}

void Shell::SetTheme(const Theme& theme) {
  store_.SetTheme(theme);
  ScheduleRender();
}

void Shell::SetLayoutPrefs(const LayoutPrefs& prefs) {
  if (store_.layout_prefs().flags != prefs.flags) {
    ++sidebar_hover_serial_;
    ++toolbar_hover_serial_;
  }
  if (TabLayoutOf(store_.layout_prefs().flags) != TabLayoutOf(prefs.flags))
    OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  store_.SetLayoutPrefs(prefs);
  ScheduleRender();
}

Result<void> Shell::SetUiText(std::string_view key, std::string_view text) {
  auto result = store_.SetUiText(key, text);
  if (result)
    ScheduleRender();
  return result;
}

void Shell::SetTextDirection(bool right_to_left) {
  store_.SetTextDirection(right_to_left);
  ScheduleRender();
}

void Shell::PutImage(ImageId id,
                     int width,
                     int height,
                     int stride,
                     const std::uint8_t* pixels) {
  if (!store_.PutImage(id, width, height, stride, pixels)) {
    Log(LogLevel::kWarning, "put_image ignored: bad image {} ({}x{})", id,
        width, height);
    return;
  }
  ScheduleRender();
}

void Shell::DropImage(ImageId id) {
  store_.DropImage(id);
  ScheduleRender();
}

void Shell::SetSpaces(std::vector<Space> spaces, SpaceId active) {
  store_.SetSpaces(std::move(spaces), active);
  const auto& editor = store_.ui().workspace_editor;
  if (editor.open && editor.value.id && !store_.FindSpace(editor.value.id))
    store_.CloseWorkspaceEditor();
  ScheduleRender();
}

void Shell::InsertTab(std::size_t index, Tab tab) {
  Check(store_.InsertTab(index, std::move(tab)), "tab_inserted");
  // Zen's library steps aside for a new tab, e.g. one opened from history.
  OnLibraryDismissed();
}

void Shell::UpdateTab(Tab tab) {
  Check(store_.UpdateTab(std::move(tab)), "tab_updated");
}

void Shell::MoveTab(TabId id, std::size_t to_index) {
  Check(store_.MoveTab(id, to_index), "tab_moved");
}

void Shell::RemoveTab(TabId id) {
  if (store_.ui().tab_preview.tab == id)
    OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  Check(store_.RemoveTab(id), "tab_removed");
}

void Shell::SetActiveTab(TabId id) {
  if (id != store_.active_tab())
    CloseAddress();
  Check(store_.SetActiveTab(id), "set_active_tab");
}

void Shell::SetNav(NavState nav) {
  store_.SetNav(std::move(nav));
  ScheduleRender();
}

void Shell::SetActions(std::vector<Action> actions) {
  store_.SetActions(std::move(actions));
  ScheduleRender();
}

void Shell::SetSuggestions(std::uint32_t request_id,
                           std::vector<Suggestion> suggestions) {
  if (store_.ui().command_bar.generation != 0)
    return;
  store_.SetSuggestions(request_id, std::move(suggestions));
  ScheduleRender();
}

void Shell::ShowMenu(std::uint32_t request_id, const MenuTree& menu) {
  if (EnvFlag("KR_UI_MENU_TRACE"))
    Log(LogLevel::kWarning, "menu trace: show request {} menus {} panel {}",
        request_id, menu.menus.size(), menu.panel);
  const auto it = menu_anchors_.find(request_id);
  if (it == menu_anchors_.end()) {
    Log(LogLevel::kWarning, "show_menu ignored: unknown request {}",
        request_id);
    return;
  }
  if (store_.ui().command_bar.open || store_.ui().theme_editor.open ||
      store_.ui().workspace_editor.open) {
    OnMenuClosed(request_id);
    return;
  }
  if (menu.menus.empty()) {
    OnMenuClosed(request_id);
    return;
  }
  view_->ShowMenu(request_id, menu, it->second.bounds, store_.images(),
                  it->second.kind);
}

void Shell::FocusAddress(bool select_all) {
  if (OnboardingActive())
    return;
  DismissBoostEditor();
  OnWorkspaceEditorDismissed();
  OnThemeEditorDismissed();
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  command_opens_new_tab_ = false;
  if (browser_.SupportsSuggestionSnapshots()) {
    SetAddressText(store_.nav().full_url, select_all);
    return;
  }
  CancelAddressQuery();
  store_.OpenCommandBar(store_.nav().full_url, select_all);
  NotifyAddressState();
  ScheduleRender();
}

bool Shell::PreTranslateMessage(const void* native_message) {
  return native_message && view_ && view_->PreTranslateMessage(native_message);
}

bool Shell::OnUnhandledKey(const void* native_message) {
  return browser_.UnhandledKey(native_message);
}

const Image* Shell::OnImageRequested(ImageId image) const {
  return store_.images().Find(image);
}

std::optional<TabItemVm> Shell::OnTabRequested(TabId id) const {
  if (const auto* tab = store_.FindTab(id))
    return PresentMultiPageTab(store_, *tab);
  return std::nullopt;
}

// ------------------------------------------------------------- intents out

void Shell::OnTabActivated(TabId tab) {
  if (const auto* group = store_.SplitForTab(tab);
      group && SplitRepresentative(*group) == tab)
    tab = group->active;
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  browser_.ActivateTab(tab);
}

void Shell::OnTabClosed(TabId tab) {
  if (const auto* group = store_.SplitForTab(tab)) {
    tab = group->active;
  }
  browser_.CloseTab(tab);
}

void Shell::OnClearTabs() {
  const auto tabs = TabsToClear(store_);
  if (tabs.empty())
    return;
  // Like Zen, keep the window: closing its last tab would close it. The host
  // runs the new tab before the closes that follow.
  if (tabs.size() == store_.tabs().size())
    browser_.NewTab();
  for (const TabId tab : tabs)
    browser_.CloseTab(tab);
  view_->ShowNotification(store_.text()[UiText::kTabsCleared]);
}

void Shell::OnTabMuteToggled(TabId tab) {
  if (const auto* current = store_.FindTab(tab)) {
    const Flags<TabFlag> muted{TabFlag::kMuted};
    browser_.SetTabFlags(
        tab, muted,
        current->flags.Has(TabFlag::kMuted) ? Flags<TabFlag>{} : muted);
  }
}

// Tab order belongs to the host: a drop asks for the move and the host pushes
// the result back.
void Shell::OnTabDropped(TabId tab,
                         TabId previous,
                         TabId next,
                         std::optional<bool> pinned) {
  const auto* neighbor = store_.FindTab(next ? next : previous);
  const auto destination = neighbor ? neighbor->folder : FolderId{};
  if (const auto* source = store_.FindTab(tab);
      source && source->folder != destination && browser_.CanFolders())
    (void)browser_.FolderCommand({.kind = FolderCommandKind::kMoveTab,
                                  .folder = destination,
                                  .tab = tab});
  if (destination)
    pinned = true;
  const auto tabs = store_.tabs();
  const auto moved = std::ranges::find(tabs, tab, &Tab::id);
  const auto to = moved == tabs.end() ? std::nullopt
                                      : TabDropIndex(tabs, tab, previous, next);
  const bool pin = moved != tabs.end() && pinned &&
                   *pinned != moved->flags.Has(TabFlag::kPinned);
  const bool move = to && *to != static_cast<std::size_t>(moved - tabs.begin());
  const auto space = moved != tabs.end() ? moved->space : SpaceId{};
  // Reconcile the native drop even when the host ignores an unsupported move
  // or clamps it to the current position. Host updates coalesce with this task.
  store_.InvalidateTabs();
  ScheduleRender();
  if (pin) {
    const Flags<TabFlag> pinned_flag{TabFlag::kPinned};
    browser_.SetTabFlags(tab, pinned_flag,
                         *pinned ? pinned_flag : Flags<TabFlag>{});
  }
  if (move)
    browser_.MoveTab(tab, space, *to);
}

void Shell::OnNewTab() {
  DismissBoostEditor();
  OnWorkspaceEditorDismissed();
  OnThemeEditorDismissed();
  // A new tab starts as an empty command bar. Nothing opens until
  // the user picks something.
  command_opens_new_tab_ = true;
  if (browser_.SupportsSuggestionSnapshots()) {
    SetAddressText({}, false);
    return;
  }
  CancelAddressQuery();
  store_.OpenCommandBar({}, false);
  NotifyAddressState();
  ScheduleRender();
}

void Shell::OnSpaceSelected(SpaceId space) {
  browser_.SwitchSpace(space);
}

void Shell::OnNavCommand(NavCommand command) {
  browser_.SendNavCommand(command);
}

void Shell::OnWindowCommand(WindowCommand command) {
  browser_.SendWindowCommand(command);
}

void Shell::OnActionInvoked(ActionId action, Rect anchor) {
  if (EnvFlag("KR_UI_MENU_TRACE"))
    Log(LogLevel::kWarning, "menu trace: action {}", action.value);
  browser_.InvokeAction(action, anchor);
}

void Shell::OnMenuRequested(MenuKind kind, std::uint64_t target, Rect anchor) {
  if (kind == MenuKind::kTab) {
    if (const auto* group = store_.SplitForTab(TabId{target}))
      target = group->active.value;
  }
  if (OnboardingActive())
    return;
  if (EnvFlag("KR_UI_MENU_TRACE"))
    Log(LogLevel::kWarning, "menu trace: request kind {} target {}",
        std::to_underlying(kind), target);
  DismissBoostEditor();
  OnWorkspaceEditorDismissed();
  OnThemeEditorDismissed();
  CloseAddress();
  while (!menu_anchors_.empty())
    DismissMenu(menu_anchors_.begin()->first);
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  const std::uint32_t request_id = next_menu_request_++;
  if (!next_menu_request_)
    ++next_menu_request_;
  menu_anchors_[request_id] = {anchor, kind};
  browser_.RequestMenu(request_id, kind, target, anchor);
}

void Shell::OnMenuCommand(std::uint32_t request_id, std::int32_t command_id) {
  if (menu_anchors_.contains(request_id))
    browser_.MenuCommand(request_id, command_id);
}

void Shell::DismissMenu(std::uint32_t request) {
  if (EnvFlag("KR_UI_MENU_TRACE"))
    Log(LogLevel::kWarning, "menu trace: host dismiss {}", request);
  if (!menu_anchors_.contains(request))
    return;
  // Retire the request before native close events can reenter.
  menu_anchors_.erase(request);
  view_->DismissMenu(request);
  browser_.MenuClosed(request);
}

void Shell::OnMenuClosed(std::uint32_t request_id) {
  if (EnvFlag("KR_UI_MENU_TRACE"))
    Log(LogLevel::kWarning, "menu trace: native close {}", request_id);
  if (menu_anchors_.erase(request_id) != 0)
    browser_.MenuClosed(request_id);
}

std::uint32_t Shell::OnTabPreviewRequested(TabId tab) {
  if (!store_.FindTab(tab) || OnboardingActive() ||
      store_.ui().command_bar.open || store_.ui().theme_editor.open ||
      store_.ui().workspace_editor.open)
    return 0;
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  if (++next_tab_preview_request_ == 0)
    ++next_tab_preview_request_;
  const auto request = next_tab_preview_request_;
  store_.BeginTabPreview(tab, request);
  ScheduleRender();
  browser_.RequestTabPreview(request, tab);
  return request;
}

void Shell::OnTabPreviewDismissed(std::uint32_t request_id) {
  const auto& preview = store_.ui().tab_preview;
  if (!preview.tab || preview.request_id != request_id)
    return;
  store_.EndTabPreview();
  ScheduleRender();
  browser_.CancelTabPreview(request_id);
}

void Shell::SetTabPreview(std::uint32_t request_id,
                          TabId tab,
                          const TabPreviewData& data) {
  if (store_.SetTabPreview(request_id, tab, data))
    ScheduleRender();
}

void Shell::OnCommandBarRequested() {
  FocusAddress(true);
}
void Shell::OnAddressPointerRequested() {
  FocusAddress(true);
  store_.SetCommandPointerOrigin();
  ScheduleRender();
}

void Shell::OnCommandTextChanged(std::string_view text) {
  const auto length = AddressTextLength(text);
  if (length)
    OnAddressEditChanged(text, {*length, *length, false});
}

void Shell::OnAddressEditChanged(std::string_view text,
                                 AddressSelection selection) {
  const CommandBarState& bar = store_.ui().command_bar;
  if (!bar.open || !AddressTextLength(text, selection) ||
      (text == bar.text && selection == bar.selection))
    return;
  const bool changed = text != bar.text;
  const bool committed = bar.selection.composing && !selection.composing;
  if ((changed && !selection.composing) || committed)
    store_.MarkCommandTyped();
  std::string owned(text);
  std::uint32_t request_id = bar.request_id;
  if (changed || committed) {
    CancelAddressQuery();
    request_id = store_.SetCommandText(owned);
  }
  store_.SetCommandEdit(owned, selection, true);
  NotifyAddressState();
  if (selection.composing || (!changed && !committed)) {
    ScheduleRender();
    return;
  }
  if (owned.empty() && !browser_.SupportsSuggestionSnapshots()) {
    store_.SetSuggestions(request_id, {});
  } else {
    browser_.QuerySuggestions(request_id, owned);
  }
  ScheduleRender();
}

void Shell::OnCommandSelectionMoved(int delta) {
  const auto& bar = store_.ui().command_bar;
  if (!bar.open || bar.selection.composing)
    return;
  if (browser_.SupportsSuggestionSnapshots()) {
    if (bar.request_id != rendered_request_ || !rendered_generation_ ||
        !rendered_suggestion_count_)
      return;
    // Input refers to the rows already shown. A newer generation can be
    // waiting for Render, so never reinterpret its index against new rows.
    const auto count = static_cast<std::int64_t>(rendered_suggestion_count_);
    const auto selected =
        (static_cast<std::int64_t>(rendered_selection_) + delta) % count;
    rendered_selection_ = static_cast<int>((selected + count) % count);
    if (bar.generation == rendered_generation_)
      store_.SetCommandSelection(static_cast<std::size_t>(rendered_selection_));
    ScheduleRender();
    browser_.SelectSuggestionSnapshot(
        rendered_request_, rendered_generation_,
        static_cast<std::size_t>(rendered_selection_));
    return;
  }
  store_.MoveCommandSelection(delta);
  ScheduleRender();
}

void Shell::OnCommandAccepted(Disposition disposition) {
  const CommandBarState& bar = store_.ui().command_bar;
  if (bar.selection.composing)
    return;
  const bool snapshot = browser_.SupportsSuggestionSnapshots();
  const auto count =
      snapshot ? rendered_suggestion_count_ : bar.suggestions.size();
  const auto selected = snapshot ? rendered_selection_ : bar.selected;
  Submit(count == 0
             ? std::nullopt
             : std::optional<std::size_t>(static_cast<std::size_t>(selected)),
         disposition);
}

void Shell::OnSuggestionChosen(std::size_t index, Disposition disposition) {
  if (store_.ui().command_bar.selection.composing)
    return;
  Submit(index, disposition);
}

void Shell::OnCommandBarDismissed() {
  if (!store_.ui().command_bar.open)
    return;
  CloseAddress();
  browser_.FocusPage();
}

void Shell::OnSidebarRevealChanged(bool revealed) {
  const auto serial = ++sidebar_hover_serial_;
  if (revealed) {
    if (!store_.ui().sidebar_revealed) {
      store_.SetSidebarRevealed(true);
      ScheduleRender();
    }
  } else if (store_.ui().sidebar_revealed) {
    view_->PostDelayedTask(
        [this, alive = std::weak_ptr<int>(lifetime_), serial] {
          if (alive.expired() || serial != sidebar_hover_serial_)
            return;
          store_.SetSidebarRevealed(false);
          ScheduleRender();
        },
        std::chrono::milliseconds(
            static_cast<int>(design::kTokens.compact.sidebar_keep_hover_ms)));
  }
}

void Shell::OnToolbarRevealChanged(bool revealed) {
  const auto serial = ++toolbar_hover_serial_;
  if (revealed) {
    if (!store_.ui().toolbar_revealed) {
      store_.SetToolbarRevealed(true);
      ScheduleRender();
    }
  } else if (store_.ui().toolbar_revealed) {
    view_->PostTask([this, alive = std::weak_ptr<int>(lifetime_), serial] {
      if (alive.expired() || serial != toolbar_hover_serial_)
        return;
      store_.SetToolbarRevealed(false);
      ScheduleRender();
    });
  }
}

// Layout preferences and the active space belong to the host: these ask for a
// change, and the host pushes the result back.
void Shell::OnSidebarToggled(SidebarToggle toggle) {
  LayoutPrefs prefs = store_.layout_prefs();
  if (toggle == SidebarToggle::kCompact)
    prefs.flags.Set(LayoutFlag::kCompactMode,
                    !prefs.flags.Has(LayoutFlag::kCompactMode));
  else
    prefs.flags = WithSidebarMode(
        prefs.flags, NextSidebarMode(SidebarModeOf(prefs.flags), toggle));
  browser_.SetLayoutPrefs(prefs);
}

void Shell::OnSidebarResized(float width) {
  LayoutPrefs prefs = store_.layout_prefs();
  const float current = prefs.sidebar_width > 0
                            ? prefs.sidebar_width
                            : design::kTokens.sidebar.default_width;
  const bool native_titlebar = store_.ui().native_titlebar.enabled;
  const float requested =
      ClampSidebarWidth(width, prefs.flags, design::kTokens, native_titlebar);
  if (requested ==
      ClampSidebarWidth(current, prefs.flags, design::kTokens, native_titlebar))
    return;
  prefs.sidebar_width = requested;
  browser_.SetLayoutPrefs(prefs);
}

void Shell::OnNextSpaceRequested() {
  const std::span<const Space> spaces = store_.spaces();
  if (spaces.size() < 2)
    return;
  const auto active =
      std::ranges::find(spaces, store_.active_space(), &Space::id);
  const std::size_t next =
      active == spaces.end()
          ? 0
          : (static_cast<std::size_t>(active - spaces.begin()) + 1) %
                spaces.size();
  browser_.SwitchSpace(spaces[next].id);
}

void Shell::OnAnchorsChanged(std::span<const Anchor> anchors) {
  const bool landings_were_dirty = store_.dirty().Has(Dirty::kOnboarding);
  for (auto& [key, cached] : anchors_)
    cached.seen = false;
  for (const Anchor& anchor : anchors) {
    const AnchorKey key{anchor.kind, anchor.id};
    const auto [it, inserted] =
        anchors_.try_emplace(key, CachedAnchor{anchor, true});
    const bool changed = inserted || it->second.value != anchor;
    it->second = {anchor, true};
    if (changed) {
      browser_.SetAnchor(anchor);
      store_.SetOnboardingLanding(anchor);
    }
  }
  for (auto it = anchors_.begin(); it != anchors_.end();) {
    if (!it->second.seen) {
      Anchor gone = it->second.value;
      it = anchors_.erase(it);
      gone.visible = false;
      browser_.SetAnchor(gone);
      store_.SetOnboardingLanding(gone);
    } else {
      ++it;
    }
  }
  if (!landings_were_dirty && store_.dirty().Has(Dirty::kOnboarding))
    ScheduleRender();
}

void Shell::OnFocusPageRequested() {
  CloseAddress();
  browser_.FocusPage();
}

// ------------------------------------------------------------------ private

void Shell::Check(const Result<void>& result, std::string_view what) {
  if (result) {
    ScheduleRender();
  } else {
    // A host bug, not ours: report it and keep the UI consistent.
    Log(LogLevel::kWarning, "{} ignored: {}", what, result.error().message);
  }
}

void Shell::Submit(std::optional<std::size_t> suggestion,
                   Disposition disposition) {
  const CommandBarState& bar = store_.ui().command_bar;
  if (!bar.open)
    return;
  if (command_opens_new_tab_ && disposition == Disposition::kCurrentTab) {
    disposition = Disposition::kNewTab;
  }

  // Copy what the host needs and close the bar *before* calling out: the host
  // may re-enter (e.g. focus_address for a new tab page) during the call.
  const std::uint32_t request_id = bar.request_id;
  const auto generation = rendered_generation_;
  const bool snapshot = browser_.SupportsSuggestionSnapshots();
  const bool use_suggestion =
      suggestion &&
      (snapshot ? request_id == rendered_request_ && generation != 0 &&
                      *suggestion < rendered_suggestion_count_
                : *suggestion < bar.suggestions.size());
  const std::string text = bar.text;
  store_.CloseCommandBar();
  store_.SetAddressFocused(false);
  command_opens_new_tab_ = false;
  ScheduleRender();

  if (use_suggestion) {
    if (snapshot)
      browser_.AcceptSuggestionSnapshot(request_id, generation, *suggestion,
                                        disposition);
    else
      browser_.AcceptSuggestion(request_id, *suggestion, disposition);
  } else if (!text.empty()) {
    browser_.Navigate(text, disposition);
  }
  browser_.CancelSuggestions(request_id);
  NotifyAddressState();
  if (!store_.ui().command_bar.open)
    browser_.FocusPage();
}

void Shell::OnNativeTitleBarChanged(const NativeTitleBarMetrics& metrics) {
  if (store_.ui().native_titlebar == metrics)
    return;
  store_.SetNativeTitleBar(metrics);
  ScheduleRender();
}

void Shell::ScheduleRender() {
  if (render_scheduled_ || !view_)
    return;
  render_scheduled_ = true;
  view_->PostTask([this, alive = std::weak_ptr<int>(lifetime_)] {
    if (alive.expired())
      return;
    render_scheduled_ = false;
    RenderNow();
  });
}

ShellLayout Shell::CurrentLayout() const {
  std::size_t tab_count = 0;
  if (TabLayoutOf(store_.layout_prefs().flags) == TabLayout::kDynamic) {
    // Layout only distinguishes zero, one and multiple visible tabs.
    for (const auto& tab : store_.tabs()) {
      if ((tab.flags.Has(TabFlag::kFavorite) || !store_.active_space() ||
           tab.space == store_.active_space()) &&
          ++tab_count == 2)
        break;
    }
  }
  return ComputeLayout(store_.window(), store_.layout_prefs(),
                       store_.ui().sidebar_revealed,
                       store_.ui().toolbar_revealed, design::kTokens,
                       store_.ui().native_titlebar, tab_count);
}

void Shell::ReportContentBounds(const ShellLayout& layout) {
  const auto& spacing = design::kTokens.spacing;
  const auto minimum = std::pair{
      std::max(kMinCardWidth, layout.native_titlebar.left_inset +
                                  layout.native_titlebar.right_inset) +
          spacing.window_margin * 2,
      std::max(spacing.toolbar_height, layout.native_titlebar.height) +
          spacing.tab_height + spacing.window_margin * 2};
  const bool metrics_changed = !last_metrics_ || *last_metrics_ != minimum;
  if (metrics_changed) {
    last_metrics_ = minimum;
    browser_.SetShellMetrics(minimum.first, minimum.second);
  }
  // Enforcing a new native minimum can synchronously resize the HWND and
  // reenter set_window_state. Do not publish the pre-resize page afterwards.
  const auto effective = metrics_changed ? CurrentLayout() : layout;
  const auto bounds = std::pair{effective.card, effective.card_radius};
  if (last_content_bounds_ && *last_content_bounds_ == bounds)
    return;
  // Cache before calling the host; native size notifications may reenter.
  last_content_bounds_ = bounds;
  browser_.SetContentBounds(bounds.first, bounds.second);
}

void Shell::RenderNow() {
  const auto started = std::chrono::steady_clock::now();
  Flags<Dirty> changed = store_.TakeDirty();
  const ShellLayout layout = CurrentLayout();

  if (!last_layout_ || *last_layout_ != layout)
    changed |= Dirty::kLayout;

  // Tell the host where the page goes, but only when it moved.
  ReportContentBounds(layout);
  if (!view_->OwnsWindowFrame() &&
      (!last_layout_ ||
       !std::ranges::equal(last_layout_->drag_area(), layout.drag_area()))) {
    browser_.SetDragRegions(layout.drag_area());
  }
  last_layout_ = layout;

  const auto& vm =
      presenter_.Present(store_, layout, changed, browser_.CanSnapLayouts(),
                         browser_.CanFolders());
  rendered_request_ = store_.ui().command_bar.request_id;
  rendered_generation_ = store_.ui().command_bar.generation;
  rendered_suggestion_count_ = vm.command_bar.suggestions.size();
  rendered_selection_ = vm.command_bar.selected;
  const auto background =
      std::pair{vm.theme.gradient_start, vm.theme.gradient_end};
  bool backdrop_changed = !last_background_ || *last_background_ != background;
  if (!last_background_ || *last_background_ != background) {
    browser_.SetWindowBackground(background.first, background.second);
    last_background_ = background;
  }
  if (changed.Has(Dirty::kTheme) || changed.Has(Dirty::kSpaces) ||
      changed.Has(Dirty::kThemeEditor)) {
    const auto value =
        vm.theme.workspace_theme ? *vm.theme.workspace_theme : WorkspaceTheme{};
    if (!last_window_theme_ || *last_window_theme_ != value ||
        last_window_theme_space_ != store_.active_space() ||
        last_window_theme_dark_ != vm.theme.dark) {
      last_window_theme_ = value;
      last_window_theme_space_ = store_.active_space();
      last_window_theme_dark_ = vm.theme.dark;
      browser_.SetWindowTheme(store_.active_space(), value, vm.theme.dark);
      backdrop_changed = true;
    }
  }
  // Incognito and high contrast keep a neutral gradient while the accent that
  // outlines a split's focused page can still change.
  if (const auto outline = SplitOutlineColor(vm.theme);
      last_pane_outline_ != outline) {
    last_pane_outline_ = outline;
    backdrop_changed = true;
  }
  const auto glance = GlanceButtonColors(vm.theme);
  const std::array glance_colors{glance.button, glance.button_hover,
                                 glance.button_icon, glance.shadow,
                                 glance.button_shadow};
  if (last_glance_colors_ != glance_colors) {
    last_glance_colors_ = glance_colors;
    backdrop_changed = true;
  }
  if (last_transparent_chrome_ != vm.theme.transparent_chrome) {
    last_transparent_chrome_ = vm.theme.transparent_chrome;
    backdrop_changed = true;
  }
  if (backdrop_changed)
    browser_.SetWindowBackdrop(vm.theme);
  view_->Render(vm);
  store_.images().ClearDropped();
  stats_.Record(std::chrono::steady_clock::now() - started);
}

}  // namespace kr
