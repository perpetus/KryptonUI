#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "gdi_resource.hpp"
#include "theme_backdrop.hpp"

namespace kr::preview {

// The preview host owns the page card. Cache its GDI resources between paints.
class PageSurface {
 public:
  void Paint(HDC dc,
             const RECT& client,
             const RECT& page,
             int diameter,
             const BackdropState& backdrop,
             std::uint32_t page_color);
  [[nodiscard]] HRGN ClipRegion() const noexcept { return region_.get(); }

 private:
  ThemeBackdrop backdrop_;
  std::unique_ptr<std::remove_pointer_t<HRGN>, GdiDeleter> region_;
  std::unique_ptr<std::remove_pointer_t<HBRUSH>, GdiDeleter> brush_;
  RECT bounds_{};
  POINT origin_{};
  int diameter_ = -1;
  std::uint32_t color_ = 0;
};

}  // namespace kr::preview
