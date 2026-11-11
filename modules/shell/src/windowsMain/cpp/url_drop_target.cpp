#include "url_drop_target.hpp"

#include <array>

#include "krypton/ui/model/url_drop.hpp"

namespace kr::win {
namespace transfer = winrt::Windows::ApplicationModel::DataTransfer;

// Sidebar items move with pointer drags, so every system drag here comes
// from a page or another application.
bool UrlDropTarget::Supports(const xaml::DragEventArgs& args) {
  const auto data = args.DataView();
  return data.Contains(transfer::StandardDataFormats::WebLink()) ||
         data.Contains(transfer::StandardDataFormats::Uri()) ||
         data.Contains(transfer::StandardDataFormats::Text());
}
UrlDropTarget::UrlDropTarget(ViewContext& context,
                             const xaml::FrameworkElement& root)
    : context_(context), root_(root) {
  root_.AllowDrop(true);
  const auto hover = [this](auto&&, const xaml::DragEventArgs& args) {
    if (!Supports(args))
      return;
    args.AcceptedOperation(transfer::DataPackageOperation::Copy);
    args.Handled(true);
    Update(args, true);
  };
  entered_ = root_.DragEnter(winrt::auto_revoke, hover);
  over_ = root_.DragOver(winrt::auto_revoke, hover);
  left_ = root_.DragLeave(
      winrt::auto_revoke, [this](auto&&, const xaml::DragEventArgs& args) {
        if (drop_pending_)
          return;
        const auto at = args.GetPosition(root_);
        if (at.X < 0 || at.Y < 0 || at.X >= root_.ActualWidth() ||
            at.Y >= root_.ActualHeight())
          Update(args, false);
      });
  dropped_ = root_.Drop(winrt::auto_revoke,
                        [this](auto&&, const xaml::DragEventArgs& args) {
                          if (Supports(args))
                            Drop(args);
                        });
}
UrlDropTarget::~UrlDropTarget() {
  lifetime_.reset();
}
void UrlDropTarget::Update(const xaml::DragEventArgs& args, bool active) {
  const auto at = args.GetPosition(root_);
  const auto bounds = context_.bounds(root_);
  context_.url_drag_changed({bounds.x + at.X, bounds.y + at.Y}, active);
}
winrt::fire_and_forget UrlDropTarget::Drop(xaml::DragEventArgs args) {
  const std::weak_ptr<int> weak = lifetime_;
  const auto generation = ++generation_;
  const auto deferral = args.GetDeferral();
  args.Handled(true);
  args.AcceptedOperation(transfer::DataPackageOperation::Copy);
  drop_pending_ = true;
  // The source still owns the transfer until the deferral completes. Hiding
  // its revealed island here can cancel that transfer before data is read.
  std::string url;
  try {
    const auto data = args.DataView();
    for (const auto& format :
         std::array{transfer::StandardDataFormats::WebLink(),
                    transfer::StandardDataFormats::Uri(),
                    transfer::StandardDataFormats::Text()}) {
      if (!data.Contains(format))
        continue;
      try {
        winrt::hstring text;
        if (format == transfer::StandardDataFormats::WebLink())
          text = (co_await data.GetWebLinkAsync()).AbsoluteUri();
        else if (format == transfer::StandardDataFormats::Uri())
          text = (co_await data.GetUriAsync()).AbsoluteUri();
        else
          text = co_await data.GetTextAsync();
        url = winrt::to_string(text);
        if (IsDroppedWebUrl(url))
          break;
        url.clear();
      } catch (...) {
        // Some sources advertise a format they cannot supply. Try the next
        // advertised URL/text format while the original target stays alive.
      }
    }
  } catch (...) {
    // The source may disappear before its data view can be acquired.
  }
  if (!weak.expired() && generation == generation_) {
    drop_pending_ = false;
    args.AcceptedOperation(url.empty() ? transfer::DataPackageOperation::None
                                       : transfer::DataPackageOperation::Copy);
    if (!url.empty())
      context_.delegate.OnUrlDropped(url);
    context_.url_drag_changed({}, false);
  }
  try {
    deferral.Complete();
  } catch (...) {
    // The window can be destroyed while an asynchronous source is completing.
  }
}
}  // namespace kr::win
