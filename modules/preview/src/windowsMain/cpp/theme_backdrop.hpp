#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "gdi_resource.hpp"
#include "krypton_ui/abi.h"

namespace kr::preview {
struct BackdropState {
  std::array<std::uint32_t, 2> gradient;
  const KrWorkspaceTheme* theme = nullptr;
  std::uint64_t generation = 0;
  float scale = 1;
  bool dark = false;
  bool transparent = false;  // Quality glass: paint no chrome backdrop.
};

// Host-side GDI rendering of the optional full theme callback. The mesh and
// premultiplied grain bitmap are cached; painting performs no allocations.
class ThemeBackdrop {
 public:
  bool Paint(HDC dc,
             const RECT& client,
             const RECT& page,
             int radius,
             const BackdropState& state);

 private:
  struct DcDeleter {
    void operator()(HDC dc) const noexcept {
      if (dc)
        DeleteDC(dc);
    }
  };
  std::vector<TRIVERTEX> vertices_;
  std::vector<GRADIENT_TRIANGLE> triangles_;
  std::unique_ptr<std::remove_pointer_t<HBITMAP>, GdiDeleter> grain_;
  std::unique_ptr<std::remove_pointer_t<HDC>, DcDeleter> grain_dc_;
  RECT bounds_{};
  std::uint64_t generation_ = 0;
  bool initialized_ = false;
  bool dark_ = false;
};
}  // namespace kr::preview
