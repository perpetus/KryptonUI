#include <windows.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "boost_editor.hpp"
#include "browser_tools_window.hpp"
#include "chrome_header.hpp"
#include "chrome_reveal.hpp"
#include "command_bar.hpp"
#include "delayed_tasks.hpp"
#include "floating_chrome.hpp"
#include "icon_view.hpp"
#include "input_trace.hpp"
#include "island_hide.hpp"
#include "krypton/ui/core/env.hpp"
#include "krypton/ui/core/log.hpp"
#include "krypton/ui/layout/popup_layout.hpp"
#include "krypton/ui/model/focus_ownership.hpp"
#include "krypton/ui/platform/platform.hpp"
#include "library_panel.hpp"
#include "menu_presenter.hpp"
#include "native_frame.hpp"
#include "native_input.hpp"
#include "native_titlebar.hpp"
#include "onboarding_view.hpp"
#include "permission_prompt.hpp"
#include "settings_window.hpp"
#include "sidebar_chrome.hpp"
#include "sidebar_splitter.hpp"
#include "split_chooser.hpp"
#include "tab_list.hpp"
#include "tab_preview_surface.hpp"
#include "theme_backdrop.hpp"
#include "theme_editor.hpp"
#include "top_chrome.hpp"
#include "transient_surface.hpp"
#include "url_drop_target.hpp"
#include "winui_runtime.hpp"
#include "workspace_editor.hpp"

namespace kr {
namespace {
using namespace win;
using controls::Control;
using controls::Grid;
using winrt::Windows::System::VirtualKey;
namespace hosting = xaml::Hosting;

template <typename Component, typename... Args>
Component CreateNativeComponent(std::string_view name, Args&&... args) {
  try {
    return Component(std::forward<Args>(args)...);
  } catch (const winrt::hresult_error& error) {
    throw winrt::hresult_error(
        error.code(), winrt::to_hstring(std::format(
                          "{}: {}", name, winrt::to_string(error.message()))));
  }
}

bool IsPointerPress(UINT message) noexcept {
  return message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
         message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN ||
         message == WM_POINTERDOWN;
}

bool IsPointerRelease(UINT message) noexcept {
  return message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
         message == WM_MBUTTONUP || message == WM_XBUTTONUP ||
         message == WM_POINTERUP;
}

HWND WindowOf(const hosting::DesktopWindowXamlSource& source) {
  return source ? winrt::Microsoft::UI::GetWindowFromWindowId(
                      source.SiteBridge().WindowId())
                : nullptr;
}

bool AcceptsText(const winrt::Windows::Foundation::IInspectable& element) {
  return element.try_as<controls::TextBox>() ||
         element.try_as<controls::RichEditBox>() ||
         element.try_as<controls::PasswordBox>();
}

// Popup content, such as an open flyout, is not part of the root's tree.
bool WithinRoot(const winrt::Windows::Foundation::IInspectable& element,
                const xaml::UIElement& root) {
  for (auto node = element.try_as<xaml::DependencyObject>(); node;
       node = media::VisualTreeHelper::GetParent(node))
    if (node == root)
      return true;
  return !element;
}

struct RootEvents {
  xaml::FrameworkElement::LayoutUpdated_revoker layout;
  xaml::UIElement::GotFocus_revoker focused;
  xaml::UIElement::KeyDown_revoker key;
  xaml::UIElement::ProcessKeyboardAccelerators_revoker fallback;
  xaml::UIElement::PointerEntered_revoker entered;
  xaml::UIElement::PointerExited_revoker exited;
  xaml::FrameworkElement::Loaded_revoker loaded;
  xaml::UIElement::RightTapped_revoker context;
  hosting::DesktopWindowXamlSource::GotFocus_revoker native_focus;
  hosting::DesktopWindowXamlSource::TakeFocusRequested_revoker depart;
  winrt::Microsoft::UI::Input::InputPointerSource pointer{nullptr};
  winrt::event_token pressed{}, released{}, capture_lost{}, routed_away{},
      routed_released{};
  RootEvents() = default;
  RootEvents(const RootEvents&) = delete;
  RootEvents& operator=(const RootEvents&) = delete;
  ~RootEvents() { Reset(); }
  void Reset() noexcept {
    layout.revoke();
    focused.revoke();
    key.revoke();
    fallback.revoke();
    entered.revoke();
    exited.revoke();
    loaded.revoke();
    context.revoke();
    native_focus.revoke();
    depart.revoke();
    // CoreInput's source does not provide the weak references required by
    // auto_revoke. Keep the source alive and revoke its tokens in this owner.
    if (pointer) {
      pointer.PointerPressed(pressed);
      pointer.PointerReleased(released);
      pointer.PointerCaptureLost(capture_lost);
      pointer.PointerRoutedAway(routed_away);
      pointer.PointerRoutedReleased(routed_released);
      pointer = nullptr;
    }
  }
};

class WinUIShellView final : public ShellView {
 public:
  WinUIShellView(HWND parent,
                 ShellViewDelegate& delegate,
                 const ThemeVm& initial_theme)
      : parent_(parent),
        delegate_(delegate),
        tab_previews_(parent, motion_),
        glass_(delegate_,
               [this](const xaml::FrameworkElement& element) {
                 return Bounds(element);
               }),
        context_{delegate_,
                 brushes_,
                 images_,
                 [this](const xaml::FrameworkElement& element) {
                   return Bounds(element);
                 },
                 [this](const xaml::FrameworkElement& element) {
                   return PointerOver(element);
                 },
                 [this](MenuKind kind, std::uint64_t target, Rect anchor) {
                   delegate_.OnMenuRequested(kind, target, anchor);
                 },
                 [this, weak = std::weak_ptr<int>(lifetime_)] {
                   if (!weak.expired()) {
                     QueueHoverRefresh();
                     QueueFocusReturn();
                   }
                 },
                 tab_previews_,
                 motion_,
                 input_,
                 icons_,
                 glass_,
                 [this] { return WorkspaceGesturesAllowed(); },
                 [this](WorkspaceSwipePhase phase) {
                   workspace_gesture_active_ =
                       phase == WorkspaceSwipePhase::kBegin ||
                       phase == WorkspaceSwipePhase::kUpdate;
                   trace_.Record(InputTraceEvent::kSwipe, parent_,
                                 focus_.generation(),
                                 static_cast<std::uint64_t>(phase));
                 },
                 [this, weak = std::weak_ptr<int>(lifetime_)](
                     const xaml::FrameworkElement& preview,
                     bool active) {
                   return !weak.expired() && HoverPopupAllowed(preview, active);
                 },
                 [this, weak = std::weak_ptr<int>(lifetime_)](
                     const xaml::FrameworkElement& keep) {
                   if (!weak.expired())
                     sidebar_.DismissHoverPopups(keep);
                 },
                 [this](Point point, bool active) {
                   (void)UrlDragChanged(point, active);
                 }},
        header_(CreateNativeComponent<ChromeHeader>("Chrome header",
                                                    context_,
                                                    anchors_dirty_)),
        tabs_(CreateNativeComponent<TabList>("Tab list",
                                             context_,
                                             anchors_dirty_)),
        sidebar_(CreateNativeComponent<SidebarChrome>("Sidebar",
                                                      context_,
                                                      header_,
                                                      tabs_.Root())),
        top_chrome_(CreateNativeComponent<TopChrome>("Top chrome",
                                                     context_,
                                                     header_,
                                                     tabs_)),
        splitter_(context_),
        command_bar_(parent, context_),
        theme_editor_(parent, context_),
        menu_(parent,
              context_,
              [this] {
                shown_menu_request_ = 0;
                menu_loaded_.revoke();
                ReleaseReveals();
                QueueFocusReturn();
              }),
        library_(parent, context_, sidebar_.Root()),
        notification_(parent) {
    tabs_.SetNewTabButton(sidebar_.NewTabButton());
    root_.Children().Append(sidebar_.Root());
    root_.Children().Append(splitter_.Root());
    toolbar_root_ = top_chrome_.Root();
    brushes_.Apply(initial_theme);
    const auto initial_scheme = initial_theme.dark ? xaml::ElementTheme::Dark
                                                   : xaml::ElementTheme::Light;
    root_.RequestedTheme(initial_scheme);
    toolbar_root_.RequestedTheme(initial_scheme);
    Grid::SetRowSpan(toolbar_backdrop_.Root(), 2);
    root_.Children().InsertAt(0, sidebar_backdrop_.Root());
    toolbar_root_.Children().InsertAt(0, toolbar_backdrop_.Root());
    brushes_.Install(root_);
    brushes_.Install(toolbar_root_);
    menu_.InstallStyle(root_);
    menu_.InstallStyle(toolbar_root_);
    root_.Background(brushes_.sidebar_gradient);
    toolbar_root_.Background(brushes_.toolbar_gradient);
    sidebar_surface_.Initialize(root_);
    toolbar_surface_.Initialize(toolbar_root_);
    const auto initialize = [&](hosting::DesktopWindowXamlSource& source,
                                const Grid& root, NativeInputTarget& target) {
      source = hosting::DesktopWindowXamlSource();
      const auto island_parent = target.IslandParent(parent_);
      winrt::check_bool(island_parent.has_value());
      source.Initialize(
          winrt::Microsoft::UI::GetWindowIdFromWindow(*island_parent));
      source.ShouldConstrainPopupsToWorkArea(true);
      source.Content(root);
      winrt::check_bool(
          target
              .Initialize(winrt::Microsoft::UI::GetWindowFromWindowId(
                              source.SiteBridge().WindowId()),
                          root)
              .has_value());
    };
    initialize(source_, sidebar_surface_.Root(), input_targets_[0]);
    initialize(toolbar_source_, toolbar_surface_.Root(), input_targets_[1]);
    if (auto result = titlebar_.Initialize(parent_, delegate_); !result)
      platform::WriteDefaultLog(LogLevel::kWarning, result.error().message);
    titlebar_.ApplyTheme(initial_theme, brushes_.high_contrast());
    if (auto result = frame_.Initialize(
            parent_, this,
            [](void* self) {
              static_cast<WinUIShellView*>(self)->motion_.RefreshContrast();
            },
            [](void* self, bool active) {
              static_cast<WinUIShellView*>(self)->OnActivationChanged(active);
            });
        !result)
      throw winrt::hresult_error(E_FAIL,
                                 winrt::to_hstring(result.error().message));
    AttachFrameIslands();
    if (auto result = input_.Initialize(
            parent_, this,
            [](void* self, const MSG& message) {
              return static_cast<WinUIShellView*>(self)->ProcessNativeMessage(
                  message);
            },
            [](void* self, const MSG& message) {
              return static_cast<WinUIShellView*>(self)
                  ->delegate_.OnUnhandledKey(&message);
            },
            [](void* self, HWND target) {
              auto& view = *static_cast<WinUIShellView*>(self);
              view.trace_.Record(InputTraceEvent::kFocus, target,
                                 view.focus_.generation(),
                                 reinterpret_cast<std::uintptr_t>(GetFocus()));
              view.QueueFocusReturn();
            });
        !result)
      throw winrt::hresult_error(E_FAIL,
                                 winrt::to_hstring(result.error().message));
    WireRoot(false, events_[0]);
    WireRoot(true, events_[1]);
    sidebar_drop_ = std::make_unique<UrlDropTarget>(context_, root_);
    toolbar_drop_ = std::make_unique<UrlDropTarget>(context_, toolbar_root_);
  }

  ~WinUIShellView() override {
    trace_.Dump();
    focus_.SetActive(false);
    lifetime_.reset();
    input_.Reset();
    sidebar_drop_.reset();
    toolbar_drop_.reset();
    for (auto& target : input_targets_)
      target.Reset();
    onboarding_.reset();
    frame_.Reset();
    settings_.reset();
    task_manager_.reset();
    permission_manager_.reset();
    permission_prompt_.reset();
    boost_editor_.reset();
    workspace_editor_.reset();
    delayed_tasks_.Reset();
    for (auto& events : events_)
      events.Reset();
    tab_previews_.Reset();
    titlebar_.Reset();
    menu_loaded_.revoke();
    menu_.Close();
    command_bar_.Close();
    theme_editor_.Close();
    library_.Close();
    for (const auto& source : {source_, toolbar_source_, menu_source_}) {
      if (source) {
        source.Content(nullptr);
        source.Close();
      }
    }
  }

  void Render(const ShellViewModel& vm) override {
    glass_.Update(vm);
    if (vm.split_chooser.open && !split_chooser_)
      split_chooser_ = std::make_unique<SplitChooser>(
          parent_, context_,
          [this](bool blocked) { command_bar_.SetFocusBlocked(blocked); });
    if (split_chooser_ &&
        vm.changed.HasAny(Dirty::kMultiPage | Dirty::kTabs | Dirty::kImages |
                          Dirty::kText | Dirty::kTheme | Dirty::kWindow |
                          Dirty::kLayout))
      split_chooser_->Render(
          vm.split_chooser, vm.text, vm.client, vm.scale_factor,
          vm.theme.dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light,
          vm.images);
    const bool profile = !rendered_ && EnvFlag("KR_UI_PROFILE");
    std::array<std::chrono::steady_clock::time_point, 7> stages{};
    std::array<std::chrono::steady_clock::time_point, 3> theme_stages{};
    const auto mark = [&](std::size_t stage) {
      if (profile)
        stages[stage] = std::chrono::steady_clock::now();
    };
    mark(0);
    const bool boost_open =
        vm.boost_editor && vm.boost_editor->open && !vm.boost_editor->picking;
    modal_open_ = boost_open || vm.settings.state.open ||
                  vm.workspace_editor.open || vm.split_chooser.open ||
                  vm.onboarding.open;
    const bool command_was_open = command_bar_.IsOpen();
    rendering_ = true;
    struct RenderScope {
      bool& active;
      ~RenderScope() { active = false; }
    } scope{rendering_};
    if (modal_open_ || vm.library.open || vm.command_bar.open ||
        vm.theme_editor.open)
      sidebar_.DismissHoverPopups();
    if (vm.changed.Has(Dirty::kTheme) || !rendered_) {
      brushes_.Apply(vm.theme);
      if (profile)
        theme_stages[0] = std::chrono::steady_clock::now();
      titlebar_.ApplyTheme(vm.theme, brushes_.high_contrast());
      if (profile)
        theme_stages[1] = std::chrono::steady_clock::now();
      const auto theme =
          vm.theme.dark ? xaml::ElementTheme::Dark : xaml::ElementTheme::Light;
      if (root_.RequestedTheme() != theme)
        root_.RequestedTheme(theme);
      if (toolbar_root_.RequestedTheme() != theme)
        toolbar_root_.RequestedTheme(theme);
      if (profile)
        theme_stages[2] = std::chrono::steady_clock::now();
      icons_.SetPack(vm.theme.icon_pack, rendered_);
      sidebar_.SetIncognito(vm.theme.incognito);
    }
    if (vm.changed.Has(Dirty::kTheme) || vm.changed.Has(Dirty::kWindow) ||
        !rendered_)
      sidebar_.SetLibraryIcon(vm.theme, vm.scale_factor);
    if (vm.changed.Has(Dirty::kText) || !rendered_) {
      const auto direction = vm.text.right_to_left
                                 ? xaml::FlowDirection::RightToLeft
                                 : xaml::FlowDirection::LeftToRight;
      root_.FlowDirection(direction);
      toolbar_root_.FlowDirection(direction);
      header_.SetText(vm.text);
      sidebar_.SetText(vm.text);
      splitter_.SetText(vm.text);
      tabs_.SetText(vm.text);
      top_chrome_.SetText(vm.text);
      command_bar_.SetText(vm.text);
      menu_.SetText(vm.text);
      anchors_dirty_ = true;
    }
    mark(1);
    if (vm.changed.Has(Dirty::kLayout) || vm.changed.Has(Dirty::kWindow) ||
        !rendered_) {
      layout_ = vm.layout;
      sidebar_.SetWorkspaceSwipeEnabled(
          vm.settings.layout.flags.Has(LayoutFlag::kWorkspaceSwipe));
      fullscreen_ = vm.header.fullscreen;
      frame_.Update(layout_.drag_area(), drag_exclusions_, fullscreen_);
      client_ = vm.client;
      scale_ = vm.scale_factor;
      titlebar_.SetScale(vm.scale_factor);
      if (auto result = titlebar_.SetCaptionVisible(
              !fullscreen_ &&
              (!layout_.caption_overlay || layout_.caption_visible));
          !result)
        platform::WriteDefaultLog(LogLevel::kWarning, result.error().message);
      // Preserve outgoing rows until the whole toolbar has slid out. Applying
      // the hidden layout here collapses them in the middle of the transition.
      const bool toolbar_leaving =
          !layout_.toolbar_visible && reveal_animated_[1] &&
          toolbar_root_.Visibility() == xaml::Visibility::Visible;
      if (!toolbar_leaving)
        top_chrome_.SetLayout(layout_, sidebar_.Root());
      sidebar_.SetIconOnly(layout_.sidebar_icon_only);
      sidebar_.SetLibraryPlacement(layout_.tabs != TabLayout::kVertical,
                                   header_.LibrarySlot());
      tabs_.SetIconOnly(layout_.sidebar_icon_only);
      splitter_.SetLayout(layout_, vm.scale_factor);
      PlaceIsland(source_, root_, brushes_.sidebar_gradient, island_hide_[0],
                  input_targets_[0], layout_.sidebar, layout_.sidebar_island,
                  layout_.sidebar_visible, vm.scale_factor);
      PlaceIsland(toolbar_source_, toolbar_root_, brushes_.toolbar_gradient,
                  island_hide_[1], input_targets_[1], layout_.toolbar,
                  layout_.toolbar_island, layout_.toolbar_visible,
                  vm.scale_factor);
      anchors_dirty_ = true;
    }
    if (vm.changed.HasAny(Dirty::kTheme | Dirty::kLayout | Dirty::kWindow) ||
        !rendered_) {
      // Quality glass leaves steady chrome unpainted over the host's system
      // backdrop. Reveals that float over the page keep the opaque gradient.
      const bool clear = vm.theme.transparent_chrome;
      const auto border = brushes_.high_contrast()
                              ? brushes_.icon
                              : brushes_.For(Color{kTokens.compact.border});
      const auto paint = [](const Grid& root, const media::Brush& brush) {
        if (root.Background() != brush)
          root.Background(brush);
      };
      // Hidden layout geometry is not the outgoing surface's geometry. Retain
      // its margins, clipping, opaque floor and shadow until IslandHide runs.
      if (layout_.sidebar_visible) {
        sidebar_surface_.Update(layout_.sidebar, layout_.sidebar_island,
                                layout_.sidebar_overlays, vm.theme.dark,
                                brushes_.high_contrast());
        root_.BorderBrush(border);
        paint(root_, clear && !layout_.sidebar_overlays
                         ? media::Brush(brushes_.transparent)
                         : media::Brush(brushes_.sidebar_gradient));
      }
      if (layout_.toolbar_visible) {
        toolbar_surface_.Update(layout_.toolbar, layout_.toolbar_island,
                                layout_.toolbar_overlays, vm.theme.dark,
                                brushes_.high_contrast());
        toolbar_root_.BorderBrush(border);
        paint(toolbar_root_, clear && !layout_.toolbar_overlays
                                 ? media::Brush(brushes_.transparent)
                                 : media::Brush(brushes_.toolbar_gradient));
      }
    }
    if (vm.settings.state.open && !settings_)
      settings_ = std::make_unique<SettingsWindow>(parent_, context_);
    if (settings_ &&
        (vm.changed.Has(Dirty::kSettings) || vm.changed.Has(Dirty::kTheme) ||
         vm.changed.Has(Dirty::kLayout) || vm.changed.Has(Dirty::kText) ||
         !rendered_))
      settings_->Render(vm);
    images_.Drop(vm.dropped_images);
    if (vm.browser_tools.tasks_open && !task_manager_)
      task_manager_ =
          std::make_unique<BrowserToolWindow>(parent_, context_, true);
    if (vm.browser_tools.permissions_open && !permission_manager_)
      permission_manager_ =
          std::make_unique<BrowserToolWindow>(parent_, context_, false);
    if (vm.browser_tools.prompt && !permission_prompt_)
      permission_prompt_ =
          std::make_unique<PermissionPromptView>(parent_, context_);
    if (vm.changed.Has(Dirty::kBrowserTools) || vm.changed.Has(Dirty::kTheme) ||
        vm.changed.Has(Dirty::kWindow) || vm.changed.Has(Dirty::kText) ||
        vm.changed.Has(Dirty::kImages) || !rendered_) {
      if (task_manager_)
        task_manager_->Render(vm);
      if (permission_manager_)
        permission_manager_->Render(vm);
      if (permission_prompt_)
        permission_prompt_->Render(vm);
    }
    if ((vm.command_bar.open || vm.theme_editor.open) && menu_.IsOpen())
      menu_.Close();
    if (vm.changed.Has(Dirty::kLayout) || vm.changed.Has(Dirty::kWindow) ||
        vm.changed.Has(Dirty::kTheme) || vm.changed.Has(Dirty::kText))
      menu_.Update(client_, scale_, root_.RequestedTheme(),
                   root_.FlowDirection());
    controls::Grid::SetColumnSpan(
        toolbar_backdrop_.Root(),
        std::max(1,
                 static_cast<int>(toolbar_root_.ColumnDefinitions().Size())));
    if (layout_.sidebar_visible)
      sidebar_backdrop_.Render(
          vm.theme, client_, layout_.sidebar,
          brushes_.high_contrast() ||
              (vm.theme.transparent_chrome && !layout_.sidebar_overlays));
    if (layout_.toolbar_visible)
      toolbar_backdrop_.Render(
          vm.theme, client_, layout_.toolbar,
          brushes_.high_contrast() ||
              (vm.theme.transparent_chrome && !layout_.toolbar_overlays));
    mark(2);
    header_.Render(vm);
    mark(3);
    if (vm.changed.Has(Dirty::kTabs) || vm.changed.Has(Dirty::kActiveTab) ||
        vm.changed.Has(Dirty::kImages) || vm.changed.Has(Dirty::kTabPreview) ||
        vm.changed.Has(Dirty::kText) || vm.changed.Has(Dirty::kTheme) ||
        vm.changed.Has(Dirty::kLayout) || vm.changed.Has(Dirty::kWindow) ||
        !rendered_)
      tabs_.Render(vm);
    mark(4);
    if (vm.changed.Has(Dirty::kThemeEditor) || vm.changed.Has(Dirty::kTheme) ||
        vm.changed.Has(Dirty::kLayout) || vm.changed.Has(Dirty::kWindow) ||
        vm.changed.Has(Dirty::kText) || !rendered_)
      theme_editor_.Render(vm);
    if (vm.changed.Has(Dirty::kSpaces))
      for (const auto& space : vm.spaces)
        if (space.active && space.id != traced_space_) {
          traced_space_ = space.id;
          trace_.Record(workspace_gesture_active_
                            ? InputTraceEvent::kWorkspaceFromGesture
                            : InputTraceEvent::kWorkspaceFromHost,
                        parent_, focus_.generation(), space.id.value);
        }
    if (vm.changed.HasAny(Dirty::kSpaces | Dirty::kTheme) || !rendered_)
      sidebar_.Render(vm.spaces);
    if (vm.changed.HasAny(Dirty::kLibrary | Dirty::kImages | Dirty::kTheme |
                          Dirty::kText | Dirty::kTabs | Dirty::kActiveTab |
                          Dirty::kSpaces | Dirty::kLayout | Dirty::kWindow |
                          Dirty::kCommandBar) ||
        !rendered_) {
      sidebar_.RenderLibrary(vm);
      library_.Render(vm, root_.RequestedTheme());
    }
    if (vm.changed.Has(Dirty::kActions) || vm.changed.Has(Dirty::kImages))
      anchors_dirty_ = true;
    if (vm.workspace_editor.open && !workspace_editor_)
      workspace_editor_ = std::make_unique<WorkspaceEditor>(
          parent_, context_,
          [this](bool blocked) { command_bar_.SetFocusBlocked(blocked); });
    if (workspace_editor_ &&
        (vm.changed.Has(Dirty::kWorkspaceEditor) ||
         vm.changed.Has(Dirty::kText) || vm.changed.Has(Dirty::kTheme) ||
         vm.changed.Has(Dirty::kLayout) || vm.changed.Has(Dirty::kWindow) ||
         !rendered_))
      workspace_editor_->Render(vm.workspace_editor, vm.text, client_, scale_,
                                root_.RequestedTheme(),
                                vm.changed.Has(Dirty::kText));
    if (boost_open && !boost_editor_)
      boost_editor_ = std::make_unique<BoostEditor>(parent_, context_);
    if (boost_editor_ && vm.boost_editor &&
        (vm.changed.Has(Dirty::kWorkspaceEditor) ||
         vm.changed.Has(Dirty::kTheme) || vm.changed.Has(Dirty::kLayout) ||
         vm.changed.Has(Dirty::kWindow) || !rendered_))
      boost_editor_->Render(*vm.boost_editor, client_, scale_,
                            root_.RequestedTheme(), vm.theme.accent);
    mark(5);
    if (vm.changed.Has(Dirty::kCommandBar) || vm.changed.Has(Dirty::kLayout) ||
        vm.changed.Has(Dirty::kWindow) || vm.changed.Has(Dirty::kImages) ||
        vm.changed.Has(Dirty::kTheme) || vm.changed.Has(Dirty::kText) ||
        !rendered_) {
      command_bar_.Render(vm.command_bar, client_, scale_,
                          root_.RequestedTheme(), vm.images);
    }
    if (vm.onboarding.open && !onboarding_)
      onboarding_ = std::make_unique<OnboardingView>(parent_, context_, frame_);
    if (!vm.onboarding.open && onboarding_) {
      onboarding_.reset();
      anchors_dirty_ = true;
    }
    if (onboarding_ &&
        (vm.changed.HasAny(Dirty::kOnboarding | Dirty::kTheme | Dirty::kText |
                           Dirty::kLayout | Dirty::kWindow | Dirty::kImages |
                           Dirty::kPrivacyFeatures) ||
         !rendered_)) {
      onboarding_->Render(vm);
      anchors_dirty_ = true;
    }
    if (library_.IsOpen())
      library_.Raise();
    if (command_bar_.IsOpen())
      command_bar_.Raise();
    else if (theme_editor_.IsOpen())
      theme_editor_.Raise();
    else
      menu_.Raise();
    if (onboarding_)
      onboarding_->Raise();
    if (vm.changed.Has(Dirty::kLayout) || vm.changed.Has(Dirty::kWindow) ||
        command_was_open != command_bar_.IsOpen() || !rendered_)
      QueueHoverRefresh();
    const auto focus_generation = focus_.generation();
    SyncFocusSurfaces(false);
    if (focus_generation != focus_.generation())
      QueueFocusReturn();
    rendered_ = true;
    mark(6);
    if (profile) {
      const auto elapsed_ms = [](auto begin, auto end) {
        return std::chrono::duration<double, std::milli>(end - begin).count();
      };
      platform::WriteDefaultLog(
          LogLevel::kInfo,
          std::format("first theme: brushes {:.3f}, captions {:.3f}, XAML "
                      "theme {:.3f}, text {:.3f} ms",
                      elapsed_ms(stages[0], theme_stages[0]),
                      elapsed_ms(theme_stages[0], theme_stages[1]),
                      elapsed_ms(theme_stages[1], theme_stages[2]),
                      elapsed_ms(theme_stages[2], stages[1])));
      const auto ms = [&](std::size_t end) {
        return std::chrono::duration<double, std::milli>(stages[end] -
                                                         stages[end - 1])
            .count();
      };
      platform::WriteDefaultLog(
          LogLevel::kInfo,
          std::format("first native render: theme/text {:.3f}, "
                      "islands/backdrop {:.3f}, header {:.3f}, tabs {:.3f}, "
                      "sidebar/editor {:.3f}, search {:.3f} ms",
                      ms(1), ms(2), ms(3), ms(4), ms(5), ms(6)));
    }
  }

  void PostTask(Task task) override {
    const std::weak_ptr<int> weak = lifetime_;
    root_.DispatcherQueue().TryEnqueue([weak,
                                        task = std::move(task)]() mutable {
      if (weak.expired())
        return;
      try {
        task();
      } catch (const winrt::hresult_error& error) {
        platform::WriteDefaultLog(
            LogLevel::kError,
            std::format("Native UI task failed (0x{:08X}): {}",
                        static_cast<std::uint32_t>(error.code().value),
                        winrt::to_string(error.message())));
      } catch (...) {
        platform::WriteDefaultLog(LogLevel::kError, "Native UI task failed");
      }
    });
  }

  void ShowNotification(std::string_view text) override {
    if (client_.empty())
      return;
    const auto& t = design::kTokens.notification;
    notification_.Ensure();
    const auto& root = notification_.Root();
    if (!notification_backdrop_) {
      notification_backdrop_ = std::make_unique<ThemeBackdrop>();
      notification_surface_ = controls::Border();
      notification_label_ = Label(L"", t.font_size);
      notification_label_.FontWeight(kSemiBold);
      notification_label_.VerticalAlignment(xaml::VerticalAlignment::Center);
      notification_label_.TextWrapping(xaml::TextWrapping::Wrap);
      controls::Border inset;
      inset.Padding({t.padding + t.label_margin, t.padding,
                     t.padding + t.label_margin, t.padding});
      inset.Child(notification_label_);
      controls::Grid layers;
      layers.Children().Append(notification_backdrop_->Root());
      layers.Children().Append(inset);
      notification_surface_.Child(layers);
      root.Children().Append(notification_surface_);
    }
    const auto& surface = notification_surface_;
    const auto& label = notification_label_;
    if (brushes_.high_contrast())
      surface.Background(brushes_.menu_background);
    else
      surface.Background(brushes_.transparent);
    surface.CornerRadius(UniformCorners(t.radius));
    surface.MinHeight(t.height);
    surface.BorderBrush(brushes_.menu_border);
    surface.BorderThickness(
        UniformThickness(brushes_.high_contrast() ? t.contrast_border : 0));
    label.Text(winrt::to_hstring(text));
    label.Foreground(brushes_.text);
    const auto width_limit = std::max(0.0f, client_.width - 2 * t.margin);
    surface.MaxWidth(width_limit);
    surface.Measure({width_limit, client_.height});
    const auto desired = surface.DesiredSize();
    const float width = std::min(width_limit, desired.Width);
    const Rect frame = PlaceNotification(client_, {width, desired.Height},
                                         layout_.sidebar_on_right);
    const auto color = [](const auto& stop) {
      const auto value = stop.Color();
      return Color::FromArgb(value.A, value.R, value.G, value.B);
    };
    const auto stops = brushes_.toolbar_gradient.GradientStops();
    notification_.Show(frame, scale_, root_.RequestedTheme());
    notification_backdrop_->Render(
        MakeLinearThemeGradient(color(stops.GetAt(0)), color(stops.GetAt(1)),
                                client_),
        0, client_, frame, brushes_.high_contrast());
    notification_fade_.Play(root, motion_, brushes_.high_contrast(), t.fade_ms);
    if (auto peer = xaml::Automation::Peers::FrameworkElementAutomationPeer::
            CreatePeerForElement(label))
      peer.RaiseNotificationEvent(
          xaml::Automation::Peers::AutomationNotificationKind::ActionCompleted,
          xaml::Automation::Peers::AutomationNotificationProcessing::
              ImportantMostRecent,
          winrt::to_hstring(text), L"browser-notification");
    const auto serial = ++notification_serial_;
    const std::weak_ptr<int> weak = lifetime_;
    PostDelayedTask(
        [this, weak, serial] {
          if (!weak.expired() && notification_serial_ == serial) {
            notification_fade_.Stop();
            notification_.Hide();
          }
        },
        std::chrono::milliseconds(static_cast<int>(t.duration_ms)));
  }

  void PostDelayedTask(Task task, std::chrono::milliseconds delay) override {
    if (delay <= std::chrono::milliseconds::zero()) {
      PostTask(std::move(task));
      return;
    }
    delayed_tasks_.Post(root_.DispatcherQueue(), std::move(task), delay);
  }

  // The per-thread hook has already processed removed messages. Keeping this
  // legacy slot as a no-op prevents old host pumps from translating twice.
  bool PreTranslateMessage(const void*) override { return false; }
  bool OwnsWindowFrame() const noexcept override { return true; }

  bool ProcessNativeMessage(const MSG& native) {
    if ((native.message == WM_KEYDOWN || native.message == WM_SYSKEYDOWN) &&
        native.wParam == VK_ESCAPE && tabs_.IsDragging()) {
      // Escape ends a sidebar drag. Consume it here, before the key hand-back
      // below would retarget it to the page.
      tabs_.CancelDrag();
      return true;
    }
    if (native.message == WM_IME_STARTCOMPOSITION)
      composing_ = true;
    if (native.message == WM_IME_ENDCOMPOSITION) {
      composing_ = false;
      QueueFocusReturn();
    }
    if (native.message == WM_KEYDOWN && native.wParam == VK_F12 &&
        GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) < 0)
      trace_.Dump();
    if (IsPointerPress(native.message))
      OnPointerPress(native.hwnd, native.pt);
    else if (IsPointerRelease(native.message) ||
             native.message == WM_CAPTURECHANGED ||
             native.message == WM_CANCELMODE) {
      pointer_down_ = false;
      QueueFocusReturn();
    } else if ((native.message == WM_MOUSEMOVE ||
                native.message == WM_NCMOUSEMOVE ||
                native.message == WM_POINTERUPDATE) &&
               !NativeInputTarget::Contains(native.hwnd) &&
               NativeInputTarget::Contains(GetFocus()))
      // XAML can miss the exit into the native page. Return keyboard focus
      // on that crossing, after the pointer has finished using chrome.
      QueueFocusReturn();
    else if ((native.message == WM_KEYDOWN ||
              native.message == WM_SYSKEYDOWN) &&
             NativeInputTarget::Contains(native.hwnd) && !menu_.IsOpen() &&
             FocusWithinSteadyRoot()) {
      // Pointer focus must not strand page typing when the cursor stays over
      // chrome. Tab/F6 deliberately enter keyboard navigation instead.
      if (native.wParam == VK_TAB || native.wParam == VK_F6) {
        focus_.Enter(FocusOwner::kKeyboardChrome);
        return_focus_ = nullptr;
      } else {
        if (focus_.owner() == FocusOwner::kPointerChrome ||
            focus_.owner() == FocusOwner::kPage)
          focus_.Enter(FocusOwner::kPage);
        ReturnFocus(true);
        if (HostFocus(GetFocus()))
          // The input hook retargets this original queued key to the host.
          return false;
        // Capture, a held button or an open surface can delay the hand-back.
        // Keep the pointer loan and its destination until that blocker ends.
        QueueFocusReturn();
      }
    }
    if (tab_previews_.IsOpen() &&
        (native.message == WM_MOUSEMOVE || native.message == WM_NCMOUSEMOVE ||
         native.message == WM_MOUSELEAVE ||
         native.message == WM_NCMOUSELEAVE)) {
      // XAML can miss PointerExited when the pointer enters the native page.
      // The existing message hook observes that crossing without idle polling.
      POINT cursor{};
      if (GetCursorPos(&cursor) &&
          (!input_.Owns(WindowFromPoint(cursor)) ||
           (ScreenToClient(parent_, &cursor) &&
            !tab_previews_.ContainsAnchor(
                {static_cast<float>(cursor.x) / scale_,
                 static_cast<float>(cursor.y) / scale_}))))
        tabs_.DismissHover();
    }
    if (trace_menus_ &&
        (native.message == WM_LBUTTONDOWN || native.message == WM_RBUTTONDOWN ||
         native.message == WM_NCLBUTTONDOWN ||
         native.message == WM_POINTERDOWN)) {
      wchar_t window_class[128]{};
      GetClassNameW(native.hwnd, window_class, 128);
      const auto hit = SendMessageW(parent_, WM_NCHITTEST, 0,
                                    MAKELPARAM(native.pt.x, native.pt.y));
      Log(LogLevel::kWarning,
          "menu trace: input hwnd {} class {} message {} screen {},{} hit {} "
          "open {}",
          reinterpret_cast<std::uintptr_t>(native.hwnd),
          winrt::to_string(std::wstring_view(window_class)), native.message,
          native.pt.x, native.pt.y, hit, menu_.IsOpen());
    }
    if (library_.IsOpen()) {
      const bool escape =
          (native.message == WM_KEYDOWN || native.message == WM_SYSKEYDOWN) &&
          native.wParam == VK_ESCAPE;
      if (!menu_.IsOpen() && escape) {
        library_.Close();
        delegate_.OnLibraryDismissed();
        SyncFocusSurfaces();
        return true;
      }
      if (IsPointerPress(native.message) &&
          GetAncestor(native.hwnd, GA_ROOT) == parent_) {
        auto point = native.pt;
        if (ScreenToClient(parent_, &point)) {
          const Point cursor{static_cast<float>(point.x) / scale_,
                             static_cast<float>(point.y) / scale_};
          if (!library_.Contains(cursor) && !menu_.Contains(cursor)) {
            // Dismiss before dispatch, without consuming the outside click.
            library_.Close();
            delegate_.OnLibraryDismissed();
            SyncFocusSurfaces();
          }
        }
      }
    }
    if (menu_.IsOpen()) {
      if ((native.message == WM_KEYDOWN || native.message == WM_SYSKEYDOWN) &&
          native.wParam == VK_ESCAPE) {
        menu_.Close();
        return true;
      }
      const bool pointer_down = native.message == WM_LBUTTONDOWN ||
                                native.message == WM_RBUTTONDOWN ||
                                native.message == WM_MBUTTONDOWN ||
                                native.message == WM_XBUTTONDOWN ||
                                native.message == WM_NCLBUTTONDOWN ||
                                native.message == WM_NCRBUTTONDOWN;
      // Native popup windows receive their own clicks. The browser's child
      // HWNDs need explicit light dismissal across the XAML island boundary.
      if (pointer_down && GetAncestor(native.hwnd, GA_ROOT) == parent_) {
        auto point = native.pt;
        if (ScreenToClient(parent_, &point) &&
            !menu_.Contains({static_cast<float>(point.x) / scale_,
                             static_cast<float>(point.y) / scale_}))
          menu_.Close();
      }
    }
    const bool revealable =
        !layout_.caption_reveal.empty() ||
        (!layout_.sidebar.empty() &&
         (!layout_.sidebar_visible || layout_.sidebar_overlays)) ||
        (!layout_.toolbar.empty() &&
         (!layout_.toolbar_visible || layout_.toolbar_overlays));
    if (IsPointerPress(native.message) || IsPointerRelease(native.message) ||
        native.message == WM_CAPTURECHANGED || native.message == WM_CANCELMODE)
      QueueHoverRefresh();
    if ((revealable || sidebar_.HasActiveHoverPopup()) &&
        (native.message == WM_MOUSEMOVE || native.message == WM_NCMOUSEMOVE ||
         native.message == WM_MOUSELEAVE || native.message == WM_NCMOUSELEAVE))
      QueueHoverRefresh();
    return windows::PreTranslateMessage(&native);
  }

  void ShowMenu(std::uint32_t request,
                const MenuTree& tree,
                Rect anchor,
                const ImageStore& images,
                MenuKind kind) override {
    // Native flyout closure may restore XAML focus. Finish it before the new
    // menu acquires focus, rather than hiding a hover preview on a later
    // render.
    sidebar_.DismissHoverPopups();
    // A menu over the page, as from a right click there, hands keys back to
    // the page when it closes.
    if (const HWND focus = HostFocus(GetFocus()))
      return_focus_ = focus;
    shown_menu_request_ = request;
    focus_.SetSurface(FocusSurface::kMenu, true);
    tabs_.DismissHover();
    trace_.Record(InputTraceEvent::kSurface, GetFocus(), focus_.generation(),
                  request);
    if (tree.panel || layout_.sidebar_visible || layout_.toolbar_visible) {
      menu_.Show(request, tree, anchor, images, kind, layout_, root_,
                 toolbar_root_, client_, scale_);
      return;
    }
    // A transient popup owner keeps host-surface menus available when all
    // steady chrome is hidden. It exists only while the popup needs it.
    if (!menu_source_) {
      menu_root_ = Grid();
      brushes_.Install(menu_root_);
      menu_.InstallStyle(menu_root_);
      menu_source_ = hosting::DesktopWindowXamlSource();
      const auto island_parent = input_targets_[2].IslandParent(parent_);
      winrt::check_bool(island_parent.has_value());
      menu_source_.Initialize(
          winrt::Microsoft::UI::GetWindowIdFromWindow(*island_parent));
      menu_source_.ShouldConstrainPopupsToWorkArea(true);
      menu_source_.Content(menu_root_);
      winrt::check_bool(
          input_targets_[2]
              .Initialize(winrt::Microsoft::UI::GetWindowFromWindowId(
                              menu_source_.SiteBridge().WindowId()),
                          menu_root_)
              .has_value());
    }
    menu_root_.RequestedTheme(root_.RequestedTheme());
    menu_root_.FlowDirection(root_.FlowDirection());
    island_hide_[2].BeforeShow();
    menu_source_.SiteBridge().MoveAndResize(
        {static_cast<std::int32_t>(std::lround(client_.x * scale_)),
         static_cast<std::int32_t>(std::lround(client_.y * scale_)), 1, 1});
    ShellLayout popup_layout;
    popup_layout.sidebar = {client_.x, client_.y, 1 / scale_, 1 / scale_};
    popup_layout.sidebar_visible = true;
    if (!menu_root_.IsLoaded()) {
      const std::weak_ptr<int> weak = lifetime_;
      menu_loaded_ = menu_root_.Loaded(
          winrt::auto_revoke,
          [this, weak, request, owned_tree = tree, anchor, kind,
           images = &images, popup_layout](auto&&, auto&&) {
            if (weak.expired())
              return;
            menu_loaded_.revoke();
            menu_.Show(request, owned_tree, anchor, *images, kind, popup_layout,
                       menu_root_, menu_root_, client_, scale_);
          });
      menu_source_.SiteBridge().Show();
      return;
    }
    menu_source_.SiteBridge().Show();
    menu_.Show(request, tree, anchor, images, kind, popup_layout, menu_root_,
               menu_root_, client_, scale_);
  }

  void FocusNative(Anchor target) override {
    // The host moves keyboard focus into chrome on purpose; keep it there.
    focus_.Enter(FocusOwner::kKeyboardChrome);
    return_focus_ = nullptr;
    pending_focus_ = target;
    TryNativeFocus();
  }

  void TryNativeFocus() {
    if (!pending_focus_ ||
        (!layout_.sidebar_visible && !layout_.toolbar_visible))
      return;
    const auto target = *pending_focus_;
    pending_focus_.reset();
    bool focused = header_.FocusNative(target);
    if (!focused && target.kind == AnchorKind::kTab)
      focused = tabs_.FocusNative(TabId{target.id});
    if (!focused && target.kind == AnchorKind::kElement) {
      const auto element = static_cast<Element>(target.id);
      focused = element == Element::kSidebar
                    ? tabs_.Root().Focus(xaml::FocusState::Programmatic)
                    : sidebar_.FocusNative(element) ||
                          top_chrome_.FocusNative(element);
    }
    if (!focused)
      pending_focus_ = target;
  }

  void DismissMenu(std::uint32_t request) noexcept override try {
    if (shown_menu_request_ != request)
      return;
    shown_menu_request_ = 0;
    menu_loaded_.revoke();
    menu_.Close();
    HideMenuSource();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Menu popup owner could not hide");
  }

  Result<void> RegisterGlassBackdrop(
      const GlassBackdropResource& resource) override {
    return glass_.Register(resource);
  }
  Result<void> SubmitGlassBackdrop(const GlassBackdropFrame& frame) override {
    return glass_.Submit(frame);
  }
  Result<void> DropGlassBackdrop(std::uint64_t id) override {
    return glass_.Drop(id);
  }

 private:
  bool HasOpenFlyouts() const {
    for (const auto& target : NativeInputTarget::Roots())
      if (input_.Owns(target.window))
        if (const auto root = target.XamlRoot())
          for (const auto& popup :
               media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root))
            // Tooltips are passive and do not own keyboard input.
            if (!popup.Child().try_as<controls::ToolTip>())
              return true;
    return false;
  }

  bool BrowserToolsOwnInput() const noexcept {
    return (permission_prompt_ && permission_prompt_->OwnsInput()) ||
           (permission_manager_ && permission_manager_->OwnsInput()) ||
           (task_manager_ && task_manager_->OwnsInput());
  }
  void SyncFocusSurfaces(bool inspect_popups = true) {
    focus_.SetSurface(FocusSurface::kMenu, menu_.IsOpen());
    focus_.SetSurface(FocusSurface::kSearch, command_bar_.IsOpen());
    focus_.SetSurface(FocusSurface::kEditor,
                      theme_editor_.IsOpen() ||
                          (boost_editor_ && boost_editor_->OwnsInput()));
    focus_.SetSurface(FocusSurface::kLibrary, library_.OwnsInput());
    focus_.SetSurface(FocusSurface::kModal,
                      modal_open_ || BrowserToolsOwnInput() ||
                          (boost_editor_ && boost_editor_->OwnsInput()));
    if (inspect_popups)
      focus_.SetSurface(FocusSurface::kFlyout, HasOpenFlyouts());
  }

  bool HoverPopupAllowed(const xaml::FrameworkElement& preview,
                         bool active) const {
    if (modal_open_ || BrowserToolsOwnInput() || url_drag_active_ ||
        (boost_editor_ && boost_editor_->OwnsInput()) || composing_ ||
        menu_.IsOpen() || command_bar_.IsOpen() || theme_editor_.IsOpen() ||
        library_.OwnsInput() ||
        GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) !=
            GetAncestor(parent_, GA_ROOTOWNER))
      return false;
    bool owns_popup = false;
    for (const auto& target : NativeInputTarget::Roots())
      if (input_.Owns(target.window))
        if (const auto root = target.XamlRoot())
          for (const auto& popup :
               media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root)) {
            // The native popup may still own input during its closing phase.
            // GetOpenPopupsForXamlRoot retains it until that phase finishes.
            if (popup.Child().try_as<controls::ToolTip>())
              continue;
            bool own = WithinRoot(preview, popup.Child());
            // A flyout's logical content is available before its presenter has
            // completed template realization and attached the visual parents.
            for (auto child = popup.Child(); child && !own;) {
              own = child == preview;
              const auto content = child.try_as<controls::ContentControl>();
              child = content ? content.Content().try_as<xaml::UIElement>()
                              : xaml::UIElement{nullptr};
            }
            if (!own)
              return false;
            owns_popup = true;
          }
    return active || owns_popup ||
           (!GetCapture() && GetKeyState(VK_LBUTTON) >= 0 &&
            GetKeyState(VK_RBUTTON) >= 0 && GetKeyState(VK_MBUTTON) >= 0 &&
            GetKeyState(VK_XBUTTON1) >= 0 && GetKeyState(VK_XBUTTON2) >= 0);
  }

  bool WorkspaceGesturesAllowed() const {
    return !modal_open_ && !BrowserToolsOwnInput() && !url_drag_active_ &&
           !(boost_editor_ && boost_editor_->OwnsInput()) && !composing_ &&
           layout_.sidebar_visible && !menu_.IsOpen() &&
           !command_bar_.IsOpen() && !theme_editor_.IsOpen() &&
           !library_.OwnsInput() && !HasOpenFlyouts() &&
           GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) ==
               GetAncestor(parent_, GA_ROOTOWNER);
  }

  void OnActivationChanged(bool active) {
    focus_.SetActive(active);
    trace_.Record(InputTraceEvent::kActivation, GetFocus(), focus_.generation(),
                  active);
    if (!active) {
      url_drag_active_ = false;
      pending_focus_.reset();
      composing_ = false;
      pointer_down_ = false;
      tabs_.ResetPointerState();
      return_focus_ = nullptr;
    }
  }

  void WirePointerSource(bool toolbar, RootEvents& events) {
    if (events.pointer)
      return;
    const auto& root = toolbar ? toolbar_root_ : root_;
    if (!root.XamlRoot())
      return;
    const auto island = root.XamlRoot().ContentIsland();
    if (!island)
      return;
    events.pointer =
        winrt::Microsoft::UI::Input::InputPointerSource::GetForIsland(island);
    // A root's Loaded can precede the island's input-source connection.
    if (!events.pointer)
      return;
    const HWND window = WindowOf(toolbar ? toolbar_source_ : source_);
    events.pressed =
        events.pointer.PointerPressed([this, window](auto&&, auto&&) {
          OnPointerPress(window);
          pointer_down_ = true;
        });
    events.released =
        events.pointer.PointerReleased([this, window](auto&&, auto&&) {
          pointer_down_ = false;
          trace_.Record(InputTraceEvent::kRelease, window, focus_.generation());
          QueueFocusReturn();
        });
    events.capture_lost =
        events.pointer.PointerCaptureLost([this, window](auto&&, auto&&) {
          pointer_down_ = false;
          trace_.Record(InputTraceEvent::kCaptureLost, window,
                        focus_.generation());
          tabs_.CancelPointerInteraction();
          QueueHoverRefresh();
          QueueFocusReturn();
        });
    events.routed_away =
        events.pointer.PointerRoutedAway([this, window](auto&&, auto&&) {
          trace_.Record(InputTraceEvent::kPointerRoute, window,
                        focus_.generation(), 0);
          pointer_down_ = false;
          tabs_.CancelPointerInteraction();
          QueueHoverRefresh();
          QueueFocusReturn();
        });
    events.routed_released =
        events.pointer.PointerRoutedReleased([this, window](auto&&, auto&&) {
          trace_.Record(InputTraceEvent::kPointerRoute, window,
                        focus_.generation(), 1);
          pointer_down_ = false;
          tabs_.CancelPointerInteraction();
          QueueHoverRefresh();
          QueueFocusReturn();
        });
  }

  void WireRoot(bool toolbar, RootEvents& events) {
    const auto& root = toolbar ? toolbar_root_ : root_;
    const auto& source = toolbar ? toolbar_source_ : source_;
    events.native_focus = source.GotFocus(
        winrt::auto_revoke, [this](auto&&, auto&&) { QueueFocusReturn(); });
    events.depart =
        source.TakeFocusRequested(winrt::auto_revoke, [this](auto&&, auto&&) {
          // Let Chromium retain its page traversal/edit-model policy.
          trace_.Record(InputTraceEvent::kNavigationExit, GetFocus(),
                        focus_.generation());
          focus_.Enter(FocusOwner::kPage);
          return_focus_ = parent_;
          delegate_.OnFocusPageRequested();
        });
    events.layout =
        root.LayoutUpdated(winrt::auto_revoke, [this](auto&&, auto&&) {
          glass_.Layout();
          PublishAnchors();
          TryNativeFocus();
          // A popup can close without changing the focused XAML element.
          // Revisit an explicit page request on that layout notification.
          if (focus_.owner() == FocusOwner::kPage && return_focus_ &&
              NativeInputTarget::Contains(GetFocus()))
            QueueFocusReturn();
        });
    // XAML restores focus here when a local flyout, such as a folder menu,
    // closes.
    events.focused =
        root.GotFocus(winrt::auto_revoke, [this, toolbar](auto&&, auto&&) {
          const auto xaml_root = (toolbar ? toolbar_root_ : root_).XamlRoot();
          const auto focused =
              xaml_root
                  ? xaml::Input::FocusManager::GetFocusedElement(xaml_root)
                  : nullptr;
          if (AcceptsText(focused))
            focus_.Enter(FocusOwner::kText);
          QueueFocusReturn();
        });
    events.key = root.KeyDown(
        winrt::auto_revoke,
        [this](auto&&, const xaml::Input::KeyRoutedEventArgs& args) {
          using Keys = winrt::Windows::UI::Core::CoreVirtualKeyStates;
          const auto down = [](VirtualKey key) {
            return (winrt::Microsoft::UI::Input::InputKeyboardSource::
                        GetKeyStateForCurrentThread(key) &
                    Keys::Down) != Keys::None;
          };
          const bool primary = down(VirtualKey::Control) &&
                               !down(VirtualKey::Shift) &&
                               !down(VirtualKey::Menu);
          if (primary &&
              (args.Key() == VirtualKey::L || args.Key() == VirtualKey::T) &&
              input_.ForwardUnhandledKey(
                  static_cast<std::uint32_t>(args.Key()))) {
            args.Handled(true);
            return;
          }
          if (primary && args.Key() == VirtualKey::L)
            delegate_.OnCommandBarRequested();
          else if (primary && args.Key() == VirtualKey::T)
            delegate_.OnNewTab();
          else if (args.Key() == VirtualKey::Escape)
            delegate_.OnFocusPageRequested();
          else
            return;
          args.Handled(true);
        });
    events.fallback = root.ProcessKeyboardAccelerators(
        winrt::auto_revoke,
        [this](auto&&,
               const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
          if (!args.Handled() && input_.ForwardUnhandledKey(
                                     static_cast<std::uint32_t>(args.Key())))
            args.Handled(true);
        });
    events.entered = root.PointerEntered(
        winrt::auto_revoke, [this](auto&&, auto&&) { QueueHoverRefresh(); });
    events.exited =
        root.PointerExited(winrt::auto_revoke, [this](auto&&, auto&&) {
          QueueHoverRefresh();
          QueueFocusReturn();
        });
    events.loaded =
        root.Loaded(winrt::auto_revoke, [this, toolbar](auto&&, auto&&) {
          AttachFrameIslands();
          WirePointerSource(toolbar, events_[toolbar ? 1 : 0]);
          const std::weak_ptr<int> weak = lifetime_;
          PostTask([this, toolbar, weak] {
            if (!weak.expired())
              WirePointerSource(toolbar, events_[toolbar ? 1 : 0]);
          });
        });
    if (root.IsLoaded())
      WirePointerSource(toolbar, events);
    events.context = root.RightTapped(
        winrt::auto_revoke,
        [this, toolbar](auto&&,
                        const xaml::Input::RightTappedRoutedEventArgs& args) {
          const auto point = args.GetPosition(toolbar ? toolbar_root_ : root_);
          const auto region = toolbar ? layout_.toolbar : layout_.sidebar;
          delegate_.OnMenuRequested(
              MenuKind::kSidebar, 0,
              {point.X + region.x, point.Y + region.y, 0, 0});
          args.Handled(true);
        });
  }

  void ReleaseReveals() {
    HideMenuSource();
    QueueHoverRefresh();
  }

  void HideMenuSource() {
    if (!menu_source_)
      return;
    ReleaseFocus(WindowOf(menu_source_), return_focus_, parent_);
    const auto bridge = menu_source_.SiteBridge();
    island_hide_[2].Hide(WindowOf(menu_source_), menu_root_.DispatcherQueue(),
                         [bridge] { bridge.Hide(); });
  }

  // Steady chrome borrows Win32 keyboard focus while the pointer uses it and
  // while a menu is open over the host. Keep it until the pointer leaves, so
  // focus loss cannot reset hover or cancel a control's pending click. Win32
  // never hands focus back from a child window; return it on that crossing,
  // unless the user is typing in a chrome field or using a flyout. A press on
  // host content takes it back at once, before the host handles the press.
  void OnPointerPress(HWND target, std::optional<POINT> point = std::nullopt) {
    if (settings_)
      settings_->OnPointerPress(target);
    const HWND focus = GetFocus();
    trace_.Record(InputTraceEvent::kPress, target, focus_.generation(),
                  reinterpret_cast<std::uintptr_t>(focus));
    pending_focus_.reset();
    const bool registered = NativeInputTarget::Contains(target);
    const bool chrome = registered || PointerWithinChrome(point);
    trace_.Record(InputTraceEvent::kPointerTarget, target, focus_.generation(),
                  (registered ? 1u : 0u) | (chrome ? 2u : 0u) |
                      (menu_.IsOpen() ? 4u : 0u));
    if (chrome) {
      focus_.Enter(FocusOwner::kPointerChrome);
      if (const HWND host = HostFocus(focus))
        return_focus_ = host;
      else if (!return_focus_)
        return_focus_ = parent_;
    } else if (target == parent_ || IsChild(parent_, target)) {
      focus_.Enter(FocusOwner::kPage);
      return_focus_ = HostFocus(target) ? target : parent_;
      if (focus && NativeInputTarget::Contains(focus) && input_.Owns(focus))
        SetFocus(parent_);
    }
  }

  // The host window, or its own child, that holds keyboard focus.
  HWND HostFocus(HWND focus) const noexcept {
    return focus && (focus == parent_ || IsChild(parent_, focus)) &&
                   !NativeInputTarget::Contains(focus)
               ? focus
               : nullptr;
  }

  bool PointerWithinChrome(
      std::optional<POINT> screen_point = std::nullopt) const {
    POINT cursor{};
    if (screen_point)
      cursor = *screen_point;
    else if (!GetCursorPos(&cursor))
      return false;
    const HWND under = WindowFromPoint(cursor);
    if (!input_.Owns(under))
      return false;
    if (NativeInputTarget::Contains(under))
      return true;
    if (!ScreenToClient(parent_, &cursor))
      return false;
    const Point point{static_cast<float>(cursor.x) / scale_,
                      static_cast<float>(cursor.y) / scale_};
    // Desktop popup input HWNDs can be owned by the host rather than by the
    // registered island. Use live popup bounds before treating their press as
    // a page click; stealing Win32 focus here cancels WinUI's pending Click.
    if (menu_.Contains(point))
      return true;
    for (const auto& target : NativeInputTarget::Roots())
      if (input_.Owns(target.window))
        if (const auto root = target.XamlRoot()) {
          for (const auto& popup :
               media::VisualTreeHelper::GetOpenPopupsForXamlRoot(root)) {
            const auto child = popup.Child().try_as<xaml::FrameworkElement>();
            if (!child)
              continue;
            if (Bounds(child).Contains(point))
              return true;
          }
        }
    return (layout_.sidebar_visible && layout_.sidebar.Contains(point)) ||
           (layout_.toolbar_visible && layout_.toolbar.Contains(point));
  }

  bool FocusWithinSteadyRoot() const {
    const HWND focus = GetFocus();
    std::size_t index = 0;
    for (const auto& [source, root] :
         {std::pair{source_, root_}, std::pair{toolbar_source_, toolbar_root_},
          std::pair{menu_source_, menu_root_}}) {
      const HWND window = WindowOf(source);
      const bool owns = input_targets_[index++].Owns(focus);
      if (!window || !owns)
        continue;
      const auto xaml_root = root ? root.XamlRoot() : nullptr;
      const auto element =
          xaml_root ? xaml::Input::FocusManager::GetFocusedElement(xaml_root)
                    : nullptr;
      return WithinRoot(element, root);
    }
    return false;
  }

  bool FocusWithinVisibleIsland(HWND focus) const {
    const std::array sources{source_, toolbar_source_, menu_source_};
    for (std::size_t index = 0; index < sources.size(); ++index)
      if (input_targets_[index].Owns(focus) &&
          !island_hide_[index].IsParked() &&
          IsWindowVisible(WindowOf(sources[index])))
        return true;
    return false;
  }

  void QueueFocusReturn() {
    if (!return_focus_ || focus_return_queued_ || url_drag_active_)
      return;
    SyncFocusSurfaces();
    focus_return_queued_ = true;
    const auto generation = focus_.generation();
    const std::weak_ptr<int> weak = lifetime_;
    // Low priority runs after XAML restores focus from a closing flyout.
    if (!root_.DispatcherQueue().TryEnqueue(
            winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [this, weak, generation] {
              if (weak.expired())
                return;
              focus_return_queued_ = false;
              try {
                SyncFocusSurfaces();
                if (generation != focus_.generation()) {
                  QueueFocusReturn();
                  return;
                }
                if (focus_.CanReturn(generation))
                  ReturnFocus();
              } catch (...) {
                platform::WriteDefaultLog(LogLevel::kWarning,
                                          "Keyboard focus could not return");
              }
            }))
      focus_return_queued_ = false;
  }

  void ReturnFocus(bool keyboard = false) {
    HWND target = return_focus_;
    if (target && (!HostFocus(target) || !IsWindowVisible(target)))
      target = return_focus_ = parent_;
    const HWND focus = GetFocus();
    // A drag or held press settles on its release.
    SyncFocusSurfaces();
    if (url_drag_active_ || !focus_.CanReturn(focus_.generation()) ||
        composing_ || pointer_down_ || !target || !focus ||
        !HostFocus(target) || !IsWindow(target) || !IsWindowVisible(target) ||
        rendering_ || command_bar_.IsOpen() || theme_editor_.IsOpen() ||
        menu_.IsOpen() || GetCapture() || GetKeyState(VK_LBUTTON) < 0 ||
        GetKeyState(VK_RBUTTON) < 0 || GetKeyState(VK_MBUTTON) < 0 ||
        GetKeyState(VK_XBUTTON1) < 0 || GetKeyState(VK_XBUTTON2) < 0)
      return;
    if (focus == target)
      return;
    // Hidden input-site HWNDs still belong to their visible island. Check the
    // pointer before the hidden-window fallback too, so it cannot bypass the
    // pointer loan and reset an in-progress chrome interaction.
    if (!keyboard && focus_.owner() == FocusOwner::kPointerChrome &&
        FocusWithinVisibleIsland(focus) && PointerWithinChrome())
      return;
    if (!IsWindowVisible(focus) && NativeInputTarget::Contains(focus) &&
        input_.Owns(focus)) {
      SetFocus(target);
      return;
    }
    std::size_t index = 0;
    for (const auto& [source, root] :
         {std::pair{source_, root_}, std::pair{toolbar_source_, toolbar_root_},
          std::pair{menu_source_, menu_root_}}) {
      const HWND window = WindowOf(source);
      const bool owns = input_targets_[index++].Owns(focus);
      if (!window || !owns)
        continue;
      const auto xaml_root = root ? root.XamlRoot() : nullptr;
      const auto element =
          xaml_root ? xaml::Input::FocusManager::GetFocusedElement(xaml_root)
                    : nullptr;
      if (WithinRoot(element, root)) {
        if (AcceptsText(element)) {
          focus_.Enter(FocusOwner::kText);
          return_focus_ = nullptr;
        } else {
          // Dispatcher priority alone does not mean that XAML has finished
          // its pointer interaction. Keep focus while the cursor is still
          // over native UI, including owned popups, even between presses.
          // Keep the destination through a late Win32 focus grab after exit.
          // Keyboard interaction and semantic focus requests end the loan.
          trace_.Record(InputTraceEvent::kReturn, target, focus_.generation());
          SetFocus(target);
        }
      }
      return;
    }
  }

  bool UrlDragChanged(Point point, bool active) override {
    if (!active && ((sidebar_drop_ && sidebar_drop_->IsReadingDrop()) ||
                    (toolbar_drop_ && toolbar_drop_->IsReadingDrop())))
      return false;
    url_drag_active_ = active;
    url_drag_point_ = point;
    if (active)
      tabs_.DismissHover();
    QueueHoverRefresh();
    if (!active)
      QueueFocusReturn();
    return active &&
           ((layout_.sidebar_visible && layout_.sidebar.Contains(point)) ||
            (layout_.toolbar_visible && layout_.toolbar.Contains(point)) ||
            layout_.sidebar_hover.Contains(point) ||
            layout_.toolbar_hover.Contains(point));
  }
  void QueueHoverRefresh() {
    if (hover_refresh_queued_)
      return;
    hover_refresh_queued_ = true;
    const std::weak_ptr<int> weak = lifetime_;
    PostTask([this, weak] {
      if (weak.expired())
        return;
      hover_refresh_queued_ = false;
      tabs_.RefreshPointerHover();
      if (menu_.IsOpen())
        tabs_.DismissHover();
      sidebar_.RefreshHoverPopups();
      POINT cursor{};
      if (!GetCursorPos(&cursor) || !ScreenToClient(parent_, &cursor))
        return;
      const Point point = url_drag_active_
                              ? url_drag_point_
                              : Point{static_cast<float>(cursor.x) / scale_,
                                      static_cast<float>(cursor.y) / scale_};
      const bool keep_open = BrowserToolsOwnInput() || menu_.IsOpen() ||
                             command_bar_.IsOpen() || tabs_.IsPreviewOpen() ||
                             theme_editor_.IsOpen() || library_.OwnsInput() ||
                             (boost_editor_ && boost_editor_->OwnsInput()) ||
                             HasOpenFlyouts();
      const bool sidebar_revealable =
          !layout_.sidebar.empty() &&
          (!layout_.sidebar_visible || layout_.sidebar_overlays);
      const bool toolbar_revealable =
          !layout_.toolbar.empty() &&
          (!layout_.toolbar_visible || layout_.toolbar_overlays);
      const bool sidebar =
          sidebar_revealable && (layout_.sidebar_hover.Contains(point) ||
                                 (keep_open && layout_.sidebar_visible));
      const bool toolbar =
          (toolbar_revealable && (layout_.toolbar_hover.Contains(point) ||
                                  (keep_open && layout_.toolbar_visible))) ||
          (!layout_.caption_reveal.empty() &&
           (layout_.caption_reveal.Contains(point) ||
            (keep_open && layout_.caption_visible)));
      if (sidebar_hovered_ != sidebar) {
        sidebar_hovered_ = sidebar;
        delegate_.OnSidebarRevealChanged(sidebar);
      }
      if (toolbar_hovered_ != toolbar) {
        toolbar_hovered_ = toolbar;
        delegate_.OnToolbarRevealChanged(toolbar);
      }
    });
  }

  struct CaptionRegion {
    HWND window = nullptr;
    winrt::Windows::Graphics::RectInt32 bounds{}, left{}, right{};
    bool operator==(const CaptionRegion&) const = default;
  };

  void ClipToolbarCaption(Rect island, float scale, int width, int height) {
    const auto px = [scale](float value) {
      return static_cast<std::int32_t>(std::lround(value * scale));
    };
    const auto cutout = [&](Rect band) {
      const float left = std::max(band.x, island.x);
      const float top = std::max(band.y, island.y);
      const float right = std::min(band.right(), island.right());
      const float bottom = std::min(band.bottom(), island.bottom());
      if (right <= left || bottom <= top)
        return winrt::Windows::Graphics::RectInt32{};
      return winrt::Windows::Graphics::RectInt32{
          px(left) - px(island.x), px(top) - px(island.y), px(right) - px(left),
          px(bottom) - px(top)};
    };
    CaptionRegion geometry{WindowOf(toolbar_source_), {0, 0, width, height}};
    const auto& caption = layout_.native_titlebar;
    if (caption.enabled && !fullscreen_ &&
        (!layout_.caption_overlay || layout_.caption_visible)) {
      const float band_height =
          std::clamp(caption.height, 0.0f, client_.height);
      const float left = std::clamp(caption.left_inset, 0.0f, client_.width);
      const float right = std::clamp(caption.right_inset, 0.0f, client_.width);
      geometry.left = cutout({client_.x, client_.y, left, band_height});
      geometry.right =
          cutout({client_.right() - right, client_.y, right, band_height});
    }
    if (toolbar_caption_region_ == geometry)
      return;
    if (!geometry.left.Width && !geometry.right.Width) {
      if (toolbar_caption_region_ && (toolbar_caption_region_->left.Width ||
                                      toolbar_caption_region_->right.Width))
        winrt::check_bool(SetWindowRgn(geometry.window, nullptr, TRUE));
    } else {
      const auto delete_region = [](HRGN value) noexcept {
        DeleteObject(value);
      };
      using Region =
          std::unique_ptr<std::remove_pointer_t<HRGN>, decltype(delete_region)>;
      Region region(CreateRectRgn(0, 0, width, height));
      winrt::check_bool(region != nullptr);
      for (const auto& rect : {geometry.left, geometry.right}) {
        if (!rect.Width || !rect.Height)
          continue;
        Region excluded(CreateRectRgn(rect.X, rect.Y, rect.X + rect.Width,
                                      rect.Y + rect.Height));
        winrt::check_bool(excluded != nullptr);
        winrt::check_bool(CombineRgn(region.get(), region.get(), excluded.get(),
                                     RGN_DIFF) != ERROR);
      }
      winrt::check_bool(SetWindowRgn(geometry.window, region.get(), TRUE));
      // The window owns the region after a successful SetWindowRgn.
      (void)region.release();
    }
    toolbar_caption_region_ = geometry;
  }

  void PlaceIsland(const hosting::DesktopWindowXamlSource& source,
                   const Grid& root,
                   const media::LinearGradientBrush& gradient,
                   IslandHide& hide,
                   const NativeInputTarget& input_target,
                   Rect region,
                   Rect island,
                   bool visible,
                   float scale) {
    // A transparent XAML root still leaves the native child surface present.
    // Hide that surface too; host messages already drive edge-hover reveals.
    const auto index = source == source_ ? 0u : 1u;
    if (!visible || region.empty()) {
      ReleaseFocus(WindowOf(source), return_focus_, parent_);
      if (root.Visibility() == xaml::Visibility::Collapsed)
        return;
      const auto bridge = source.SiteBridge();
      root.IsHitTestVisible(false);
      reveals_[index].Hide(
          reveal_animated_[index] && motion_.Enabled(brushes_.high_contrast()),
          [this, weak = std::weak_ptr<int>(lifetime_), index, root, bridge,
           window = WindowOf(source)] {
            if (weak.expired())
              return;
            island_hide_[index].Hide(window, root.DispatcherQueue(),
                                     [bridge] { bridge.Hide(); });
            root.Visibility(xaml::Visibility::Collapsed);
          });
      return;
    }
    hide.BeforeShow();
    gradient.MappingMode(media::BrushMappingMode::Absolute);
    gradient.StartPoint({client_.x - region.x, client_.y - region.y});
    gradient.EndPoint(
        {client_.right() - region.x, client_.bottom() - region.y});
    const auto px = [scale](float value) {
      return static_cast<std::int32_t>(std::lround(value * scale));
    };
    const auto left = px(island.x), top = px(island.y);
    root.Visibility(xaml::Visibility::Visible);
    const auto width = std::max(1, px(island.right()) - left);
    const auto height = std::max(1, px(island.bottom()) - top);
    root.IsHitTestVisible(true);
    reveal_animated_[index] =
        source == source_ ? layout_.sidebar_overlays : layout_.toolbar_overlays;
    const auto distance =
        source == source_ ? winrt::Windows::Foundation::Numerics::
                                float3{layout_.sidebar_on_right ? island.width
                                                                : -island.width,
                                       0, 0}
                          : winrt::Windows::Foundation::Numerics::float3{
                                0, -island.height, 0};
    // The wrapper owns the opaque floor and shadow as well as the XAML chrome.
    // Moving only its content leaves a stationary duplicate behind it.
    const auto& reveal_root =
        source == source_ ? sidebar_surface_.Root() : toolbar_surface_.Root();
    reveals_[index].Show(
        reveal_root, distance,
        reveal_animated_[index] && motion_.Enabled(brushes_.high_contrast()));
    source.SiteBridge().MoveAndResize({left, top, width, height});
    // A full-width lower row requires a full-width HWND. Its upper corner
    // must leave native caption controls outside both painting and hit testing.
    if (source == toolbar_source_)
      ClipToolbarCaption(island, scale, width, height);
    input_target.Raise();
    source.SiteBridge().Show();
  }

  Rect Bounds(const xaml::FrameworkElement& element) const {
    if (!element)
      return {};
    const auto root = element.XamlRoot();
    if (!root)
      return {};
    for (const auto& target : NativeInputTarget::Roots()) {
      if (!input_.Owns(target.window) || target.XamlRoot() != root)
        continue;
      POINT origin{};
      if (!ClientToScreen(target.window, &origin) ||
          !ScreenToClient(parent_, &origin))
        return {};
      // A transient island is not a child of the sidebar's visual tree.
      // Map from its own XamlRoot through its native client origin, including
      // the floating chrome wrapper and the root's rasterization scale.
      const auto rect = element.TransformToVisual(nullptr).TransformBounds(
          {0, 0, static_cast<float>(element.ActualWidth()),
           static_cast<float>(element.ActualHeight())});
      const auto ratio = static_cast<float>(root.RasterizationScale()) / scale_;
      return {static_cast<float>(origin.x) / scale_ + rect.X * ratio,
              static_cast<float>(origin.y) / scale_ + rect.Y * ratio,
              rect.Width * ratio, rect.Height * ratio};
    }
    return {};
  }

  // A cursor over another app's window above this one is not over the element,
  // even inside its rectangle.
  bool PointerOver(const xaml::FrameworkElement& element) const noexcept try {
    POINT cursor{};
    if (!GetCursorPos(&cursor))
      return false;
    const HWND under = WindowFromPoint(cursor);
    // WinUI input windows may be owned top-level windows, rather than children
    // of the browser HWND. Use the same ownership rules as message routing.
    if (!input_.Owns(under) || !ScreenToClient(parent_, &cursor))
      return false;
    return Bounds(element).Contains({static_cast<float>(cursor.x) / scale_,
                                     static_cast<float>(cursor.y) / scale_});
  } catch (...) {
    // The element can leave the tree while a pointer event is delivered.
    return false;
  }

  void PublishAnchors() {
    if (!anchors_dirty_ || rendering_ || !rendered_)
      return;
    // RTL can move the address slot without changing its size. Apply its
    // placement after native layout, then publish bounds on the next pass.
    if (header_.RefreshPlacement())
      return;
    anchors_dirty_ = false;
    drag_exclusions_.clear();
    header_.AppendDragExclusions(drag_exclusions_, layout_);
    anchors_.clear();
    header_.AppendAnchors(anchors_,
                          layout_.toolbar_layout == ToolbarLayout::kSingle
                              ? layout_.sidebar_visible
                              : layout_.toolbar_visible,
                          layout_.sidebar_visible);
    Rect address_anchor;
    for (const auto& anchor : anchors_)
      if (anchor.kind == AnchorKind::kElement &&
          anchor.id == static_cast<std::uint64_t>(Element::kAddress) &&
          anchor.visible)
        address_anchor = anchor.bounds;
    command_bar_.SetAddressAnchor(address_anchor);
    for (const auto& anchor : anchors_)
      if (anchor.kind == AnchorKind::kElement &&
          anchor.id == static_cast<std::uint64_t>(Element::kAppMenu) &&
          anchor.visible)
        menu_.Reanchor(anchor.bounds);
    if (layout_.tabs == TabLayout::kVertical)
      sidebar_.AppendAnchors(anchors_, layout_.sidebar_visible);
    else
      sidebar_.AppendLibraryAnchor(anchors_, layout_.toolbar_visible);
    top_chrome_.AppendAnchors(anchors_, layout_.toolbar_visible);
    tabs_.AppendAnchors(anchors_, layout_);
    if (layout_.tabs != TabLayout::kVertical)
      for (const auto& anchor : anchors_)
        if (anchor.kind == AnchorKind::kTab && anchor.visible)
          drag_exclusions_.push_back(anchor.bounds);
    top_chrome_.AppendDragExclusions(drag_exclusions_, layout_.toolbar_visible);
    if (onboarding_)
      if (const auto card = onboarding_->CardBounds())
        drag_exclusions_.push_back(*card);
    frame_.Update(layout_.drag_area(), drag_exclusions_, fullscreen_);

    anchors_.push_back({AnchorKind::kElement,
                        static_cast<std::uint64_t>(Element::kSidebar),
                        layout_.sidebar, layout_.sidebar_visible});
    anchors_.push_back({AnchorKind::kElement,
                        static_cast<std::uint64_t>(Element::kToolbar),
                        layout_.toolbar, layout_.toolbar_visible});
    delegate_.OnAnchorsChanged(anchors_);
  }

  void AttachFrameIslands() {
    for (const auto& source : {source_, toolbar_source_}) {
      if (auto result =
              frame_.AttachIsland(winrt::Microsoft::UI::GetWindowFromWindowId(
                  source.SiteBridge().WindowId()));
          !result)
        throw winrt::hresult_error(E_FAIL,
                                   winrt::to_hstring(result.error().message));
    }
  }

  HWND parent_;
  ShellViewDelegate& delegate_;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  bool anchors_dirty_ = true;
  const bool trace_menus_ = EnvFlag("KR_UI_MENU_TRACE");
  bool rendering_ = false, rendered_ = false;
  bool sidebar_hovered_ = false, toolbar_hovered_ = false;
  bool hover_refresh_queued_ = false;
  bool focus_return_queued_ = false;
  bool modal_open_ = false, composing_ = false, pointer_down_ = false;
  FocusOwnership focus_;
  InputTrace trace_;
  bool workspace_gesture_active_ = false;
  SpaceId traced_space_;
  // The host window that had keyboard focus before chrome borrowed it.
  HWND return_focus_ = nullptr;
  bool fullscreen_ = false;
  ShellLayout layout_;
  Rect client_;
  float scale_ = 1;
  Brushes brushes_;
  DelayedTasks delayed_tasks_;
  NativeTitleBar titlebar_;
  NativeFrame frame_;
  NativeInput input_;
  std::array<NativeInputTarget, 3> input_targets_;
  std::array<IslandHide, 3> island_hide_;
  std::optional<CaptionRegion> toolbar_caption_region_;
  ImageCache images_;
  NativeMotion motion_;
  TabPreviewSurface tab_previews_;
  IconSet icons_{motion_, brushes_};
  GlassMaterials glass_;
  ViewContext context_;
  std::unique_ptr<SettingsWindow> settings_;
  std::unique_ptr<OnboardingView> onboarding_;
  std::unique_ptr<BrowserToolWindow> task_manager_, permission_manager_;
  std::unique_ptr<PermissionPromptView> permission_prompt_;
  std::unique_ptr<WorkspaceEditor> workspace_editor_;
  std::unique_ptr<BoostEditor> boost_editor_;
  std::unique_ptr<SplitChooser> split_chooser_;
  std::uint32_t shown_menu_request_ = 0;
  std::optional<Anchor> pending_focus_;
  ChromeHeader header_;
  TabList tabs_;
  SidebarChrome sidebar_;
  TopChrome top_chrome_;
  SidebarSplitter splitter_;
  CommandBar command_bar_;
  ThemeEditor theme_editor_;
  MenuPresenter menu_;
  LibraryPanel library_;
  TransientSurface notification_;
  std::unique_ptr<ThemeBackdrop> notification_backdrop_;
  controls::Border notification_surface_{nullptr};
  controls::TextBlock notification_label_{nullptr};
  ContentFade notification_fade_;
  std::uint64_t notification_serial_ = 0;
  ThemeBackdrop sidebar_backdrop_, toolbar_backdrop_;
  Grid root_, toolbar_root_;
  std::unique_ptr<UrlDropTarget> sidebar_drop_, toolbar_drop_;
  bool url_drag_active_ = false;
  Point url_drag_point_;
  FloatingChrome sidebar_surface_, toolbar_surface_;
  std::array<ChromeReveal, 2> reveals_;
  std::array<bool, 2> reveal_animated_{};
  hosting::DesktopWindowXamlSource source_{nullptr}, toolbar_source_{nullptr};
  hosting::DesktopWindowXamlSource menu_source_{nullptr};
  Grid menu_root_{nullptr};
  xaml::FrameworkElement::Loaded_revoker menu_loaded_;
  std::vector<Anchor> anchors_;
  std::vector<Rect> drag_exclusions_;
  std::array<RootEvents, 2> events_;
};

}  // namespace

namespace platform {

std::string_view Name() noexcept {
  return "windows-winui3";
}

Result<void> ShutdownRuntime() noexcept {
  win::ShutdownThemeGradientWorker();
  win::ShutdownGlassWorker();
  return windows::ShutdownRuntime();
}

Result<void> CheckSupport() noexcept {
  return windows::CheckRuntimeSupport();
}

Result<std::unique_ptr<ShellView>> CreateShellView(
    const ShellViewParams& params,
    ShellViewDelegate& delegate) noexcept {
  if (!params.parent_window ||
      !IsWindow(static_cast<HWND>(params.parent_window)))
    return Fail(ErrorCode::kInvalidArgument, "A valid parent HWND is required");
  if (auto result = windows::EnsureRuntime(); !result) {
    WriteDefaultLog(LogLevel::kError, result.error().message);
    return std::unexpected(result.error());
  }
  try {
    return std::make_unique<WinUIShellView>(
        static_cast<HWND>(params.parent_window), delegate, params.theme);
  } catch (const winrt::hresult_error& error) {
    WriteDefaultLog(LogLevel::kError, winrt::to_string(error.message()));
    return Fail(ErrorCode::kPlatform,
                std::format("WinUI shell: {} (0x{:08x})",
                            winrt::to_string(error.message()),
                            static_cast<std::uint32_t>(error.code().value)));
  } catch (...) {
    return Fail(ErrorCode::kPlatform, "Could not create WinUI shell");
  }
}

void WriteDefaultLog(LogLevel level, std::string_view text) noexcept {
  try {
    const auto line =
        std::format("[krypton-ui {}] {}\n", static_cast<int>(level), text);
    OutputDebugStringA(line.c_str());
    std::fwrite(line.data(), 1, line.size(), stderr);
  } catch (...) {
  }
}

}  // namespace platform

}  // namespace kr
