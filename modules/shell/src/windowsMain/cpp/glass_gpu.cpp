#include "glass_gpu.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "krypton/ui/design/glass_blur.generated.hpp"
#include "krypton/ui/design/glass_downsample.generated.hpp"
#include "krypton/ui/design/glass_pixel.generated.hpp"
#include "krypton/ui/design/glass_vertex.generated.hpp"
#include "krypton/ui/design/tokens.hpp"

namespace kr::win {
namespace {
auto FloatColor(Color color) noexcept {
  return std::array<float, 4>{color.r() / 255.0f, color.g() / 255.0f,
                              color.b() / 255.0f, color.a() / 255.0f};
}
}  // namespace
void GlassGpu::Initialize() {
  winrt::com_ptr<ID3D11Device> device;
  winrt::com_ptr<ID3D11DeviceContext> context;
  constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,
                                       D3D_FEATURE_LEVEL_11_0};
  winrt::check_hresult(D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION,
      device.put(), nullptr, context.put()));
  device_ = device.as<ID3D11Device5>();
  context_ = context.as<ID3D11DeviceContext4>();
  winrt::com_ptr<IDXGIAdapter> actual;
  winrt::check_hresult(device.as<IDXGIDevice>()->GetAdapter(actual.put()));
  DXGI_ADAPTER_DESC desc{};
  winrt::check_hresult(actual->GetDesc(&desc));
  adapter_ = static_cast<std::uint64_t>(desc.AdapterLuid.LowPart) |
             (static_cast<std::uint64_t>(
                  static_cast<std::uint32_t>(desc.AdapterLuid.HighPart))
              << 32);
  winrt::check_hresult(device_->CreateVertexShader(
      g_glass_vertex, sizeof(g_glass_vertex), nullptr, vertex_.put()));
  winrt::check_hresult(device_->CreatePixelShader(
      g_glass_pixel, sizeof(g_glass_pixel), nullptr, glass_.put()));
  winrt::check_hresult(device_->CreatePixelShader(g_glass_downsample,
                                                  sizeof(g_glass_downsample),
                                                  nullptr, downsample_.put()));
  winrt::check_hresult(device_->CreatePixelShader(
      g_glass_blur, sizeof(g_glass_blur), nullptr, blur_.put()));
  D3D11_BUFFER_DESC buffer{};
  buffer.ByteWidth = sizeof(Parameters);
  buffer.Usage = D3D11_USAGE_DEFAULT;
  buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  winrt::check_hresult(
      device_->CreateBuffer(&buffer, nullptr, constants_.put()));
  D3D11_SAMPLER_DESC sampler{};
  sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = sampler.AddressV = sampler.AddressW =
      D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler.MaxLOD = D3D11_FLOAT32_MAX;
  winrt::check_hresult(device_->CreateSamplerState(&sampler, clamp_.put()));
  D3D11_RASTERIZER_DESC raster{};
  raster.FillMode = D3D11_FILL_SOLID;
  raster.CullMode = D3D11_CULL_NONE;
  raster.DepthClipEnable = true;
  winrt::check_hresult(
      device_->CreateRasterizerState(&raster, rasterizer_.put()));
  // Live surfaces are panels and search; both profiles are precomputed once.
  const auto& t = design::kTokens.glass;
  const std::array curves{
      MakeGlassCurve(t.panel_bezel, t.thickness, t.refractive_index,
                     t.displacement_limit),
      MakeGlassCurve(t.search_bezel, t.search_thickness, t.refractive_index,
                     t.search_displacement_limit,
                     GlassSurfaceProfile::kConvexSquircle)};
  for (std::size_t i = 0; i < curves.size(); ++i)
    curve_max_[i] = *std::ranges::max_element(curves[i]);
  D3D11_TEXTURE2D_DESC texture{};
  texture.Width = static_cast<UINT>(kGlassCurveSamples);
  texture.Height = static_cast<UINT>(curves.size());
  texture.MipLevels = texture.ArraySize = 1;
  texture.Format = DXGI_FORMAT_R32_FLOAT;
  texture.SampleDesc.Count = 1;
  texture.Usage = D3D11_USAGE_IMMUTABLE;
  texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA data{
      curves.data(), static_cast<UINT>(kGlassCurveSamples * sizeof(float)), 0};
  winrt::check_hresult(
      device_->CreateTexture2D(&texture, &data, curve_.texture.put()));
  winrt::check_hresult(device_->CreateShaderResourceView(
      curve_.texture.get(), nullptr, curve_.source.put()));
}
GlassTexture GlassGpu::Texture(UINT width, UINT height) {
  GlassTexture result{.width = width, .height = height};
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  winrt::check_hresult(
      device_->CreateTexture2D(&desc, nullptr, result.texture.put()));
  winrt::check_hresult(device_->CreateShaderResourceView(
      result.texture.get(), nullptr, result.source.put()));
  winrt::check_hresult(device_->CreateRenderTargetView(
      result.texture.get(), nullptr, result.target.put()));
  return result;
}
void GlassGpu::Bind(const Parameters& p,
                    ID3D11PixelShader* shader,
                    ID3D11RenderTargetView* target,
                    UINT width,
                    UINT height,
                    float x,
                    float y) {
  context_->UpdateSubresource(constants_.get(), 0, nullptr, &p, 0, 0);
  context_->OMSetRenderTargets(1, &target, nullptr);
  context_->OMSetBlendState(nullptr, nullptr, ~0u);
  context_->IASetInputLayout(nullptr);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(vertex_.get(), nullptr, 0);
  context_->PSSetShader(shader, nullptr, 0);
  auto* buffer = constants_.get();
  context_->PSSetConstantBuffers(0, 1, &buffer);
  auto* sampler = clamp_.get();
  context_->PSSetSamplers(0, 1, &sampler);
  const D3D11_VIEWPORT viewport{
      x, y, static_cast<float>(width), static_cast<float>(height), 0, 1};
  context_->RSSetViewports(1, &viewport);
  context_->RSSetState(rasterizer_.get());
  context_->Draw(3, 0);
  ++submissions_;
  ID3D11ShaderResourceView* empty[2]{};
  context_->PSSetShaderResources(0, 2, empty);
  context_->OMSetRenderTargets(0, nullptr, nullptr);
}
void GlassGpu::PrepareLive(ID3D11ShaderResourceView* source,
                           UINT width,
                           UINT height,
                           float scale,
                           std::array<GlassTexture, 2>& work) {
  const UINT w = std::max(1u, (width + 1) / 2),
             h = std::max(1u, (height + 1) / 2);
  if (work[0].width != w || work[0].height != h)
    for (auto& texture : work)
      texture = Texture(w, h);
  Parameters p{};
  p.work = {1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height),
            0, 0};
  context_->PSSetShaderResources(0, 1, &source);
  Bind(p, downsample_.get(), work[0].target.get(), w, h);
  // Sigma in half-resolution texels; the 2x2 box already covers less.
  const float sigma = design::kTokens.glass.search_blur * scale * 0.5f;
  if (sigma < 0.5f)
    return;
  p.work = {1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h), 1, 0};
  p.options[1] = sigma;
  auto* input = work[0].source.get();
  context_->PSSetShaderResources(0, 1, &input);
  Bind(p, blur_.get(), work[1].target.get(), w, h);
  p.work[2] = 0;
  p.work[3] = 1;
  input = work[1].source.get();
  context_->PSSetShaderResources(0, 1, &input);
  Bind(p, blur_.get(), work[0].target.get(), w, h);
}
void GlassGpu::Draw(IDXGISurface* target,
                    POINT offset,
                    std::span<const GlassDraw> draws,
                    const GlassSceneData& scene,
                    const GlassTexture& backdrop,
                    Rect source_bounds) {
  winrt::com_ptr<IDXGISurface> borrowed;
  borrowed.copy_from(target);
  auto resource = borrowed.as<ID3D11Texture2D>();
  winrt::com_ptr<ID3D11RenderTargetView> view;
  winrt::check_hresult(
      device_->CreateRenderTargetView(resource.get(), nullptr, view.put()));
  Parameters p{};
  const auto& t = design::kTokens.glass;
  p.optics = {0, t.search_highlight, t.search_specular_width,
              t.search_saturation};
  const float angle = t.search_light_angle * std::numbers::pi_v<float> / 180;
  p.light = {std::cos(angle), std::sin(angle), 0, 0};
  p.base = FloatColor(scene.base);
  p.source = {source_bounds.x, source_bounds.y, source_bounds.width,
              source_bounds.height};
  p.work = {1.0f / static_cast<float>(backdrop.width),
            1.0f / static_cast<float>(backdrop.height), 0, 0};
  for (const auto& draw : draws) {
    p.shape = {draw.bounds.x, draw.bounds.y, draw.bounds.width,
               draw.bounds.height};
    const bool search = draw.role == GlassRole::kSearch;
    p.tint = FloatColor(
        Color{search ? (scene.dark ? t.search_dark_tint : t.search_light_tint)
                     : (scene.dark ? t.dark_tint : t.light_tint)});
    p.work[2] = search ? t.search_refraction_scale : 1.0f;
    const std::size_t row = search ? 1 : 0;
    p.work[3] = curve_max_[row];
    p.geometry = {draw.radius, search ? t.search_bezel : t.panel_bezel,
                  scene.scale, (static_cast<float>(row) + 0.5f) / 2.0f};
    ID3D11ShaderResourceView* inputs[]{backdrop.source.get(),
                                       curve_.source.get()};
    context_->PSSetShaderResources(0, 2, inputs);
    Bind(p, glass_.get(), view.get(), static_cast<UINT>(draw.tile.width),
         static_cast<UINT>(draw.tile.height),
         draw.tile.x + static_cast<float>(offset.x),
         draw.tile.y + static_cast<float>(offset.y));
  }
  context_->Flush();
}
}  // namespace kr::win
