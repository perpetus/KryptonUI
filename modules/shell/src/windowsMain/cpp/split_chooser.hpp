#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "krypton/ui/presentation/multi_page.hpp"
#include "transient_surface.hpp"
#include "view_context.hpp"

namespace kr::win {
class SplitOptions;
class SplitChooser {
 public:
  SplitChooser(HWND parent,
               ViewContext& context,
               std::move_only_function<void(bool)> focus_blocked);
  ~SplitChooser();
  void Render(const ShellViewModel::SplitChooserVm& model,
              UiTextView text,
              Rect client,
              float scale,
              xaml::ElementTheme theme,
              const ImageStore* images);

 private:
  void Build();
  void TryOpen();
  void RestoreSelection();
  ViewContext& context_;
  HWND parent_;
  std::unique_ptr<SplitOptions> options_;
  TransientSurface island_;
  std::move_only_function<void(bool)> focus_blocked_;
  controls::ContentDialog dialog_{nullptr};
  controls::ListView tabs_{nullptr};
  controls::Grid layouts_{nullptr};
  controls::TextBlock layout_heading_{nullptr};
  struct LayoutTile {
    controls::Primitives::ToggleButton button{nullptr};
    controls::Canvas canvas{nullptr};
    std::array<controls::Border, kMaxSplitMembers> panes{nullptr, nullptr,
                                                         nullptr, nullptr};
    std::array<controls::Image, kMaxSplitMembers> icons{nullptr, nullptr,
                                                        nullptr, nullptr};
    controls::Primitives::ButtonBase::Click_revoker click;
  };
  std::array<LayoutTile, kSplitLayoutChoices.size()> layout_tiles_;
  foundation::Collections::IObservableVector<IInspectable> items_{nullptr};
  std::vector<TabId> keys_;
  std::array<std::string, 4 + kSplitLayoutChoices.size()> labels_;
  std::array<TabId, kMaxSplitMembers> selected_{};
  std::size_t selected_count_ = 0;
  std::uint64_t open_generation_ = 0;
  bool requested_ = false, showing_ = false, applying_ = false;
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  foundation::IAsyncOperation<controls::ContentDialogResult> operation_{
      nullptr};
  controls::ContentDialog::PrimaryButtonClick_revoker accepted_;
  controls::ListViewBase::SelectionChanged_revoker selection_;
  controls::ListViewBase::ContainerContentChanging_revoker realizing_;
  xaml::FrameworkElement::Loaded_revoker loaded_;
};
}  // namespace kr::win
