#include "glass_abi.hpp"

#include <cmath>

#include "krypton/ui/shell/shell.hpp"

namespace kr::abi {
namespace {
KrResult Convert(const Result<void>& result) noexcept {
  if (result)
    return KR_OK;
  return result.error().code == ErrorCode::kUnsupported ? KR_ERR_UNSUPPORTED
         : result.error().code == ErrorCode::kPlatform
             ? KR_ERR_PLATFORM
             : KR_ERR_INVALID_ARGUMENT;
}
}  // namespace
void FillGlassUi(KrShellUI& ui) noexcept {
  ui.register_glass_backdrop = [](void* self,
                                  const KrGlassBackdropResource* r) noexcept {
    try {
      if (!self || !r || !KR_HAS_FIELD(r, KrGlassBackdropResource, bounds) ||
          !r->id || r->kind != KR_GLASS_D3D11_BGRA8 || !r->texture ||
          !r->ready_fence || !r->release_fence || !r->width || !r->height ||
          r->width > 16384 || r->height > 16384 ||
          !std::isfinite(r->bounds.x) || !std::isfinite(r->bounds.y) ||
          !std::isfinite(r->bounds.width) || !std::isfinite(r->bounds.height) ||
          r->bounds.width <= 0 || r->bounds.height <= 0)
        return KR_ERR_INVALID_ARGUMENT;
      return Convert(static_cast<Shell*>(self)->RegisterGlassBackdrop(
          {r->id,
           r->texture,
           r->ready_fence,
           r->release_fence,
           r->adapter_luid,
           r->width,
           r->height,
           {r->bounds.x, r->bounds.y, r->bounds.width, r->bounds.height}}));
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
  ui.submit_glass_backdrop = [](void* self,
                                const KrGlassBackdropFrame* f) noexcept {
    try {
      if (!self || !f ||
          !KR_HAS_FIELD(f, KrGlassBackdropFrame, release_value) ||
          !f->resource_id || !f->revision || !f->ready_value ||
          !f->release_value)
        return KR_ERR_INVALID_ARGUMENT;
      return Convert(static_cast<Shell*>(self)->SubmitGlassBackdrop(
          {f->resource_id, f->revision, f->ready_value, f->release_value}));
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
  ui.drop_glass_backdrop = [](void* self, std::uint64_t id) noexcept {
    try {
      if (!self || !id)
        return KR_ERR_INVALID_ARGUMENT;
      return Convert(static_cast<Shell*>(self)->DropGlassBackdrop(id));
    } catch (...) {
      return KR_ERR_PLATFORM;
    }
  };
}
}  // namespace kr::abi
