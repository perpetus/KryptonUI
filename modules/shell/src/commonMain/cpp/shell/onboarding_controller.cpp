#include "krypton/ui/presentation/onboarding.hpp"
#include "krypton/ui/presentation/settings.hpp"
#include "krypton/ui/presentation/theme_editor.hpp"
#include "krypton/ui/shell/shell.hpp"

namespace kr {
namespace {
[[nodiscard]] constexpr std::uint32_t Bit(OnboardingStep step) noexcept {
  return 1u << std::to_underlying(step);
}
[[nodiscard]] Result<void> FromHost(KrResult result, const char* what) {
  if (result == KR_OK)
    return {};
  return Fail(result == KR_ERR_UNSUPPORTED ? ErrorCode::kUnsupported
                                           : ErrorCode::kPlatform,
              what);
}
}  // namespace

// ------------------------------------------------------------- host port

bool BrowserPort::CanOnboard() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, onboarding_finished) &&
         table_.onboarding_finished;
}
bool BrowserPort::CanImport() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, start_import) && table_.start_import;
}
bool BrowserPort::CanSetDefaultBrowser() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, request_default_browser) &&
         table_.request_default_browser;
}
bool BrowserPort::CanChooseSearch() const noexcept {
  return KR_HAS_SLOT(&table_, KrBrowser, set_default_search_engine) &&
         table_.set_default_search_engine;
}
void BrowserPort::OnboardingVisibilityChanged(bool visible) const {
  if (KR_HAS_SLOT(&table_, KrBrowser, onboarding_visibility_changed) &&
      table_.onboarding_visibility_changed)
    table_.onboarding_visibility_changed(table_.self, visible);
}
Result<void> BrowserPort::StartImport(ImportSourceId source,
                                      Flags<ImportData> items) const {
  if (!CanImport())
    return Fail(ErrorCode::kUnsupported, "Import unavailable");
  return FromHost(table_.start_import(table_.self, source.value, items.bits()),
                  "Import rejected");
}
Result<void> BrowserPort::RequestDefaultBrowser() const {
  if (!CanSetDefaultBrowser())
    return Fail(ErrorCode::kUnsupported, "Default browser unavailable");
  return FromHost(table_.request_default_browser(table_.self),
                  "Default browser request rejected");
}
Result<void> BrowserPort::SetDefaultSearchEngine(SearchEngineId id) const {
  if (!CanChooseSearch())
    return Fail(ErrorCode::kUnsupported, "Search choice unavailable");
  return FromHost(table_.set_default_search_engine(table_.self, id.value),
                  "Search engine rejected");
}
void BrowserPort::OnboardingFinished(OnboardingOutcome outcome,
                                     OnboardingStep last_step) const {
  if (!CanOnboard())
    return;
  const KrOnboardingResult value{sizeof(KrOnboardingResult),
                                 std::to_underlying(outcome),
                                 std::to_underlying(last_step)};
  table_.onboarding_finished(table_.self, &value);
}

// ------------------------------------------------------------- state in

Result<void> Shell::ShowOnboarding(bool first_run, OnboardingStep step) {
  if (!browser_.CanOnboard())
    return Fail(ErrorCode::kUnsupported, "host cannot record onboarding");
  if (step > OnboardingStep::kReady)
    return Fail(ErrorCode::kInvalidArgument, "Invalid onboarding step");
  if (OnboardingActive())
    return {};
  // The onboarding takes the whole window: nothing else stays open over it.
  if (store_.ui().settings.open)
    OnSettingsDismissed();
  if (store_.library().open)
    OnLibraryDismissed();
  OnThemeEditorDismissed();
  OnWorkspaceEditorDismissed();
  OnTabPreviewDismissed(store_.ui().tab_preview.request_id);
  CloseAddress();
  while (!menu_anchors_.empty())
    DismissMenu(menu_anchors_.begin()->first);

  {
    auto& state = store_.EditOnboarding();
    const auto serial = state.open_serial + 1;
    state = {};
    state.open = true;
    state.first_run = first_run;
    state.open_serial = serial;
    state.step_serial = 1;
    state.capabilities = {.layout = browser_.CanEditLayout(),
                          .theme = browser_.CanEditTheme(),
                          .import = browser_.CanImport(),
                          .search = browser_.CanChooseSearch(),
                          .privacy = browser_.CanEditPrivacyFeatures(),
                          .default_browser = browser_.CanSetDefaultBrowser()};
  }
  for (const auto& [key, cached] : anchors_)
    store_.SetOnboardingLanding(cached.value);
  // The catalog is observed for the whole onboarding, so the privacy moment
  // knows whether it has anything to offer before the user reaches it.
  const bool privacy_was_visible = store_.privacy_features().visible;
  store_.EditPrivacyFeatures().SetVisible(
      true, browser_.CanEditPrivacyFeatures(), browser_.CanRelaunch());
  ScheduleRender();
  browser_.OnboardingVisibilityChanged(true);
  if (!privacy_was_visible)
    browser_.PrivacyFeaturesVisibilityChanged(true);
  // The host may publish synchronously above; resolve the start afterwards.
  if (step != OnboardingStep::kIgnition && OnboardingActive()) {
    const auto& state = store_.onboarding();
    const auto& privacy = store_.privacy_features();
    GoToOnboardingStep(
        OnboardingStepAvailable(step, state, privacy, store_.browser_services())
            ? step
            : NextOnboardingStep(step, 1, state, privacy,
                                 store_.browser_services()),
        1);
  }
  return {};
}

Result<void> Shell::UpsertImportSource(ImportSource value) {
  auto result = store_.EditOnboarding().UpsertImportSource(std::move(value));
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::RemoveImportSource(ImportSourceId id) {
  auto result = store_.EditOnboarding().RemoveImportSource(id);
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::SetImportProgress(const ImportProgress& value) {
  auto result = store_.EditOnboarding().SetImportProgress(value);
  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::SetDefaultBrowserState(Flags<DefaultBrowserFlag> flags) {
  auto result = store_.EditBrowserServices().SetDefaultBrowser(flags);
  if (result && !flags.Has(DefaultBrowserFlag::kPending)) {
    store_.EditOnboarding().default_requested = false;
    const auto& action = store_.browser_settings().action;
    if (action.request && action.action == SettingsAction::kMakeDefault) {
      const auto completed = CompleteSettingsOperation(
          action.request, flags.Has(DefaultBrowserFlag::kIsDefault));
      (void)completed;
    }
  }

  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::UpsertSearchEngine(SearchEngine value) {
  auto result =
      store_.EditBrowserServices().UpsertSearchEngine(std::move(value));
  if (result) {
    auto& settings = store_.EditBrowserSettings();
    if (settings.pending_engine &&
        (store_.browser_services().DefaultEngine().value ==
             settings.pending_engine ||
         store_.browser_services().SearchManaged()))
      settings.pending_engine = 0;
  }

  if (result)
    ScheduleRender();
  return result;
}
Result<void> Shell::RemoveSearchEngine(SearchEngineId id) {
  auto result = store_.EditBrowserServices().RemoveSearchEngine(id);
  if (result && store_.onboarding().draft.engine == id)
    store_.EditOnboarding().draft.engine = {};
  if (result && store_.browser_settings().pending_engine == id.value) {
    auto& state = store_.EditBrowserSettings();
    state.pending_engine = 0;
    state.error = true;
  }

  if (result)
    ScheduleRender();
  return result;
}

// ------------------------------------------------------------- user input

void Shell::OnOnboardingAction(OnboardingAction action) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing)
    return;
  const auto step = state.step;
  const auto& privacy = store_.privacy_features();
  switch (action) {
    case OnboardingAction::kNext:
      if (step == OnboardingStep::kReady) {
        FinishOnboarding(OnboardingOutcome::kCompleted);
        return;
      }
      CommitOnboardingStep();
      GoToOnboardingStep(NextOnboardingStep(step, 1, store_.onboarding(),
                                            store_.privacy_features(),
                                            store_.browser_services()),
                         1);
      return;
    case OnboardingAction::kBack:
      if (step == OnboardingStep::kIgnition)
        return;
      if (step == OnboardingStep::kColor)
        LeaveOnboardingColor(false);
      GoToOnboardingStep(NextOnboardingStep(step, -1, state, privacy,
                                            store_.browser_services()),
                         -1);
      return;
    case OnboardingAction::kRecommended:
      if (step == OnboardingStep::kColor)
        LeaveOnboardingColor(false);
      GoToOnboardingStep(OnboardingStep::kReady, 1);
      return;
    case OnboardingAction::kSkip:
      FinishOnboarding(OnboardingOutcome::kSkipped);
      return;
    case OnboardingAction::kFinish:
      if (step == OnboardingStep::kReady)
        FinishOnboarding(OnboardingOutcome::kCompleted);
      return;
  }
}

void Shell::OnOnboardingLayoutHovered(std::optional<LayoutChoice> choice) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing || state.step != OnboardingStep::kShape ||
      state.chrome_veiled || (choice && *choice > LayoutChoice::kCollapsed))
    return;
  std::optional<LayoutPrefs> ghost;
  if (choice && *choice != SelectedLayout(store_.layout_prefs()))
    ghost = SelectLayout(store_.layout_prefs(), *choice);
  if (state.ghost == ghost)
    return;
  store_.EditOnboarding().ghost = ghost;
  ScheduleRender();
}

void Shell::OnOnboardingLayoutChosen(LayoutChoice choice) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing || state.step != OnboardingStep::kShape ||
      !state.capabilities.layout || choice > LayoutChoice::kCollapsed)
    return;
  const auto prefs = SelectLayout(store_.layout_prefs(), choice);
  if (prefs.flags == store_.layout_prefs().flags)
    return;
  // Veil the chrome while the host applies it; the ghost morphs meanwhile.
  auto& edit = store_.EditOnboarding();
  edit.ghost = prefs;
  edit.chrome_veiled = true;
  ++edit.morph_serial;
  ScheduleRender();
  browser_.SetLayoutPrefs(prefs);
}

void Shell::OnOnboardingSideChosen(bool right) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing || state.step != OnboardingStep::kShape ||
      !state.capabilities.layout ||
      !LayoutOptionAvailable(store_.layout_prefs(),
                             LayoutOption::kSidebarRight))
    return;
  const auto prefs =
      EditLayout(store_.layout_prefs(), {LayoutOption::kSidebarRight, right});
  if (prefs.flags == store_.layout_prefs().flags)
    return;
  auto& edit = store_.EditOnboarding();
  edit.ghost = prefs;
  edit.chrome_veiled = true;
  ++edit.morph_serial;
  ScheduleRender();
  browser_.SetLayoutPrefs(prefs);
}

void Shell::OnOnboardingMorphSettled(std::uint32_t serial) {
  const auto& state = store_.onboarding();
  if (!state.open || serial != state.morph_serial || !state.chrome_veiled)
    return;
  // Reveal whatever the host has confirmed by now; a later confirmation just
  // updates the chrome like any other layout change.
  auto& edit = store_.EditOnboarding();
  edit.chrome_veiled = false;
  edit.ghost.reset();
  ScheduleRender();
}

void Shell::OnOnboardingColorPoint(Point client) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing || state.step != OnboardingStep::kColor ||
      !state.theme_editing)
    return;
  const Rect field =
      OnboardingColorField(store_.window().client, CurrentLayout());
  if (!store_.EditOnboardingTheme(
          {ThemeEditKind::kMovePrimary, ColorFieldPoint(field, client)}))
    return;
  browser_.SetWorkspaceTheme(state.theme_space, state.theme, false);
  ScheduleRender();
}

void Shell::OnOnboardingThemeEdited(const ThemeEdit& edit, Point origin) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing || state.step != OnboardingStep::kColor ||
      !state.theme_editing || edit.kind == ThemeEditKind::kPreviousPage ||
      edit.kind == ThemeEditKind::kNextPage)
    return;
  if (!store_.EditOnboardingTheme(edit))
    return;
  // Discrete changes spread from where they were made; drags just follow.
  if (edit.kind != ThemeEditKind::kMovePrimary &&
      edit.kind != ThemeEditKind::kOpacity &&
      edit.kind != ThemeEditKind::kTexture) {
    auto& reveal = store_.EditOnboarding().reveal;
    reveal.origin = origin;
    ++reveal.serial;
  }
  browser_.SetWorkspaceTheme(state.theme_space, state.theme, false);
  ScheduleRender();
}

void Shell::OnOnboardingImportChosen(ImportSourceId source) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing || state.step != OnboardingStep::kImport)
    return;
  const auto* found = source ? state.FindImportSource(source) : nullptr;
  if (source && !found)
    return;
  auto& draft = store_.EditOnboarding().draft;
  draft.import_source = source;
  if (found)
    draft.import_items = found->items;
  ScheduleRender();
}

void Shell::OnOnboardingImportItemToggled(ImportData item, bool enabled) {
  const auto& state = store_.onboarding();
  const auto* source = state.FindImportSource(state.draft.import_source);
  if (!state.open || state.finishing || state.step != OnboardingStep::kImport ||
      !source || !source->items.Has(item) ||
      state.draft.import_items.Has(item) == enabled)
    return;
  store_.EditOnboarding().draft.import_items.Set(item, enabled);
  ScheduleRender();
}

void Shell::OnOnboardingSearchChosen(SearchEngineId engine) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing || state.step != OnboardingStep::kSearch ||
      !store_.browser_services().FindSearchEngine(engine) ||
      state.draft.engine == engine)
    return;
  store_.EditOnboarding().draft.engine = engine;
  ScheduleRender();
}

void Shell::OnOnboardingDefaultChoice(bool make_default) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing ||
      state.step != OnboardingStep::kDefaultBrowser)
    return;
  if (!make_default) {
    OnOnboardingAction(OnboardingAction::kNext);
    return;
  }
  const auto flags = store_.browser_services().default_browser;
  if (state.default_requested || flags.Has(DefaultBrowserFlag::kIsDefault) ||
      flags.Has(DefaultBrowserFlag::kManaged) ||
      !flags.Has(DefaultBrowserFlag::kCanSet))
    return;
  {
    auto& edit = store_.EditOnboarding();
    edit.draft.make_default = true;
    edit.default_requested = true;
    edit.committed |= Bit(OnboardingStep::kDefaultBrowser);
  }
  ScheduleRender();
  // Windows answers later; the host pushes the result when it returns.
  if (!browser_.RequestDefaultBrowser()) {
    store_.EditOnboarding().default_requested = false;
    ScheduleRender();
  }
}

void Shell::OnOnboardingHandoffFinished() {
  const auto& state = store_.onboarding();
  if (!state.open || !state.finishing)
    return;
  const bool completed = state.outcome == OnboardingOutcome::kCompleted;
  CloseOnboarding();
  // The card became the address field: the first search is the last step.
  if (completed)
    FocusAddress(true);
  else
    browser_.FocusPage();
}

// ------------------------------------------------------------- private

void Shell::GoToOnboardingStep(OnboardingStep step, int direction) {
  {
    auto& edit = store_.EditOnboarding();
    if (edit.step == step)
      return;
    edit.step = step;
    edit.direction = direction < 0 ? std::int8_t{-1} : std::int8_t{1};
    ++edit.step_serial;
    edit.ghost.reset();
    edit.chrome_veiled = false;
  }
  if (step == OnboardingStep::kColor)
    EnterOnboardingColor();
  ScheduleRender();
}

void Shell::EnterOnboardingColor() {
  const SpaceId space = store_.active_space();
  auto& state = store_.EditOnboarding();
  if (!state.capabilities.theme)
    return;
  if (!state.theme_ready || state.theme_space != space) {
    // The editor's own starting point: the saved theme, or one derived from
    // the workspace accent.
    const auto* current = store_.WorkspaceThemeFor(space);
    WorkspaceTheme value = current ? *current : WorkspaceTheme{};
    if (!current || !current->customized) {
      const auto* workspace = store_.FindSpace(space);
      value =
          ThemeFromAccent(workspace ? workspace->color : store_.theme().accent,
                          current ? current->scheme : ThemeScheme::kSystem);
    }
    state.theme_ready = true;
    state.theme_space = space;
    state.theme = value;
    state.theme_before = current ? *current : WorkspaceTheme{};
    state.theme_edited = false;
  }
  store_.SetOnboardingThemeEditing(true);
  if (store_.onboarding().theme_edited)
    browser_.SetWorkspaceTheme(space, store_.onboarding().theme, false);
}

void Shell::LeaveOnboardingColor(bool commit) {
  const auto& state = store_.onboarding();
  if (!state.theme_editing)
    return;
  const auto space = state.theme_space;
  const bool edited = state.theme_edited;
  store_.SetOnboardingThemeEditing(false);
  if (!edited)
    return;
  if (commit) {
    auto& edit = store_.EditOnboarding();
    edit.theme_before = edit.theme;
    edit.theme_edited = false;
    browser_.SetWorkspaceTheme(space, edit.theme, true);
  } else {
    // Keep the draft for a return visit; put the host's preview back.
    browser_.SetWorkspaceTheme(space, state.theme_before, false);
  }
}

void Shell::CommitOnboardingStep() {
  const auto& state = store_.onboarding();
  const auto step = state.step;
  switch (step) {
    case OnboardingStep::kIgnition:
    case OnboardingStep::kShape:
    case OnboardingStep::kPrivacy:
    case OnboardingStep::kDefaultBrowser:
    case OnboardingStep::kReady:
      break;
    case OnboardingStep::kColor:
      LeaveOnboardingColor(true);
      break;
    case OnboardingStep::kImport: {
      const auto* source = state.FindImportSource(state.draft.import_source);
      const bool busy = state.import_requested ||
                        state.import.phase == ImportPhase::kRunning ||
                        state.import.phase == ImportPhase::kSucceeded;
      if (!source || busy)
        break;
      const auto items = state.draft.import_items & source->items;
      if (items.none())
        break;
      // The host may publish progress or replace its sources before returning.
      const auto id = source->id;
      const auto icon = source->icon;
      store_.EditOnboarding().import_requested = true;
      if (browser_.StartImport(id, items))
        LandOnboarding(Element::kLibrary, icon);
      else
        store_.EditOnboarding().import_requested = false;
      break;
    }
    case OnboardingStep::kSearch: {
      const auto engine = state.draft.engine
                              ? state.draft.engine
                              : store_.browser_services().DefaultEngine();
      const auto* found = store_.browser_services().FindSearchEngine(engine);
      if (!found)
        break;
      const auto icon = found->icon;
      if (engine == store_.browser_services().DefaultEngine() ||
          browser_.SetDefaultSearchEngine(engine))
        LandOnboarding(Element::kAddress, icon);
      break;
    }
  }
  store_.EditOnboarding().committed |= Bit(step);
}

void Shell::LandOnboarding(Element target, ImageId icon) {
  if (!store_.onboarding().Landing(target))
    return;
  auto& flight = store_.EditOnboarding().flight;
  flight.target = target;
  flight.icon = icon;
  ++flight.serial;
  ScheduleRender();
}

void Shell::FinishOnboarding(OnboardingOutcome outcome) {
  const auto& state = store_.onboarding();
  if (!state.open || state.finishing)
    return;
  if (state.step == OnboardingStep::kColor)
    LeaveOnboardingColor(false);
  const auto step = state.step;
  {
    auto& edit = store_.EditOnboarding();
    edit.finishing = true;
    edit.outcome = outcome;
    edit.ghost.reset();
    edit.chrome_veiled = false;
    edit.reported = true;
  }
  ScheduleRender();
  browser_.OnboardingFinished(outcome, step);
}

void Shell::CloseOnboarding() {
  if (!OnboardingActive())
    return;
  store_.SetOnboardingThemeEditing(false);
  const bool privacy_visible = store_.privacy_features().visible;
  {
    auto& edit = store_.EditOnboarding();
    const auto serial = edit.open_serial;
    edit = {};
    edit.open_serial = serial;
  }
  if (privacy_visible)
    store_.EditPrivacyFeatures().SetVisible(
        false, browser_.CanEditPrivacyFeatures(), browser_.CanRelaunch());
  ScheduleRender();
  browser_.OnboardingVisibilityChanged(false);
  if (privacy_visible)
    browser_.PrivacyFeaturesVisibilityChanged(false);
}
}  // namespace kr
