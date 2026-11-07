#pragma once

#include <d3d11_4.h>
#include <dxgi1_3.h>
#include <winrt/base.h>

#include <array>
#include <span>

#include "krypton/ui/core/color.hpp"
#include "krypton/ui/core/geometry.hpp"
#include "krypton/ui/model/glass.hpp"

namespace kr::win {
struct GlassDraw {
  Rect bounds, tile;
  float radius = 0;
  GlassRole role = GlassRole::kSearch;
  friend bool operator==(const GlassDraw&, const GlassDraw&) = default;
};
struct GlassSceneData {
  Color base;
  float scale = 1;
  bool dark = false;
  friend bool operator==(const GlassSceneData&,
                         const GlassSceneData&) = default;
};
struct GlassTexture {
  winrt::com_ptr<ID3D11Texture2D> texture;
  winrt::com_ptr<ID3D11ShaderResourceView> source;
  winrt::com_ptr<ID3D11RenderTargetView> target;
  UINT width = 0, height = 0;
};

// All methods are used exclusively by the adapter's graphics worker. No shader
// compilation, readback, waits, or immediate-context access occurs on the UI
// thread.
class GlassGpu {
 public:
  void Initialize();
  [[nodiscard]] std::uint64_t adapter() const noexcept { return adapter_; }
  [[nodiscard]] ID3D11Device* device() const noexcept { return device_.get(); }
  [[nodiscard]] ID3D11DeviceContext4* context() const noexcept {
    return context_.get();
  }
  // Downsamples a live host copy to half resolution and blurs it there. The
  // result is work[0]; the source is not read by later GPU work.
  void PrepareLive(ID3D11ShaderResourceView* source,
                   UINT width,
                   UINT height,
                   float scale,
                   std::array<GlassTexture, 2>& work);
  void Draw(IDXGISurface* target,
            POINT offset,
            std::span<const GlassDraw> draws,
            const GlassSceneData& scene,
            const GlassTexture& backdrop,
            Rect source_bounds);
  [[nodiscard]] std::uint64_t submissions() const noexcept {
    return submissions_;
  }

 private:
  using V4 = std::array<float, 4>;
  struct Parameters {
    V4 shape{}, geometry{}, optics{}, light{}, tint{}, base{}, source{}, work{},
        options{};
  };
  [[nodiscard]] GlassTexture Texture(UINT width, UINT height);
  void Bind(const Parameters& parameters,
            ID3D11PixelShader* shader,
            ID3D11RenderTargetView* target,
            UINT width,
            UINT height,
            float x = 0,
            float y = 0);
  winrt::com_ptr<ID3D11Device5> device_;
  winrt::com_ptr<ID3D11DeviceContext4> context_;
  winrt::com_ptr<ID3D11VertexShader> vertex_;
  winrt::com_ptr<ID3D11PixelShader> glass_, downsample_, blur_;
  winrt::com_ptr<ID3D11Buffer> constants_;
  winrt::com_ptr<ID3D11SamplerState> clamp_;
  winrt::com_ptr<ID3D11RasterizerState> rasterizer_;
  GlassTexture curve_;
  std::array<float, 2> curve_max_{};
  std::uint64_t adapter_ = 0, submissions_ = 0;
};
}  // namespace kr::win
