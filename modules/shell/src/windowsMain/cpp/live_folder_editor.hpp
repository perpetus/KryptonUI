#pragma once
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <memory>

#include "krypton/ui/presentation/live_folder_editor.hpp"
#include "krypton/ui/presentation/view_model.hpp"
#include "view_context.hpp"
namespace kr::win {
// Retained on the existing island. Native Flyout owns focus/light dismissal;
// no new HWND, dispatcher timer, network work or background thread is needed.
class LiveFolderEditor {
 public:
  explicit LiveFolderEditor(ViewContext& context);
  ~LiveFolderEditor();
  void Show(const xaml::FrameworkElement& anchor,
            const FolderVm& folder,
            std::optional<LiveFolderProvider> preset = std::nullopt);

 private:
  void Save();
  void UpdateProvider();
  ViewContext& context_;
  FolderId folder_;
  controls::Flyout flyout_{nullptr};
  controls::ScrollViewer scroll_{nullptr};
  controls::TextBlock help_{nullptr};
  bool sign_in_required_ = false;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  controls::ComboBox provider_{nullptr};
  controls::TextBox source_{nullptr};
  controls::NumberBox interval_{nullptr};
  controls::NumberBox limit_{nullptr};
  controls::TextBlock status_{nullptr};
  controls::TextBlock error_{nullptr};
  controls::Button save_{nullptr};
  controls::InfoBar warning_{nullptr};
  controls::Button sign_in_{nullptr};
  controls::Button::Click_revoker signed_in_;
  controls::Button::Click_revoker saved_;
  controls::Primitives::Selector::SelectionChanged_revoker provider_changed_;
  float width_ = 0, max_height_ = 0;
};
}  // namespace kr::win
