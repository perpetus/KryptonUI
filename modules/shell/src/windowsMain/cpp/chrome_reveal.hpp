#pragma once

#include <functional>
#include <memory>

#include "native_motion.hpp"

namespace kr::win {
// A finite compositor transition followed by IslandHide's normal pointer exit.
class ChromeReveal {
 public:
  ~ChromeReveal();
  void Show(const xaml::UIElement& root,
            winrt::Windows::Foundation::Numerics::float3 hidden,
            bool animate);
  void Hide(bool animate, std::move_only_function<void()> complete);

 private:
  void Cancel();
  std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
  winrt::Microsoft::UI::Composition::Visual visual_{nullptr};
  winrt::Microsoft::UI::Composition::CompositionScopedBatch batch_{nullptr};
  // Composition batches do not provide IWeakReferenceSource; auto_revoke is
  // therefore unavailable. Keep the source and its explicit token together.
  winrt::event_token completed_{};
  winrt::Windows::Foundation::Numerics::float3 hidden_{};
  std::uint64_t generation_ = 0;
  bool visible_ = false, hiding_ = false, listening_ = false;
};
}  // namespace kr::win
