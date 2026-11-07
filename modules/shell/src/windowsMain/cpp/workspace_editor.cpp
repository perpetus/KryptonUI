#include "workspace_editor.hpp"

#include <winrt/Microsoft.UI.Dispatching.h>

#include <utility>

#include "krypton/ui/platform/platform.hpp"
#include "native_input.hpp"

namespace kr::win {
WorkspaceEditor::~WorkspaceEditor() {
  lifetime_.reset();
  loaded_.revoke();
  save_.revoke();
  remove_.revoke();
  fallback_.revoke();
  Close();
}

void WorkspaceEditor::Build() {
  if (dialog_)
    return;
  island_.Ensure();
  context_.brushes.Install(island_.Root());
  dialog_ = controls::ContentDialog();
  controls::StackPanel content;
  content.Spacing(kTokens.spacing.section_gap);
  name_ = controls::TextBox();
  glyph_ = std::make_unique<WorkspaceIconPicker>(context_);
  error_ = Label(L"", kTokens.type.size);
  error_.TextWrapping(xaml::TextWrapping::Wrap);
  error_.Visibility(xaml::Visibility::Collapsed);
  content.Children().Append(name_);
  content.Children().Append(glyph_->Root());
  content.Children().Append(error_);
  dialog_.Content(content);
  dialog_.DefaultButton(controls::ContentDialogButton::Primary);
  save_ = dialog_.PrimaryButtonClick(
      winrt::auto_revoke,
      [this](auto&&, const controls::ContentDialogButtonClickEventArgs& args) {
        const auto name = winrt::to_string(name_.Text());
        const auto glyph = glyph_->Value();
        if (!context_.delegate.OnWorkspaceSubmitted(WorkspaceEditKind::kUpsert,
                                                    name, glyph)) {
          error_.Text(required_text_);
          error_.Visibility(xaml::Visibility::Visible);
          args.Cancel(true);
        } else {
          requested_ = false;
          closing_ = true;
        }
      });
  remove_ = dialog_.SecondaryButtonClick(
      winrt::auto_revoke,
      [this](auto&&, const controls::ContentDialogButtonClickEventArgs& args) {
        if (!context_.delegate.OnWorkspaceSubmitted(WorkspaceEditKind::kRemove,
                                                    {}, {}))
          args.Cancel(true);
        else {
          requested_ = false;
          closing_ = true;
        }
      });
  fallback_ = dialog_.ProcessKeyboardAccelerators(
      winrt::auto_revoke,
      [this](auto&&,
             const xaml::Input::ProcessKeyboardAcceleratorEventArgs& args) {
        if (!args.Handled() && context_.input.ForwardUnhandledKey(
                                   static_cast<std::uint32_t>(args.Key())))
          args.Handled(true);
      });
  loaded_ = island_.Root().Loaded(winrt::auto_revoke,
                                  [this](auto&&, auto&&) { TryOpen(); });
}

void WorkspaceEditor::TryOpen() {
  if (!requested_ || showing_ || !island_.IsReady())
    return;
  dialog_.XamlRoot(island_.Root().XamlRoot());
  showing_ = true;
  try {
    focus_blocked_(true);
    operation_ = dialog_.ShowAsync();
    const std::weak_ptr<int> weak = lifetime_;
    const auto queue = island_.Root().DispatcherQueue();
    operation_.Completed([this, weak, queue](auto&&, auto&&) {
      if (weak.expired())
        return;
      // ContentDialog restores focus while completing ShowAsync. Open the next
      // input surface on the following UI turn, after that restoration.
      queue.TryEnqueue([this, weak] {
        if (weak.expired())
          return;
        try {
          FinishClose();
        } catch (const winrt::hresult_error& error) {
          platform::WriteDefaultLog(LogLevel::kError,
                                    winrt::to_string(error.message()));
        } catch (...) {
          platform::WriteDefaultLog(
              LogLevel::kError, "Workspace editor could not finish closing");
        }
      });
    });
  } catch (...) {
    showing_ = false;
    requested_ = false;
    island_.Hide();
    focus_blocked_(false);
    context_.delegate.OnWorkspaceEditorDismissed();
    throw;
  }
}

void WorkspaceEditor::FinishClose() {
  showing_ = false;
  operation_ = nullptr;
  const bool was_closing = std::exchange(closing_, false);
  if (was_closing && requested_) {
    // A replacement request keeps the draft and focus block through reopening.
    TryOpen();
    return;
  }
  if (glyph_)
    glyph_->Close();
  island_.Hide();
  if (std::exchange(requested_, false))
    context_.delegate.OnWorkspaceEditorDismissed();
  focus_blocked_(false);
}

void WorkspaceEditor::Close() noexcept {
  requested_ = false;
  try {
    if (glyph_)
      glyph_->Close();
    if (dialog_ && showing_) {
      closing_ = true;
      dialog_.Hide();
    }
    island_.Hide();
  } catch (...) {
    platform::WriteDefaultLog(LogLevel::kWarning,
                              "Workspace editor could not close");
  }
}

void WorkspaceEditor::Render(const WorkspaceEditorVm& vm,
                             UiTextView text,
                             Rect client,
                             float scale,
                             xaml::ElementTheme theme,
                             bool text_changed) {
  if (!vm.open) {
    Close();
    return;
  }
  Build();
  island_.Root().FlowDirection(text.right_to_left
                                   ? xaml::FlowDirection::RightToLeft
                                   : xaml::FlowDirection::LeftToRight);
  dialog_.RequestedTheme(theme);
  dialog_.Background(context_.glass.For(dialog_, GlassRole::kPanel,
                                        context_.brushes.overlay,
                                        kTokens.radius.overlay));
  dialog_.FlowDirection(island_.Root().FlowDirection());
  if (text_changed || !text_ready_ || serial_ != vm.serial) {
    dialog_.Title(winrt::box_value(
        winrt::to_hstring(text[vm.icon_tab ? UiText::kTabIconEditor
                                           : UiText::kWorkspaceEditor])));
    dialog_.PrimaryButtonText(winrt::to_hstring(text[UiText::kWorkspaceSave]));
    dialog_.CloseButtonText(winrt::to_hstring(text[UiText::kWorkspaceCancel]));
    dialog_.SecondaryButtonText(
        vm.can_remove
            ? winrt::to_hstring(text[vm.icon_tab ? UiText::kResetTabIcon
                                                 : UiText::kWorkspaceDelete])
            : winrt::hstring{});
    name_.Header(
        winrt::box_value(winrt::to_hstring(text[UiText::kWorkspaceName])));
    glyph_->SetText(text);
    required_text_ = winrt::to_hstring(text[UiText::kWorkspaceNameRequired]);
    if (error_.Visibility() == xaml::Visibility::Visible)
      error_.Text(required_text_);
    text_ready_ = true;
  }
  if (serial_ != vm.serial) {
    serial_ = vm.serial;
    name_.Text(winrt::to_hstring(vm.name));
    glyph_->SetValue(vm.glyph);
    error_.Visibility(xaml::Visibility::Collapsed);
  }
  name_.Visibility(vm.icon_tab ? xaml::Visibility::Collapsed
                               : xaml::Visibility::Visible);
  requested_ = true;
  island_.Show(client, scale, theme);
  TryOpen();
  island_.Raise();
}
}  // namespace kr::win
