#pragma once

// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on

#include <span>
#include <vector>

#include "krypton/ui/core/result.hpp"
#include "krypton/ui/layout/drag_regions.hpp"
#include "krypton/ui/layout/frame_hit_test.hpp"

namespace kr::win {

class NativeFrame {
 public:
  using SettingsChanged = void (*)(void*);
  using ActivationChanged = void (*)(void*, bool);
  NativeFrame() = default;
  ~NativeFrame();
  NativeFrame(const NativeFrame&) = delete;
  NativeFrame& operator=(const NativeFrame&) = delete;

  [[nodiscard]] Result<void> Initialize(HWND parent,
                                        void* context,
                                        SettingsChanged settings,
                                        ActivationChanged activation = nullptr);
  [[nodiscard]] Result<void> AttachIsland(HWND island);
  void Reset() noexcept;
  void Update(std::span<const Rect> draggable,
              std::span<const Rect> controls,
              bool fullscreen);

 private:
  [[nodiscard]] Result<void> AttachChild(HWND child);
  void RefreshMetrics() noexcept;
  [[nodiscard]] FrameHit HitTest(LPARAM point) const noexcept;
  static LRESULT CALLBACK
      ParentProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
  static LRESULT CALLBACK
      IslandProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
  HWND parent_ = nullptr;
  void* context_ = nullptr;
  SettingsChanged settings_ = nullptr;
  ActivationChanged activation_ = nullptr;
  POINT origin_{};
  float scale_ = 1;
  FrameGeometry geometry_;
  DragRegions drag_regions_;
  std::span<const Rect> draggable_;
  std::vector<HWND> children_;
};

}  // namespace kr::win
