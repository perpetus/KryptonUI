#include "glass_material.hpp"

#include <unknwn.h>
#include <windows.h>
#include <winrt/Microsoft.Graphics.DirectX.h>
#include <winrt/Microsoft.UI.Composition.Interop.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.Power.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <format>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "glass_gpu.hpp"
#include "krypton/ui/core/env.hpp"
#include "krypton/ui/platform/platform.hpp"

namespace kr::win {
namespace {
namespace comp = winrt::Microsoft::UI::Composition;
namespace media = xaml::Media;
namespace hosting = xaml::Hosting;
struct MaterialBrush : media::XamlCompositionBrushBaseT<MaterialBrush> {
  std::atomic<bool> connected{false};
  std::move_only_function<void()> changed;
  void OnConnected() noexcept {
    connected.store(true);
    if (changed)
      changed();
  }
  void OnDisconnected() noexcept {
    connected.store(false);
    if (changed)
      changed();
  }
  void Set(const comp::CompositionBrush& value) {
    if (CompositionBrush() != value)
      CompositionBrush(value);
  }
};
struct Resource {
  GlassBackdropResource description;
  winrt::handle texture, ready, release;
  std::atomic<int> status{0};  // pending, usable, failed
  bool drop = false, pending = false;
  GlassBackdropFrame frame;
  std::uint64_t last_ready = 0, last_release = 0;
  winrt::com_ptr<ID3D11Texture2D> imported;
  winrt::com_ptr<ID3D11ShaderResourceView> source;
  winrt::com_ptr<ID3D11Fence> producer, consumer;
};
struct Scene : std::enable_shared_from_this<Scene> {
  std::mutex mutex;
  GlassSceneData data;
  std::vector<GlassDraw> draws;
  std::array<std::unique_ptr<Resource>, 4> resources;
  comp::Compositor compositor{nullptr};
  comp::CompositionGraphicsDevice graphics{nullptr};
  comp::CompositionDrawingSurface surface{nullptr};
  winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{nullptr};
  std::move_only_function<void()> completed;
  std::atomic<bool> dirty{false}, closed{false}, live{false}, failed{false},
      imported{false};
  std::array<GlassTexture, 2> live_textures;
  Rect live_bounds;
  bool notified_live = false;
  std::uint64_t device_generation = 0;
  std::atomic<std::uint64_t> adapter{0}, revision{1}, rendered{0},
      submissions{0}, memory{0}, frames{0};
  UINT width = 0, height = 0, output_width = 0, output_height = 0;
  // Worker-only reusable snapshots.
  std::vector<GlassDraw> work_draws;
};
// One event-driven worker per process. It wakes only for geometry changes and
// host frames; Performance and steady Quality chrome never reach it.
class Worker {
 public:
  Worker() : thread_([this](std::stop_token stop) { Run(stop); }) {}
  ~Worker() {
    thread_.request_stop();
    wake_.notify_one();
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
  static void Notify(Scene& scene) {
    if (!scene.closed)
      (void)scene.dispatcher.TryEnqueue([weak = scene.weak_from_this()] {
        if (const auto value = weak.lock();
            value && !value->closed && value->completed)
          value->completed();
      });
  }
  void Run(std::stop_token stop) noexcept {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    std::uint64_t serial = 0;
    while (!stop.stop_requested()) {
      std::unique_lock lock(mutex_);
      wake_.wait(lock,
                 [&] { return stop.stop_requested() || serial_ != serial; });
      if (stop.stop_requested())
        break;
      serial = serial_;
      // Scene ownership is stable while processing; Add only takes this lock.
      // Driver calls never run under a mutex taken by frame submission.
      const auto count = scenes_.size();
      for (std::size_t i = 0; i < count; ++i) {
        Scene* scene = scenes_[i].get();
        lock.unlock();
        try {
          Process(*scene);
        } catch (const winrt::hresult_error& error) {
          if (!scene->failed.exchange(true)) {
            const auto message = "Liquid glass GPU unavailable: " +
                                 winrt::to_string(error.message());
            platform::WriteDefaultLog(LogLevel::kWarning, message);
          }
          scene->live = false;
          Notify(*scene);
          if (gpu_ && gpu_->device() &&
              FAILED(gpu_->device()->GetDeviceRemovedReason()))
            gpu_.reset();
        } catch (...) {
          scene->failed = true;
          scene->live = false;
          Notify(*scene);
        }
        lock.lock();
      }
      std::erase_if(scenes_,
                    [](const auto& scene) { return scene->closed.load(); });
    }
    scenes_.clear();
    winrt::uninit_apartment();
  }
  void Reset(Scene& scene) {
    scene.graphics = nullptr;
    std::lock_guard lock(scene.mutex);
    scene.surface = nullptr;
    scene.live_textures = {};
    scene.live = false;
    scene.notified_live = false;
    for (auto& slot : scene.resources)
      if (slot) {
        slot->source = nullptr;
        slot->imported = nullptr;
        slot->producer = nullptr;
        slot->consumer = nullptr;
        slot->status = 0;
      }
  }
  bool Import(Scene& scene) {
    bool imported = false;
    for (auto& slot : scene.resources) {
      Resource* resource;
      {
        std::lock_guard lock(scene.mutex);
        resource = slot.get();
      }
      if (!resource || resource->status != 0)
        continue;
      imported = true;
      try {
        if (resource->description.adapter != gpu_->adapter())
          winrt::throw_hresult(DXGI_ERROR_UNSUPPORTED);
        auto device = winrt::com_ptr<ID3D11Device>{};
        device.copy_from(gpu_->device());
        const auto native = device.as<ID3D11Device5>();
        winrt::check_hresult(native->OpenSharedFence(
            resource->ready.get(), __uuidof(ID3D11Fence),
            resource->producer.put_void()));
        winrt::check_hresult(native->OpenSharedFence(
            resource->release.get(), __uuidof(ID3D11Fence),
            resource->consumer.put_void()));
        winrt::check_hresult(native->OpenSharedResource1(
            resource->texture.get(), __uuidof(ID3D11Texture2D),
            resource->imported.put_void()));
        D3D11_TEXTURE2D_DESC desc{};
        resource->imported->GetDesc(&desc);
        if (desc.Width != resource->description.width ||
            desc.Height != resource->description.height ||
            desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
            desc.SampleDesc.Count != 1 || desc.ArraySize != 1 ||
            desc.MipLevels != 1 ||
            !(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE))
          winrt::throw_hresult(E_INVALIDARG);
        winrt::check_hresult(native->CreateShaderResourceView(
            resource->imported.get(), nullptr, resource->source.put()));
        resource->status = 1;
      } catch (...) {
        resource->status = -1;
      }
    }
    return imported;
  }
  void Process(Scene& scene) {
    if (scene.closed) {
      // Accepted frames still release ownership when a window closes.
      if (gpu_)
        ReleasePending(scene);
      return;
    }
    if (!scene.dirty.exchange(false))
      return;
    if (!gpu_) {
      auto device = std::make_unique<GlassGpu>();
      device->Initialize();
      gpu_ = std::move(device);
      ++device_generation_;
    }
    if (scene.device_generation != device_generation_) {
      Reset(scene);
      scene.device_generation = device_generation_;
    }
    scene.failed = false;
    if (scene.adapter.exchange(gpu_->adapter()) == 0)
      Notify(scene);
    const auto submissions_before = gpu_->submissions();
    const bool imported = Import(scene);
    GlassSceneData data;
    UINT width, height;
    std::uint64_t revision;
    {
      std::lock_guard lock(scene.mutex);
      data = scene.data;
      width = scene.width;
      height = scene.height;
      scene.work_draws.assign(scene.draws.begin(), scene.draws.end());
      revision = scene.revision.load();
    }
    bool new_frame = false;
    for (auto& slot : scene.resources) {
      Resource* resource;
      GlassBackdropFrame frame;
      {
        std::lock_guard lock(scene.mutex);
        resource = slot.get();
        if (!resource || !resource->pending)
          continue;
        frame = resource->frame;
      }
      winrt::check_hresult(
          gpu_->context()->Wait(resource->producer.get(), frame.ready));
      if (frame.revision == revision && !scene.work_draws.empty()) {
        gpu_->PrepareLive(resource->source.get(), resource->description.width,
                          resource->description.height,
                          static_cast<float>(resource->description.width) /
                              resource->description.bounds.width,
                          scene.live_textures);
        scene.live_bounds = resource->description.bounds;
        new_frame = true;
        scene.live = true;
        ++scene.frames;
      }
      // The downsample was the copy's only reader; the host may reuse it.
      winrt::check_hresult(
          gpu_->context()->Signal(resource->consumer.get(), frame.release));
      {
        std::lock_guard lock(scene.mutex);
        resource->pending = false;
      }
    }
    {
      std::lock_guard lock(scene.mutex);
      for (auto& resource : scene.resources)
        if (resource && resource->drop && !resource->pending)
          resource.reset();
    }
    if (imported) {
      scene.imported = true;
      Notify(scene);
    }
    if (!width || !height || scene.work_draws.empty()) {
      gpu_->context()->Flush();
      {
        std::lock_guard lock(scene.mutex);
        scene.surface = nullptr;
      }
      scene.live_textures = {};
      scene.live = false;
      scene.notified_live = false;
      scene.output_width = scene.output_height = 0;
      scene.memory = 0;
      return;
    }
    // Until the first host frame, elements show their fallback color.
    if (!scene.live) {
      gpu_->context()->Flush();
      return;
    }
    if (!scene.graphics) {
      winrt::check_hresult(
          scene.compositor.as<comp::ICompositorInterop>()->CreateGraphicsDevice(
              gpu_->device(), &scene.graphics));
    }
    const bool resized = !scene.surface || scene.output_width != width ||
                         scene.output_height != height;
    if (resized) {
      const auto output = scene.graphics.CreateDrawingSurface(
          {static_cast<float>(width), static_cast<float>(height)},
          winrt::Microsoft::Graphics::DirectX::DirectXPixelFormat::
              B8G8R8A8UIntNormalized,
          winrt::Microsoft::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
      {
        std::lock_guard lock(scene.mutex);
        scene.surface = output;
      }
      scene.output_width = width;
      scene.output_height = height;
    }
    auto surface = scene.surface.as<comp::ICompositionDrawingSurfaceInterop>();
    winrt::com_ptr<IDXGISurface> target;
    POINT offset{};
    winrt::check_hresult(surface->BeginDraw(nullptr, __uuidof(IDXGISurface),
                                            target.put_void(), &offset));
    try {
      gpu_->Draw(target.get(), offset, scene.work_draws, data,
                 scene.live_textures[0], scene.live_bounds);
    } catch (...) {
      (void)surface->EndDraw();
      throw;
    }
    winrt::check_hresult(surface->EndDraw());
    scene.submissions.fetch_add(gpu_->submissions() - submissions_before);
    scene.memory = static_cast<std::uint64_t>(width) * height * 4 +
                   static_cast<std::uint64_t>(scene.live_textures[0].width) *
                       scene.live_textures[0].height * 8;
    const bool changed = scene.rendered.exchange(revision) != revision;
    // A live frame changes pixels without UI work. Only the first live frame
    // needs a UI notification to attach the surface.
    if (resized || changed || (new_frame && !scene.notified_live)) {
      scene.notified_live = true;
      Notify(scene);
    }
  }
  void ReleasePending(Scene& scene) {
    std::lock_guard lock(scene.mutex);
    for (auto& resource : scene.resources)
      if (resource && resource->pending && resource->status == 1) {
        (void)gpu_->context()->Wait(resource->producer.get(),
                                    resource->frame.ready);
        (void)gpu_->context()->Signal(resource->consumer.get(),
                                      resource->frame.release);
        resource->pending = false;
      }
    gpu_->context()->Flush();
  }
  std::mutex mutex_;
  std::condition_variable wake_;
  std::uint64_t serial_ = 0;
  std::vector<std::shared_ptr<Scene>> scenes_;
  std::unique_ptr<GlassGpu> gpu_;
  std::uint64_t device_generation_ = 0;
  std::jthread thread_;
};
std::unique_ptr<Worker>& GraphicsWorker() {
  static std::unique_ptr<Worker> worker;
  return worker;
}
Worker& GetWorker() {
  auto& worker = GraphicsWorker();
  if (!worker)
    worker = std::make_unique<Worker>();
  return *worker;
}
winrt::handle Duplicate(std::uintptr_t handle) {
  HANDLE value = nullptr;
  winrt::check_bool(DuplicateHandle(
      GetCurrentProcess(), reinterpret_cast<HANDLE>(handle),
      GetCurrentProcess(), &value, 0, false, DUPLICATE_SAME_ACCESS));
  return winrt::handle{value};
}
winrt::Windows::UI::Color Native(std::uint32_t argb) noexcept {
  const Color color{argb};
  return {color.a(), color.r(), color.g(), color.b()};
}
}  // namespace

struct GlassMaterials::Impl {
  // A Quality surface over the page, refracting the host's live copy.
  struct Entry {
    winrt::weak_ref<xaml::FrameworkElement> element;
    winrt::com_ptr<MaterialBrush> brush;
    comp::CompositionSurfaceBrush native{nullptr};
    GlassRole role{};
    float radius = 0;
    Rect placed;
    bool explicit_bounds = false;
  };
  // Static specular rim. The compositor draws it; only a size change touches
  // it from the UI thread. Chrome roles also get a connection-aware brush
  // over a shared tint, so the rim hides when the element stops using it.
  struct Rim {
    winrt::weak_ref<xaml::FrameworkElement> element;
    winrt::com_ptr<MaterialBrush> brush;
    comp::ShapeVisual visual{nullptr};
    comp::CompositionRoundedRectangleGeometry geometry{nullptr};
    comp::CompositionSpriteShape glow{nullptr};
    xaml::FrameworkElement::SizeChanged_revoker size_changed;
    GlassRole role{};
    float radius = -1;
  };
  ShellViewDelegate& delegate;
  std::move_only_function<Rect(const xaml::FrameworkElement&)> bounds;
  std::unordered_map<void*, Entry> entries;
  std::unordered_map<void*, Rim> rims;
  std::vector<GlassDraw> draws;
  std::vector<void*> draw_keys;
  std::vector<Rect> regions;
  std::shared_ptr<Scene> scene;
  GlassSceneData data;
  GlassMode requested = GlassMode::kOff, policy = GlassMode::kOff;
  comp::CompositionColorBrush chrome_tint{nullptr}, address_tint{nullptr};
  comp::CompositionLinearGradientBrush rim_brush{nullptr};
  std::array<comp::CompositionColorGradientStop, 4> rim_stops{nullptr, nullptr,
                                                              nullptr, nullptr};
  winrt::Windows::UI::ViewManagement::UISettings settings{nullptr};
  bool attached = false, request_active = false, pending_layout = false,
       theme_dirty = false, animations = true;
  std::uint64_t requested_revision = 0;
  Impl(ShellViewDelegate& d,
       std::move_only_function<Rect(const xaml::FrameworkElement&)> b)
      : delegate(d), bounds(std::move(b)), scene(std::make_shared<Scene>()) {
    scene->dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::
        GetForCurrentThread();
    scene->completed = [this] { Completed(); };
  }
  [[nodiscard]] bool Live(GlassRole role) const {
    return GlassUsesLiveBackdrop(policy, role) &&
           delegate.CanRequestGlassBackdrop();
  }
  void QueueLayout() {
    if (pending_layout)
      return;
    pending_layout = true;
    (void)scene->dispatcher.TryEnqueue(
        [weak = std::weak_ptr<Scene>(scene), this] {
          if (const auto value = weak.lock(); value && !value->closed) {
            pending_layout = false;
            Layout();
          }
        });
  }
  void ApplyTheme() {
    if (!rim_brush)
      return;
    const auto& t = design::kTokens.glass;
    chrome_tint.Color(
        Native(data.dark ? t.chrome_dark_tint : t.chrome_light_tint));
    address_tint.Color(
        Native(data.dark ? t.address_dark_tint : t.address_light_tint));
    const auto highlight = Native(data.dark ? t.rim_dark : t.rim_light);
    auto clear = highlight;
    clear.A = 0;
    rim_stops[0].Color(highlight);
    rim_stops[1].Color(clear);
    rim_stops[2].Color(clear);
    rim_stops[3].Color(
        Native(data.dark ? t.rim_dark_shade : t.rim_light_shade));
  }
  void EnsureRimBrush(const comp::Compositor& compositor) {
    if (rim_brush && rim_brush.Compositor() == compositor)
      return;
    // Relative mapping follows each rim's own bounds: a highlight from the top
    // left, clear sides, and a faint shade toward the bottom right.
    chrome_tint = compositor.CreateColorBrush();
    address_tint = compositor.CreateColorBrush();
    rim_brush = compositor.CreateLinearGradientBrush();
    rim_brush.StartPoint({0, 0});
    rim_brush.EndPoint({1, 1});
    const float fade = design::kTokens.glass.rim_fade;
    const std::array offsets{0.0f, fade, 1 - fade, 1.0f};
    for (std::size_t i = 0; i < rim_stops.size(); ++i) {
      rim_stops[i] = compositor.CreateColorGradientStop();
      rim_stops[i].Offset(offsets[i]);
      rim_brush.ColorStops().Append(rim_stops[i]);
    }
    ApplyTheme();
  }
  void SyncGlow(Rim& rim) {
    const bool glow = policy == GlassMode::kQuality &&
                      rim.role != GlassRole::kAddress && animations;
    if (glow == static_cast<bool>(rim.glow))
      return;
    const auto shapes = rim.visual.Shapes();
    if (!glow) {
      if (std::uint32_t index = 0; shapes.IndexOf(rim.glow, index))
        shapes.RemoveAt(index);
      rim.glow = nullptr;
      return;
    }
    const auto element = rim.element.get();
    if (!element)
      return;
    const auto compositor = rim.visual.Compositor();
    auto brush = compositor.CreateRadialGradientBrush();
    brush.MappingMode(comp::CompositionMappingMode::Absolute);
    brush.EllipseRadius({design::kTokens.glass.pointer_radius,
                         design::kTokens.glass.pointer_radius});
    const auto alpha =
        static_cast<std::uint8_t>(design::kTokens.glass.pointer_opacity * 255);
    brush.ColorStops().Append(
        compositor.CreateColorGradientStop(0, {alpha, 255, 255, 255}));
    brush.ColorStops().Append(
        compositor.CreateColorGradientStop(1, {0, 255, 255, 255}));
    auto expression = compositor.CreateExpressionAnimation(
        L"Vector2(pointer.Position.X, pointer.Position.Y)");
    expression.SetReferenceParameter(
        L"pointer",
        hosting::ElementCompositionPreview::GetPointerPositionPropertySet(
            element));
    brush.StartAnimation(L"CenterPoint", expression);
    rim.glow = compositor.CreateSpriteShape(rim.geometry);
    rim.glow.StrokeThickness(design::kTokens.glass.edge_width);
    rim.glow.StrokeBrush(brush);
    shapes.Append(rim.glow);
  }
  Rim& AttachRim(const xaml::FrameworkElement& element,
                 GlassRole role,
                 float radius) {
    const auto key = winrt::get_abi(element);
    auto it = rims.find(key);
    if (it != rims.end() && it->second.element.get() != element) {
      rims.erase(it);
      it = rims.end();
    }
    const float inset = design::kTokens.glass.edge_width / 2;
    if (it == rims.end()) {
      std::erase_if(
          rims, [](const auto& item) { return !item.second.element.get(); });
      const auto compositor =
          hosting::ElementCompositionPreview::GetElementVisual(element)
              .Compositor();
      EnsureRimBrush(compositor);
      Rim rim;
      rim.element = winrt::make_weak(element);
      rim.role = role;
      rim.geometry = compositor.CreateRoundedRectangleGeometry();
      rim.geometry.Offset({inset, inset});
      auto stroke = compositor.CreateSpriteShape(rim.geometry);
      stroke.StrokeThickness(design::kTokens.glass.edge_width);
      stroke.StrokeBrush(rim_brush);
      rim.visual = compositor.CreateShapeVisual();
      rim.visual.Shapes().Append(stroke);
      const auto resize = [visual = rim.visual, geometry = rim.geometry, inset](
                              float width, float height) {
        visual.Size({width, height});
        geometry.Size({std::max(0.0f, width - 2 * inset),
                       std::max(0.0f, height - 2 * inset)});
      };
      resize(static_cast<float>(element.ActualWidth()),
             static_cast<float>(element.ActualHeight()));
      rim.size_changed = element.SizeChanged(
          winrt::auto_revoke,
          [resize](auto&&, const xaml::SizeChangedEventArgs& args) {
            const auto size = args.NewSize();
            resize(size.Width, size.Height);
          });
      hosting::ElementCompositionPreview::SetElementChildVisual(element,
                                                                rim.visual);
      it = rims.emplace(key, std::move(rim)).first;
    }
    auto& rim = it->second;
    rim.role = role;
    if (rim.radius != radius) {
      rim.radius = radius;
      const float corner = std::max(0.0f, radius - inset);
      rim.geometry.CornerRadius({corner, corner});
    }
    SyncGlow(rim);
    return rim;
  }
  void DetachRim(const xaml::FrameworkElement& element) {
    if (const auto it = rims.find(winrt::get_abi(element)); it != rims.end()) {
      if (it->second.element.get() == element)
        hosting::ElementCompositionPreview::SetElementChildVisual(element,
                                                                  nullptr);
      rims.erase(it);
    }
  }
  void DetachRims() {
    for (auto& [key, rim] : rims)
      if (const auto element = rim.element.get())
        hosting::ElementCompositionPreview::SetElementChildVisual(element,
                                                                  nullptr);
    rims.clear();
  }
  void DropEntry(const xaml::FrameworkElement& element) {
    if (const auto it = entries.find(winrt::get_abi(element));
        it != entries.end()) {
      it->second.brush->Set(nullptr);
      entries.erase(it);
      QueueLayout();
    }
  }
  void Completed() {
    if (scene->failed) {
      for (auto& [key, entry] : entries)
        entry.brush->Set(nullptr);
      return;
    }
    const bool live = scene->live;
    comp::CompositionDrawingSurface surface{nullptr};
    {
      std::lock_guard lock(scene->mutex);
      surface = scene->surface;
    }
    if (scene->rendered != scene->revision) {
      Request(scene->imported.exchange(false));
      return;
    }
    for (std::size_t index = 0; index < draws.size(); ++index) {
      const auto found = entries.find(draw_keys[index]);
      if (found == entries.end())
        continue;
      auto& entry = found->second;
      if (!entry.brush->connected)
        continue;
      if (!live || !surface) {
        entry.brush->Set(nullptr);
        continue;
      }
      const auto& draw = draws[index];
      if (!entry.native)
        entry.native = scene->compositor.CreateSurfaceBrush();
      entry.native.Surface(surface);
      entry.native.Stretch(comp::CompositionStretch::None);
      entry.native.HorizontalAlignmentRatio(0);
      entry.native.VerticalAlignmentRatio(0);
      entry.native.Scale({1 / data.scale, 1 / data.scale});
      entry.native.Offset(
          {-draw.tile.x / data.scale, -draw.tile.y / data.scale});
      entry.brush->Set(entry.native);
    }
    Request(scene->imported.exchange(false));
  }

  void Request(bool force = false) {
    const bool active = !regions.empty() && scene->adapter &&
                        delegate.CanRequestGlassBackdrop();
    if (!active && !request_active)
      return;
    if (!force && active == request_active &&
        requested_revision == scene->revision)
      return;
    request_active = active;
    requested_revision = scene->revision;
    delegate.OnGlassBackdropRequested(
        {requested_revision,
         active ? std::span<const Rect>(regions) : std::span<const Rect>{},
         data.scale,
         3 * design::kTokens.glass.search_blur +
             std::max(design::kTokens.glass.displacement_limit,
                      design::kTokens.glass.search_displacement_limit *
                          design::kTokens.glass.search_refraction_scale * 0.5f),
         scene->adapter.load()});
  }
  void Layout() {
    // Runs on every XAML layout pass; without live surfaces it does nothing.
    if (entries.empty() && draws.empty() && !request_active)
      return;
    draws.clear();
    draw_keys.clear();
    regions.clear();
    UINT x = 0, y = 0, row = 0, width = 0;
    for (auto it = entries.begin(); it != entries.end();) {
      auto& entry = it->second;
      const auto element = entry.element.get();
      if (!element) {
        it = entries.erase(it);
        continue;
      }
      ++it;
      if (!entry.brush->connected || !element.XamlRoot() ||
          element.ActualWidth() <= 0 || element.ActualHeight() <= 0)
        continue;
      Rect rect = entry.explicit_bounds ? entry.placed : bounds(element);
      if (rect.empty())
        continue;
      if (rect.x < data_client.x || rect.y < data_client.y ||
          rect.right() > data_client.right() ||
          rect.bottom() > data_client.bottom()) {
        entry.brush->Set(nullptr);
        continue;
      }
      if (!scene->compositor)
        scene->compositor =
            hosting::ElementCompositionPreview::GetElementVisual(element)
                .Compositor();
      const UINT w = static_cast<UINT>(std::ceil(rect.width * data.scale)),
                 h = static_cast<UINT>(std::ceil(rect.height * data.scale));
      if (!w || !h || w > 8192 || h > 8192)
        continue;
      if (x && x + w > 2048) {
        y += row;
        row = 0;
        x = 0;
      }
      if (static_cast<std::uint64_t>(std::max(width, x + w)) *
              (y + std::max(row, h)) >
          design::kTokens.glass.max_atlas_pixels) {
        entry.brush->Set(nullptr);
        continue;
      }
      draws.push_back({rect,
                       {static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(w), static_cast<float>(h)},
                       entry.radius,
                       entry.role});
      draw_keys.push_back(winrt::get_abi(element));
      regions.push_back(rect);
      x += w;
      row = std::max(row, h);
      width = std::max(width, x);
    }
    const UINT height = y + row;
    bool changed;
    {
      std::lock_guard lock(scene->mutex);
      changed = theme_dirty || scene->draws != draws;
      if (changed) {
        scene->data = data;
        scene->draws.assign(draws.begin(), draws.end());
        scene->width = width;
        scene->height = height;
        ++scene->revision;
      }
    }
    theme_dirty = false;
    if (changed) {
      if (!attached && scene->compositor) {
        GetWorker().Add(scene);
        attached = true;
      }
      scene->dirty = true;
      if (attached)
        GetWorker().Wake();
    }
    Request();
  }
  Rect data_client;
};

GlassMaterials::GlassMaterials(
    ShellViewDelegate& delegate,
    std::move_only_function<Rect(const xaml::FrameworkElement&)> bounds)
    : impl_(std::make_unique<Impl>(delegate, std::move(bounds))) {}
GlassMaterials::~GlassMaterials() {
  auto& i = *impl_;
  i.policy = GlassMode::kOff;
  i.regions.clear();
  i.Request();
  i.scene->closed = true;
  for (auto& [key, entry] : i.entries)
    entry.brush->Set(nullptr);
  i.DetachRims();
  if (i.attached)
    GetWorker().Wake();
  if (EnvFlag("KR_GLASS_STATS")) {
    const auto message = std::format(
        "glass: {} draw submissions, {} resident bytes, {} live elements, "
        "{} live frames",
        i.scene->submissions.load(), i.scene->memory.load(), i.entries.size(),
        i.scene->frames.load());
    platform::WriteDefaultLog(LogLevel::kInfo, message);
  }
}
void GlassMaterials::Update(const ShellViewModel& vm) {
  auto& i = *impl_;
  if (vm.theme.glass_mode == GlassMode::kOff &&
      i.requested == GlassMode::kOff && i.policy == GlassMode::kOff)
    return;
  if (i.requested == vm.theme.glass_mode && !i.scene->failed &&
      !vm.changed.HasAny(Dirty::kTheme | Dirty::kWindow | Dirty::kLayout))
    return;
  const auto previous = i.policy;
  i.requested = vm.theme.glass_mode;
  bool effects = true, energy = false;
  try {
    if (!i.settings)
      i.settings = winrt::Windows::UI::ViewManagement::UISettings();
    effects = i.settings.AdvancedEffectsEnabled();
    i.animations = i.settings.AnimationsEnabled();
    energy = winrt::Windows::System::Power::PowerManager::EnergySaverStatus() ==
             winrt::Windows::System::Power::EnergySaverStatus::On;
  } catch (...) {
    effects = false;
  }
  i.policy =
      ResolveGlassMode(i.requested, {.transparency = effects,
                                     .high_contrast = vm.theme.high_contrast,
                                     .energy_saver = energy});
  i.data_client = vm.client;
  const bool dark_changed = i.data.dark != vm.theme.dark;
  i.data.scale = vm.scale_factor;
  i.data.dark = vm.theme.dark;
  i.data.base = Color{vm.theme.palette->gradient_base};
  if (dark_changed)
    i.ApplyTheme();
  if (i.policy == GlassMode::kOff)
    i.DetachRims();
  else if (previous != i.policy)
    for (auto& [key, rim] : i.rims)
      i.SyncGlow(rim);
  // Leaving Quality (or a host without backdrops) ends live refraction.
  if (!i.entries.empty() && (i.policy != GlassMode::kQuality ||
                             !i.delegate.CanRequestGlassBackdrop())) {
    for (auto& [key, entry] : i.entries)
      entry.brush->Set(nullptr);
    i.entries.clear();
  }
  i.theme_dirty |= previous != i.policy || vm.changed.Has(Dirty::kTheme) ||
                   vm.changed.Has(Dirty::kWindow) || i.scene->failed;
  if (!i.entries.empty() || !i.draws.empty() || i.request_active)
    i.QueueLayout();
}
media::Brush GlassMaterials::For(const xaml::FrameworkElement& element,
                                 GlassRole role,
                                 const media::Brush& fallback,
                                 float radius) {
  auto& i = *impl_;
  if (!GlassEligible(i.policy, role)) {
    i.DetachRim(element);
    i.DropEntry(element);
    return fallback;
  }
  if (!i.Live(role)) {
    // Static material: a shared tint for steady chrome, the surface's own
    // background for floating panels, and a compositor-drawn rim.
    i.DropEntry(element);
    auto& rim = i.AttachRim(element, role, radius);
    if (GlassUsesPanelProfile(role))
      return fallback;
    if (!rim.brush) {
      rim.brush = winrt::make_self<MaterialBrush>();
      rim.visual.IsVisible(false);
      rim.brush->changed = [visual = rim.visual, brush = rim.brush.get()] {
        visual.IsVisible(brush->connected);
      };
    }
    const auto& tint =
        role == GlassRole::kAddress ? i.address_tint : i.chrome_tint;
    rim.brush->Set(tint);
    return rim.brush.as<media::Brush>();
  }
  i.DetachRim(element);
  const auto key = winrt::get_abi(element);
  auto [it, inserted] = i.entries.try_emplace(key);
  auto& entry = it->second;
  if (!inserted && entry.element.get() != element) {
    entry = {};
    inserted = true;
  }
  if (inserted) {
    entry.element = winrt::make_weak(element);
    entry.brush = winrt::make_self<MaterialBrush>();
    entry.role = role;
    entry.radius = radius;
    entry.brush->changed = [weak = std::weak_ptr<Scene>(i.scene), owner = &i] {
      if (const auto scene = weak.lock(); scene && !scene->closed)
        owner->QueueLayout();
    };
    i.QueueLayout();
  }
  if (entry.role != role || entry.radius != radius) {
    entry.role = role;
    entry.radius = radius;
    i.QueueLayout();
  }
  if (const auto solid = fallback.try_as<media::SolidColorBrush>()) {
    const auto brush = entry.brush.as<media::XamlCompositionBrushBase>();
    if (brush.FallbackColor() != solid.Color())
      brush.FallbackColor(solid.Color());
  }
  return entry.brush.as<media::Brush>();
}
void GlassMaterials::Place(const xaml::FrameworkElement& element, Rect bounds) {
  if (const auto it = impl_->entries.find(winrt::get_abi(element));
      it != impl_->entries.end()) {
    if (!it->second.explicit_bounds || it->second.placed != bounds) {
      it->second.placed = bounds;
      it->second.explicit_bounds = true;
      impl_->QueueLayout();
    }
  }
}
void GlassMaterials::Layout() noexcept {
  try {
    impl_->Layout();
  } catch (...) {
  }
}
Result<void> GlassMaterials::Register(const GlassBackdropResource& resource) {
  auto& i = *impl_;
  if (!i.attached || resource.adapter != i.scene->adapter)
    return Fail(ErrorCode::kUnsupported, "Backdrop adapter unavailable");
  if (static_cast<std::uint64_t>(resource.width) * resource.height >
      design::kTokens.glass.max_source_pixels)
    return Fail(ErrorCode::kInvalidArgument, "Backdrop exceeds pixel budget");
  auto value = std::make_unique<Resource>();
  value->description = resource;
  value->texture = Duplicate(resource.texture);
  value->ready = Duplicate(resource.ready_fence);
  value->release = Duplicate(resource.release_fence);
  {
    std::lock_guard lock(i.scene->mutex);
    for (const auto& slot : i.scene->resources)
      if (slot && slot->description.id == resource.id)
        return Fail(ErrorCode::kInvalidArgument, "Duplicate backdrop id");
    auto slot = std::ranges::find_if(i.scene->resources,
                                     [](const auto& r) { return !r; });
    if (slot == i.scene->resources.end())
      return Fail(ErrorCode::kInvalidArgument,
                  "Backdrop resource budget exceeded");
    *slot = std::move(value);
  }
  i.scene->dirty = true;
  GetWorker().Wake();
  return {};
}
Result<void> GlassMaterials::Submit(const GlassBackdropFrame& frame) {
  auto& i = *impl_;
  if (!i.request_active || frame.revision != i.scene->revision)
    return Fail(ErrorCode::kInvalidArgument, "Stale backdrop frame");
  {
    std::lock_guard lock(i.scene->mutex);
    const auto it =
        std::ranges::find_if(i.scene->resources, [&](const auto& r) {
          return r && r->description.id == frame.resource;
        });
    if (it == i.scene->resources.end() || (*it)->drop)
      return Fail(ErrorCode::kInvalidArgument, "Unknown backdrop resource");
    auto& resource = **it;
    if (resource.status != 1)
      return Fail(ErrorCode::kUnsupported,
                  "Backdrop import pending or unavailable");
    if (resource.pending || frame.ready <= resource.last_ready ||
        frame.release <= resource.last_release)
      return Fail(ErrorCode::kInvalidArgument,
                  "Backdrop already pending or fence values did not advance");
    resource.frame = frame;
    resource.pending = true;
    resource.last_ready = frame.ready;
    resource.last_release = frame.release;
  }
  i.scene->dirty = true;
  GetWorker().Wake();
  return {};
}
Result<void> GlassMaterials::Drop(std::uint64_t id) {
  auto& i = *impl_;
  {
    std::lock_guard lock(i.scene->mutex);
    const auto it = std::ranges::find_if(
        i.scene->resources,
        [id](const auto& r) { return r && r->description.id == id; });
    if (it == i.scene->resources.end())
      return Fail(ErrorCode::kInvalidArgument, "Unknown backdrop resource");
    (*it)->drop = true;
  }
  i.scene->dirty = true;
  if (i.attached)
    GetWorker().Wake();
  return {};
}
void ShutdownGlassWorker() noexcept {
  GraphicsWorker().reset();
}
}  // namespace kr::win
