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
  profile_changed_.revoke();
  Close();
}

bool WorkspaceEditor::CreatingProfile() const {
  return profile_ && profile_.SelectedIndex() >= 0 &&
         static_cast<std::size_t>(profile_.SelectedIndex()) ==
             profile_ids_.size();
}

void WorkspaceEditor::Submit(
    const controls::ContentDialogButtonClickEventArgs& args) {
  const auto name = winrt::to_string(name_.Text());
  const auto glyph = glyph_->Value();
  bool accepted = false;
  if (profiles_shown_) {
    const auto index = profile_.SelectedIndex();
    const bool create = CreatingProfile();
    const auto profile = !create && index >= 0 &&
                                 static_cast<std::size_t>(index) <
                                     profile_ids_.size()
                             ? profile_ids_[static_cast<std::size_t>(index)]
                             : ProfileId{};
    std::string created;
    if (create)
      created = winrt::to_string(new_profile_.Text());
    accepted = create && created.empty()
                   ? false
                   : context_.delegate
                         .OnWorkspaceProfileSubmitted(
                             WorkspaceEditKind::kUpsert, name, glyph, profile,
                             created)
                         .has_value();
  } else {
    accepted = context_.delegate
                   .OnWorkspaceSubmitted(WorkspaceEditKind::kUpsert, name,
                                         glyph)
                   .has_value();
  }
  if (!accepted) {
    error_.Text(required_text_);
    error_.Visibility(xaml::Visibility::Visible);
    args.Cancel(true);
  } else {
    requested_ = false;
    closing_ = true;
  }
}

void WorkspaceEditor::RenderProfiles(const WorkspaceEditorVm& vm,
                                     UiTextView text) {
  profiles_shown_ = !vm.icon_tab && !vm.profiles.empty();
  const auto visibility = profiles_shown_ ? xaml::Visibility::Visible
                                          : xaml::Visibility::Collapsed;
  profile_.Visibility(visibility);
  if (!profiles_shown_) {
    new_profile_.Visibility(xaml::Visibility::Collapsed);
    profile_note_.Visibility(xaml::Visibility::Collapsed);
    return;
  }
  // Rebuild the short list only when the host's choices change.
  bool changed = profile_ids_.size() != vm.profiles.size();
  for (std::size_t i = 0; !changed && i < vm.profiles.size(); ++i)
    changed = profile_ids_[i] != vm.profiles[i].id ||
              profile_names_[i] != vm.profiles[i].name;
  const auto selected_id = vm.profile;
  if (changed || profile_serial_ != vm.serial) {
    profile_serial_ = vm.serial;
    profile_ids_.clear();
    profile_names_.clear();
    profile_.Items().Clear();
    int selected = -1;
    for (const auto& choice : vm.profiles) {
      if ((selected_id && choice.id == selected_id) ||
          (!selected_id && choice.current))
        selected = static_cast<int>(profile_ids_.size());
      profile_ids_.push_back(choice.id);
      profile_names_.push_back(choice.name);
      profile_.Items().Append(winrt::box_value(winrt::to_hstring(choice.name)));
    }
    profile_.Items().Append(
        winrt::box_value(winrt::to_hstring(text[UiText::kWorkspaceNewProfile])));
    profile_.SelectedIndex(selected);
    new_profile_.Text(L"");
  }
  profile_.Header(
      winrt::box_value(winrt::to_hstring(text[UiText::kWorkspaceProfile])));
  new_profile_.Header(
      winrt::box_value(winrt::to_hstring(text[UiText::kWorkspaceProfileName])));
  // Tabs and folders keep their profile's cookies and history. Reassignment
  // waits until the Space is empty; the host enforces this too.
  profile_.IsEnabled(!vm.profile_locked);
  new_profile_.Visibility(CreatingProfile() ? xaml::Visibility::Visible
                                            : xaml::Visibility::Collapsed);
  const bool unavailable = vm.profile_status != SpaceProfileStatus::kReady;
  profile_note_.Text(winrt::to_hstring(
      text[unavailable ? UiText::kWorkspaceProfileUnavailable
                       : UiText::kWorkspaceProfileLocked]));
  profile_note_.Visibility(vm.profile_locked || unavailable
                               ? xaml::Visibility::Visible
                               : xaml::Visibility::Collapsed);
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
  profile_ = controls::ComboBox();
  profile_.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  profile_.Visibility(xaml::Visibility::Collapsed);
  new_profile_ = controls::TextBox();
  new_profile_.MaxLength(128);
  new_profile_.Visibility(xaml::Visibility::Collapsed);
  profile_note_ = Label(L"", kTokens.type.size);
  profile_note_.TextWrapping(xaml::TextWrapping::Wrap);
  profile_note_.Visibility(xaml::Visibility::Collapsed);
  profile_changed_ = profile_.SelectionChanged(
      winrt::auto_revoke, [this](auto&&, auto&&) {
        new_profile_.Visibility(CreatingProfile()
                                    ? xaml::Visibility::Visible
                                    : xaml::Visibility::Collapsed);
      });
  content.Children().Append(name_);
  content.Children().Append(glyph_->Root());
  content.Children().Append(profile_);
  content.Children().Append(new_profile_);
  content.Children().Append(profile_note_);
  content.Children().Append(error_);
  dialog_.Content(content);
  dialog_.DefaultButton(controls::ContentDialogButton::Primary);
  save_ = dialog_.PrimaryButtonClick(
      winrt::auto_revoke,
      [this](auto&&, const controls::ContentDialogButtonClickEventArgs& args) {
        Submit(args);
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
  RenderProfiles(vm, text);
  requested_ = true;
  island_.Show(client, scale, theme);
  TryOpen();
  island_.Raise();
}
}  // namespace kr::win
