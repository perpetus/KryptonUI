#include "chrome_reveal.hpp"

#include <winrt/Microsoft.UI.Xaml.Hosting.h>

namespace kr::win {
namespace composition = winrt::Microsoft::UI::Composition;
ChromeReveal::~ChromeReveal() {
  lifetime_.reset();
  Cancel();
}
void ChromeReveal::Cancel() {
  ++generation_;
  if (batch_ && listening_)
    batch_.Completed(completed_);
  listening_ = false;
  completed_ = {};
  batch_ = nullptr;
}
void ChromeReveal::Show(const xaml::UIElement& root,
                        winrt::Windows::Foundation::Numerics::float3 hidden,
                        bool animate) {
  hidden_ = hidden;
  if (visible_ && !hiding_) {
    if (!animate && visual_) {
      visual_.StopAnimation(L"Translation");
      visual_.Properties().InsertVector3(L"Translation", {});
    }
    return;
  }
  Cancel();
  xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(root, true);
  visual_ = xaml::Hosting::ElementCompositionPreview::GetElementVisual(root);
  if (!visible_)
    visual_.Properties().InsertVector3(
        L"Translation",
        animate ? hidden_ : winrt::Windows::Foundation::Numerics::float3{});
  visible_ = true;
  hiding_ = false;
  if (!animate) {
    visual_.StopAnimation(L"Translation");
    visual_.Properties().InsertVector3(L"Translation", {});
    return;
  }
  const auto compositor = visual_.Compositor();
  const auto animation = compositor.CreateVector3KeyFrameAnimation();
  animation.InsertExpressionKeyFrame(0, L"this.StartingValue");
  animation.InsertKeyFrame(1, {}, Bezier(compositor, kTokens.compact.easing));
  animation.Duration(Ms(kTokens.motion.compact_ms));
  visual_.StartAnimation(L"Translation", animation);
}
void ChromeReveal::Hide(bool animate,
                        std::move_only_function<void()> complete) {
  if (hiding_ && animate)
    return;
  Cancel();
  if (!visible_ || !visual_ || !animate) {
    visible_ = false;
    hiding_ = false;
    if (visual_) {
      visual_.StopAnimation(L"Translation");
      visual_.Properties().InsertVector3(L"Translation", {});
    }
    complete();
    return;
  }
  hiding_ = true;
  const auto generation = generation_;
  const std::weak_ptr<int> weak = lifetime_;
  const auto compositor = visual_.Compositor();
  batch_ = compositor.CreateScopedBatch(
      composition::CompositionBatchTypes::Animation);
  const auto animation = compositor.CreateVector3KeyFrameAnimation();
  animation.InsertExpressionKeyFrame(0, L"this.StartingValue");
  animation.InsertKeyFrame(1, hidden_,
                           Bezier(compositor, kTokens.compact.easing));
  animation.Duration(Ms(kTokens.motion.compact_ms));
  completed_ = batch_.Completed(
      [this, weak, generation,
       finish = std::make_shared<std::move_only_function<void()>>(
           std::move(complete))](auto&&, auto&&) {
        if (weak.expired() || generation != generation_)
          return;
        hiding_ = false;
        visible_ = false;
        (*finish)();
      });
  listening_ = true;
  visual_.StartAnimation(L"Translation", animation);
  batch_.End();
}
}  // namespace kr::win
