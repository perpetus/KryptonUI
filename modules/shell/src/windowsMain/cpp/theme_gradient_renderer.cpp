#include "theme_gradient_renderer.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <winrt/Microsoft.Graphics.DirectX.h>
#include <winrt/Microsoft.UI.Composition.Interop.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "krypton/ui/design/theme_pixel.generated.hpp"
#include "krypton/ui/design/theme_vertex.generated.hpp"
#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {
namespace comp = winrt::Microsoft::UI::Composition;
using Float4 = std::array<float, 4>;

struct ThemeBrush : xaml::Media::XamlCompositionBrushBaseT<ThemeBrush> {
  explicit ThemeBrush(comp::Compositor value) : compositor(std::move(value)) {
    FallbackColor({0, 0, 0, 0});
  }
  void OnConnected() noexcept {}
  void OnDisconnected() noexcept {}
  void Clear() noexcept {
    try {
      CompositionBrush(nullptr);
      surface = nullptr;
    } catch (...) {
    }
  }
  void Set(const comp::CompositionDrawingSurface& value) {
    if (!value) {
      Clear();
      return;
    }
    if (!surface) {
      surface = compositor.CreateSurfaceBrush();
      surface.Stretch(comp::CompositionStretch::Fill);
    }
    surface.Surface(value);
    CompositionBrush(surface);
  }
  comp::Compositor compositor{nullptr};
  comp::CompositionSurfaceBrush surface{nullptr};
};

// Only dispatcher callbacks access UI state. The worker holds a weak pointer,
// so it can neither keep a XAML element alive nor release one on its thread.
struct UiState {
  winrt::com_ptr<ThemeBrush> brush;
  winrt::weak_ref<xaml::UIElement> fallback;
  std::uint64_t revision = 0;
  void Apply(const comp::CompositionDrawingSurface& surface) noexcept {
    try {
      brush->Set(surface);
      if (const auto element = fallback.get())
        element.Visibility(surface ? xaml::Visibility::Collapsed
                                   : xaml::Visibility::Visible);
    } catch (...) {
      brush->Clear();
      try {
        if (const auto element = fallback.get())
          element.Visibility(xaml::Visibility::Visible);
      } catch (...) {
      }
    }
  }
};
struct Request {
  ThemeGradient gradient;
  Rect client, region;
  float scale = 1;
  std::uint64_t revision = 0;
};
struct Scene {
  std::mutex mutex;
  Request request;
  bool dirty = false;
  std::atomic<bool> closed{false};
  std::weak_ptr<UiState> ui;
  comp::Compositor compositor{nullptr};
  winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{nullptr};
  // Worker-owned graphics resources.
  comp::CompositionGraphicsDevice graphics{nullptr};
  comp::CompositionDrawingSurface surface{nullptr};
  UINT width = 0, height = 0;
  std::uint64_t generation = 0;
  bool reported = false;
};

struct alignas(16) Parameters {
  Float4 base{}, region{}, pixels{}, options{};
  std::array<Float4, 3> colors{}, geometry{}, fades{};
};
static_assert(sizeof(Parameters) % 16 == 0);

Float4 Channels(Color color) noexcept {
  return {color.r() / 255.0f, color.g() / 255.0f, color.b() / 255.0f, 1};
}

class Gpu {
 public:
  void Initialize() {
    winrt::check_hresult(D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
        device.put(), nullptr, context_.put()));
    winrt::check_hresult(device->CreateVertexShader(
        g_theme_vertex, sizeof(g_theme_vertex), nullptr, vertex_.put()));
    winrt::check_hresult(device->CreatePixelShader(
        g_theme_pixel, sizeof(g_theme_pixel), nullptr, pixel_.put()));
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(Parameters);
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    winrt::check_hresult(
        device->CreateBuffer(&buffer, nullptr, constants_.put()));
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    winrt::check_hresult(
        device->CreateRasterizerState(&raster, rasterizer_.put()));
  }

  void Draw(IDXGISurface* target,
            POINT offset,
            const Request& request,
            UINT width,
            UINT height) {
    winrt::com_ptr<IDXGISurface> borrowed;
    borrowed.copy_from(target);
    const auto resource = borrowed.as<ID3D11Texture2D>();
    winrt::com_ptr<ID3D11RenderTargetView> view;
    winrt::check_hresult(
        device->CreateRenderTargetView(resource.get(), nullptr, view.put()));
    Parameters p;
    p.base = Channels(request.gradient.base);
    p.region = {request.region.x, request.region.y, request.region.width,
                request.region.height};
    p.pixels = {static_cast<float>(width), static_cast<float>(height),
                (request.region.x - request.client.x) * request.scale,
                (request.region.y - request.client.y) * request.scale};
    p.options[0] =
        request.gradient.layer_count
            ? static_cast<float>(
                  design::kTokens.workspace_gradient.dither_amplitude)
            : 0;
    for (std::size_t i = 0; i < request.gradient.layer_count; ++i) {
      const auto& layer = request.gradient.layers[i];
      const bool radial = layer.kind == ThemeGradientKind::kRadial;
      p.colors[i] = Channels(layer.color);
      p.fades[i] = {layer.start, layer.end, radial ? 1.0f : 0.0f, 1};
      if (radial) {
        const auto circle = GradientCircle(layer, request.client);
        p.geometry[i] = {circle.center.x, circle.center.y, circle.radius, 0};
      } else {
        const auto line = GradientLine(layer, request.client);
        const float dx = line.end.x - line.start.x;
        const float dy = line.end.y - line.start.y;
        const float length_squared = dx * dx + dy * dy;
        p.geometry[i] = {line.start.x, line.start.y, dx / length_squared,
                         dy / length_squared};
      }
    }
    context_->UpdateSubresource(constants_.get(), 0, nullptr, &p, 0, 0);
    auto* output = view.get();
    context_->OMSetRenderTargets(1, &output, nullptr);
    context_->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vertex_.get(), nullptr, 0);
    context_->PSSetShader(pixel_.get(), nullptr, 0);
    auto* constants = constants_.get();
    context_->PSSetConstantBuffers(0, 1, &constants);
    const D3D11_VIEWPORT viewport{static_cast<float>(offset.x),
                                  static_cast<float>(offset.y),
                                  static_cast<float>(width),
                                  static_cast<float>(height),
                                  0,
                                  1};
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(rasterizer_.get());
    context_->Draw(3, 0);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->Flush();
  }

  winrt::com_ptr<ID3D11Device> device;

 private:
  winrt::com_ptr<ID3D11DeviceContext> context_;
  winrt::com_ptr<ID3D11VertexShader> vertex_;
  winrt::com_ptr<ID3D11PixelShader> pixel_;
  winrt::com_ptr<ID3D11Buffer> constants_;
  winrt::com_ptr<ID3D11RasterizerState> rasterizer_;
};

class Worker {
 public:
  Worker() : thread_([this](std::stop_token stop) { Run(stop); }) {}
  ~Worker() {
    thread_.request_stop();
    wake_.notify_one();
    thread_.join();
  }
  void Add(const std::shared_ptr<Scene>& scene) {
    std::lock_guard lock(mutex_);
    scenes_.push_back(scene);
    ++serial_;
    wake_.notify_one();
  }
  void Wake() {
    std::lock_guard lock(mutex_);
    ++serial_;
    wake_.notify_one();
  }

 private:
  static void Notify(const Scene& scene,
                     const comp::CompositionDrawingSurface& surface,
                     std::uint64_t revision) noexcept {
    try {
      if (!scene.closed)
        (void)scene.dispatcher.TryEnqueue([weak = scene.ui, surface, revision] {
          if (const auto ui = weak.lock(); ui && ui->revision == revision)
            ui->Apply(surface);
        });
    } catch (...) {
      // Dispatcher shutdown can race an in-flight draw. The weak UI state
      // prevents completion from touching an element that has been destroyed.
    }
  }

  void Process(Scene& scene, const Request& request) {
    if (!gpu_) {
      auto gpu = std::make_unique<Gpu>();
      gpu->Initialize();
      gpu_ = std::move(gpu);
      ++generation_;
    }
    if (scene.generation != generation_) {
      scene.graphics = nullptr;
      scene.surface = nullptr;
      scene.generation = generation_;
    }
    const float width = std::ceil(request.region.width * request.scale);
    const float height = std::ceil(request.region.height * request.scale);
    winrt::check_bool(std::isfinite(width) && std::isfinite(height) &&
                      width > 0 && height > 0 &&
                      width <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION &&
                      height <= D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
    const auto w = static_cast<UINT>(width), h = static_cast<UINT>(height);
    if (!scene.graphics)
      winrt::check_hresult(
          scene.compositor.as<comp::ICompositorInterop>()->CreateGraphicsDevice(
              gpu_->device.get(), &scene.graphics));
    if (!scene.surface || scene.width != w || scene.height != h) {
      const auto surface = scene.graphics.CreateDrawingSurface(
          {width, height},
          winrt::Microsoft::Graphics::DirectX::DirectXPixelFormat::
              B8G8R8A8UIntNormalized,
          winrt::Microsoft::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
      scene.surface = surface;
      scene.width = w;
      scene.height = h;
    }
    const auto interop =
        scene.surface.as<comp::ICompositionDrawingSurfaceInterop>();
    winrt::com_ptr<IDXGISurface> target;
    POINT offset{};
    winrt::check_hresult(interop->BeginDraw(nullptr, __uuidof(IDXGISurface),
                                            target.put_void(), &offset));
    try {
      gpu_->Draw(target.get(), offset, request, w, h);
    } catch (...) {
      (void)interop->EndDraw();
      throw;
    }
    winrt::check_hresult(interop->EndDraw());
    Notify(scene, scene.surface, request.revision);
  }

  void Run(std::stop_token stop) noexcept {
    try {
      winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
      return;  // The native brushes remain available without GPU rendering.
    }
    std::uint64_t serial = 0;
    while (!stop.stop_requested()) {
      std::unique_lock lock(mutex_);
      wake_.wait(lock,
                 [&] { return stop.stop_requested() || serial_ != serial; });
      if (stop.stop_requested())
        break;
      serial = serial_;
      const auto count = scenes_.size();
      for (std::size_t i = 0; i < count; ++i) {
        const auto scene = scenes_[i];
        lock.unlock();
        Request request;
        bool dirty = false;
        {
          std::lock_guard scene_lock(scene->mutex);
          if (!scene->closed && scene->dirty) {
            request = scene->request;
            scene->dirty = false;
            dirty = true;
          }
        }
        if (dirty) {
          try {
            Process(*scene, request);
          } catch (...) {
            if (!scene->reported) {
              platform::WriteDefaultLog(
                  LogLevel::kWarning,
                  "Smooth theme gradient unavailable; using native brushes");
              scene->reported = true;
            }
            Notify(*scene, nullptr, request.revision);
            if (gpu_ && FAILED(gpu_->device->GetDeviceRemovedReason()))
              gpu_.reset();
          }
        }
        lock.lock();
      }
      std::erase_if(scenes_,
                    [](const auto& scene) { return scene->closed.load(); });
    }
    scenes_.clear();
    gpu_.reset();
    winrt::uninit_apartment();
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::uint64_t serial_ = 0, generation_ = 0;
  std::vector<std::shared_ptr<Scene>> scenes_;
  std::unique_ptr<Gpu> gpu_;
  std::jthread thread_;
};

std::unique_ptr<Worker>& GradientWorker() {
  static std::unique_ptr<Worker> worker;
  return worker;
}
Worker& GetWorker() {
  auto& worker = GradientWorker();
  if (!worker)
    worker = std::make_unique<Worker>();
  return *worker;
}
}  // namespace

struct ThemeGradientRenderer::Impl {
  std::shared_ptr<UiState> ui = std::make_shared<UiState>();
  std::shared_ptr<Scene> scene = std::make_shared<Scene>();
};

ThemeGradientRenderer::ThemeGradientRenderer(
    const xaml::FrameworkElement& element,
    const xaml::UIElement& fallback)
    : impl_(std::make_unique<Impl>()) {
  auto& scene = *impl_->scene;
  scene.compositor =
      xaml::Hosting::ElementCompositionPreview::GetElementVisual(element)
          .Compositor();
  scene.dispatcher =
      winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
  winrt::check_bool(static_cast<bool>(scene.dispatcher));
  impl_->ui->brush = winrt::make_self<ThemeBrush>(scene.compositor);
  impl_->ui->fallback = winrt::make_weak(fallback);
  scene.ui = impl_->ui;
  GetWorker().Add(impl_->scene);
}

ThemeGradientRenderer::~ThemeGradientRenderer() {
  impl_->scene->closed = true;
  impl_->ui.reset();
  if (const auto& worker = GradientWorker())
    worker->Wake();
}

xaml::Media::Brush ThemeGradientRenderer::Brush() const {
  return impl_->ui->brush.as<xaml::Media::Brush>();
}

void ThemeGradientRenderer::Render(const ThemeGradient& gradient,
                                   Rect client,
                                   Rect region,
                                   float scale) {
  const auto revision = ++impl_->ui->revision;
  {
    std::lock_guard lock(impl_->scene->mutex);
    impl_->scene->request = {gradient, client, region, scale, revision};
    impl_->scene->dirty = true;
  }
  GetWorker().Wake();
}

void ShutdownThemeGradientWorker() noexcept {
  GradientWorker().reset();
}
}  // namespace kr::win
