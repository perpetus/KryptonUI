#pragma once
#include <windows.h>
namespace kr::preview {
struct GdiDeleter {
  void operator()(void* object) const noexcept {
    if (object)
      DeleteObject(static_cast<HGDIOBJ>(object));
  }
};
}  // namespace kr::preview
