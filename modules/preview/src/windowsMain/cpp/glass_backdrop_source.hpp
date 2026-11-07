#pragma once
#include <windows.h>

#include <memory>

#include "host_scene.hpp"
#include "krypton_ui/abi.h"

namespace kr::preview {
// The mock host's composed background, excluding KryptonUI foreground layers.
class GlassBackdropSource {
 public:
  GlassBackdropSource(HWND window, UINT ready_message);
  ~GlassBackdropSource();
  GlassBackdropSource(const GlassBackdropSource&) = delete;
  GlassBackdropSource& operator=(const GlassBackdropSource&) = delete;
  void Request(const KrGlassBackdropRequest& request,
               const KrShellUI& ui,
               HostScene scene);
  void Present(const KrShellUI& ui);
  void Update(HostScene scene);
  void Stop(const KrShellUI& ui);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace kr::preview
