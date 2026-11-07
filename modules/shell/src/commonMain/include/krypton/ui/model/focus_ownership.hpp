#pragma once

#include <cstdint>

namespace kr {

enum class FocusOwner {
  kPage,
  kPointerChrome,
  kKeyboardChrome,
  kText,
  kTransient
};
enum class FocusSurface : std::uint32_t {
  kMenu = 1u << 0,
  kSearch = 1u << 1,
  kEditor = 1u << 2,
  kLibrary = 1u << 3,
  kModal = 1u << 4,
  kFlyout = 1u << 5,
};

// Native notifications observe focus; they never request new ownership.
class FocusOwnership {
 public:
  void Enter(FocusOwner owner) noexcept {
    owner_ = owner;
    ++generation_;
  }
  void SetSurface(FocusSurface surface, bool open) noexcept {
    const auto bit = static_cast<std::uint32_t>(surface);
    const auto next = open ? surfaces_ | bit : surfaces_ & ~bit;
    if (next != surfaces_) {
      surfaces_ = next;
      ++generation_;
    }
  }
  void SetActive(bool active) noexcept {
    if (active_ != active) {
      active_ = active;
      ++generation_;
    }
  }
  [[nodiscard]] FocusOwner owner() const noexcept {
    return surfaces_ ? FocusOwner::kTransient : owner_;
  }
  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_;
  }
  [[nodiscard]] bool CanReturn(std::uint64_t generation) const noexcept {
    return active_ && generation == generation_ && !surfaces_ &&
           (owner_ == FocusOwner::kPage ||
            owner_ == FocusOwner::kPointerChrome);
  }

 private:
  FocusOwner owner_ = FocusOwner::kPage;
  std::uint32_t surfaces_ = 0;
  std::uint64_t generation_ = 0;
  bool active_ = true;
};

}  // namespace kr
