#include "glass_backdrop_source.hpp"

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

#include "krypton/ui/design/tokens.hpp"

namespace kr::preview {
namespace {
std::atomic<std::uint64_t> next_id{1};
struct Buffer {
  KrGlassBackdropResource resource{};
  winrt::com_ptr<ID3D11Texture2D> texture;
  winrt::com_ptr<ID3D11RenderTargetView> target;
  winrt::com_ptr<ID3D11Fence> ready, released;
  winrt::handle texture_handle, ready_handle, release_handle;
  std::uint64_t revision = 0, frame = 0, serial = 0;
  std::atomic<std::uint64_t> consumed{0};
  std::atomic<bool> pending{false};
};
}  // namespace
struct GlassBackdropSource::Impl {
  HWND window;
  UINT message;
  std::mutex mutex;
  std::condition_variable wake;
  std::shared_ptr<Buffer> output;
  std::uint64_t registered = 0, serial = 0;
  KrRect bounds{};
  std::uint64_t adapter = 0, revision = 0;
  float scale = 1;
  std::shared_ptr<const HostScene> scene;
  HostBackdropPainter painter;
  bool active = false;
  std::uint64_t requests = 0, updates = 0, accepted = 0, rejected = 0,
                stops = 0;
  std::atomic<std::uint64_t> produced{0};
  KrResult last_rejection = KR_OK;
  winrt::com_ptr<ID3D11Device5> device;
  winrt::com_ptr<ID3D11DeviceContext4> context;
  std::jthread worker;
  Impl(HWND owner, UINT ready_message)
      : window(owner),
        message(ready_message),
        worker([this](std::stop_token stop) { Run(stop); }) {}
  ~Impl() {
    worker.request_stop();
    wake.notify_one();
    worker.join();
    wchar_t stats[16]{};
    if (GetEnvironmentVariableW(L"KR_GLASS_STATS", stats,
                                static_cast<DWORD>(std::size(stats))) &&
        stats[0] != L'0')
      std::fprintf(stderr,
                   "glass host: %llu requests, %llu updates, %llu produced, "
                   "%llu accepted, "
                   "%llu rejected (last %u), %llu stops\n",
                   static_cast<unsigned long long>(requests),
                   static_cast<unsigned long long>(updates),
                   static_cast<unsigned long long>(produced.load()),
                   static_cast<unsigned long long>(accepted),
                   static_cast<unsigned long long>(rejected),
                   static_cast<unsigned>(last_rejection),
                   static_cast<unsigned long long>(stops));
  }
  void Initialize(std::uint64_t luid) {
    winrt::com_ptr<IDXGIFactory1> factory;
    winrt::check_hresult(
        CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.put_void()));
    winrt::com_ptr<IDXGIAdapter1> chosen;
    for (UINT i = 0;; ++i) {
      winrt::com_ptr<IDXGIAdapter1> candidate;
      if (factory->EnumAdapters1(i, candidate.put()) == DXGI_ERROR_NOT_FOUND)
        break;
      DXGI_ADAPTER_DESC1 desc{};
      winrt::check_hresult(candidate->GetDesc1(&desc));
      const auto id = static_cast<std::uint64_t>(desc.AdapterLuid.LowPart) |
                      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                           desc.AdapterLuid.HighPart))
                       << 32);
      if (id == luid) {
        chosen = std::move(candidate);
        break;
      }
    }
    if (!chosen)
      winrt::throw_hresult(DXGI_ERROR_UNSUPPORTED);
    winrt::com_ptr<ID3D11Device> d;
    winrt::com_ptr<ID3D11DeviceContext> c;
    winrt::check_hresult(
        D3D11CreateDevice(chosen.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                          D3D11_SDK_VERSION, d.put(), nullptr, c.put()));
    device = d.as<ID3D11Device5>();
    context = c.as<ID3D11DeviceContext4>();
  }
  std::shared_ptr<Buffer> Create(KrRect rect, float dpi, std::uint64_t luid) {
    auto buffer = std::make_shared<Buffer>();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(std::ceil(rect.width * dpi));
    desc.Height = static_cast<UINT>(std::ceil(rect.height * dpi));
    desc.MipLevels = desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags =
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    winrt::check_hresult(
        device->CreateTexture2D(&desc, nullptr, buffer->texture.put()));
    winrt::check_hresult(device->CreateRenderTargetView(
        buffer->texture.get(), nullptr, buffer->target.put()));
    HANDLE handle = nullptr;
    winrt::check_hresult(
        buffer->texture.as<IDXGIResource1>()->CreateSharedHandle(
            nullptr, DXGI_SHARED_RESOURCE_READ, nullptr, &handle));
    buffer->texture_handle.attach(handle);
    winrt::check_hresult(device->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                             __uuidof(ID3D11Fence),
                                             buffer->ready.put_void()));
    winrt::check_hresult(device->CreateFence(0, D3D11_FENCE_FLAG_SHARED,
                                             __uuidof(ID3D11Fence),
                                             buffer->released.put_void()));
    winrt::check_hresult(buffer->ready->CreateSharedHandle(nullptr, GENERIC_ALL,
                                                           nullptr, &handle));
    buffer->ready_handle.attach(handle);
    winrt::check_hresult(buffer->released->CreateSharedHandle(
        nullptr, GENERIC_ALL, nullptr, &handle));
    buffer->release_handle.attach(handle);
    buffer->resource = {
        sizeof(KrGlassBackdropResource),
        next_id++,
        KR_GLASS_D3D11_BGRA8,
        desc.Width,
        desc.Height,
        reinterpret_cast<std::uintptr_t>(buffer->texture_handle.get()),
        reinterpret_cast<std::uintptr_t>(buffer->ready_handle.get()),
        reinterpret_cast<std::uintptr_t>(buffer->release_handle.get()),
        luid,
        rect};
    return buffer;
  }
  void Run(std::stop_token stop) noexcept {
    std::uint64_t observed = 0, device_adapter = 0;
    while (!stop.stop_requested()) {
      std::unique_lock lock(mutex);
      wake.wait(lock,
                [&] { return stop.stop_requested() || serial != observed; });
      if (stop.stop_requested())
        break;
      observed = serial;
      if (!active)
        continue;
      const auto rect = bounds;
      const auto dpi = scale;
      const auto luid = adapter;
      const auto version = revision;
      const auto snapshot = scene;
      auto buffer = output;
      lock.unlock();
      try {
        if (!device || device_adapter != luid) {
          Initialize(luid);
          device_adapter = luid;
          buffer.reset();
        }
        if (!buffer || buffer->resource.bounds.x != rect.x ||
            buffer->resource.bounds.y != rect.y ||
            buffer->resource.bounds.width != rect.width ||
            buffer->resource.bounds.height != rect.height ||
            buffer->resource.width !=
                static_cast<UINT>(std::ceil(rect.width * dpi)))
          buffer = Create(rect, dpi, luid);
        if (buffer->pending)
          continue;
        if (const auto consumed = buffer->consumed.load())
          winrt::check_hresult(context->Wait(buffer->released.get(), consumed));
        HostScene render_scene = *snapshot;
        const float ratio = dpi / render_scene.scale;
        render_scene.client.right =
            static_cast<LONG>(std::lround(render_scene.client.right * ratio));
        render_scene.client.bottom =
            static_cast<LONG>(std::lround(render_scene.client.bottom * ratio));
        render_scene.scale = dpi;
        const auto pixels =
            painter.Paint(render_scene, rect, buffer->resource.width,
                          buffer->resource.height);
        context->UpdateSubresource(
            buffer->texture.get(), 0, nullptr, pixels.data(),
            buffer->resource.width * static_cast<UINT>(sizeof(std::uint32_t)),
            0);
        buffer->serial = observed;
        buffer->revision = version;
        ++buffer->frame;
        winrt::check_hresult(
            context->Signal(buffer->ready.get(), buffer->frame));
        context->Flush();
        buffer->pending = true;
        ++produced;
        {
          std::lock_guard result_lock(mutex);
          output = std::move(buffer);
        }
        PostMessageW(window, message, 0, 0);
      } catch (const winrt::hresult_error& e) {
        const auto text =
            "Preview glass source: " + winrt::to_string(e.message()) + "\n";
        OutputDebugStringA(text.c_str());
        std::fputs(text.c_str(), stderr);
      } catch (...) {
      }
    }
  }
};
GlassBackdropSource::GlassBackdropSource(HWND window, UINT message)
    : impl_(std::make_unique<Impl>(window, message)) {}
GlassBackdropSource::~GlassBackdropSource() = default;
void GlassBackdropSource::Request(const KrGlassBackdropRequest& request,
                                  const KrShellUI& ui,
                                  HostScene scene) {
  auto& i = *impl_;
  ++i.requests;
  if (!request.region_count) {
    Stop(ui);
    return;
  }
  auto bounds = request.regions[0];
  for (std::size_t n = 1; n < request.region_count; ++n) {
    const auto& r = request.regions[n];
    const auto right = std::max(bounds.x + bounds.width, r.x + r.width);
    const auto bottom = std::max(bounds.y + bounds.height, r.y + r.height);
    bounds.x = std::min(bounds.x, r.x);
    bounds.y = std::min(bounds.y, r.y);
    bounds.width = right - bounds.x;
    bounds.height = bottom - bounds.y;
  }
  const float right =
      std::min(scene.client.right / scene.scale,
               bounds.x + bounds.width + request.sampling_margin);
  const float bottom =
      std::min(scene.client.bottom / scene.scale,
               bounds.y + bounds.height + request.sampling_margin);
  bounds.x = std::max(0.0f, bounds.x - request.sampling_margin);
  bounds.y = std::max(0.0f, bounds.y - request.sampling_margin);
  bounds.width = right - bounds.x;
  bounds.height = bottom - bounds.y;
  if (bounds.width <= 0 || bounds.height <= 0) {
    Stop(ui);
    return;
  }
  // Reserve rounding to the next pixel on both axes when capping GPU storage.
  const double area = static_cast<double>(bounds.width) * bounds.height;
  const double edge = static_cast<double>(bounds.width) + bounds.height;
  const double budget = kr::design::kTokens.glass.max_source_pixels - 1;
  const float dpi = std::min(
      request.scale,
      static_cast<float>(2 * budget /
                         (edge + std::sqrt(edge * edge + 4 * area * budget))));
  {
    std::lock_guard lock(i.mutex);
    if (!i.active || i.revision != request.revision) {
      // A frame not yet accepted belongs to the old geometry and has no
      // consumer ownership. The producer may replace it on its next turn.
      if (i.output && i.output->pending &&
          i.output->revision != request.revision)
        i.output->pending = false;
      i.bounds = bounds;
      i.scale = dpi;
      i.scene = std::make_shared<HostScene>(std::move(scene));
      i.adapter = request.adapter_luid;
      i.revision = request.revision;
      i.active = true;
      ++i.serial;
      i.wake.notify_one();
    }
  }
  Present(ui);
}
void GlassBackdropSource::Present(const KrShellUI& ui) {
  if (!ui.self)
    return;
  auto& i = *impl_;
  std::shared_ptr<Buffer> buffer;
  {
    std::lock_guard lock(i.mutex);
    buffer = i.output;
  }
  if (!buffer || !buffer->pending || !i.active)
    return;
  if (buffer->resource.id != i.registered) {
    if (i.registered)
      (void)ui.drop_glass_backdrop(ui.self, i.registered);
    if (ui.register_glass_backdrop(ui.self, &buffer->resource) != KR_OK)
      return;
    i.registered = buffer->resource.id;
  }
  const KrGlassBackdropFrame frame{sizeof(frame), buffer->resource.id,
                                   buffer->revision, buffer->frame,
                                   buffer->frame};
  const auto result = ui.submit_glass_backdrop(ui.self, &frame);
  if (result == KR_OK) {
    ++i.accepted;
    buffer->consumed = buffer->frame;
    buffer->pending = false;
    std::lock_guard lock(i.mutex);
    if (buffer->serial < i.serial) {
      ++i.serial;
      i.wake.notify_one();
    }
  } else {
    ++i.rejected;
    i.last_rejection = result;
  }
}
void GlassBackdropSource::Update(HostScene scene) {
  std::lock_guard lock(impl_->mutex);
  if (impl_->active) {
    ++impl_->updates;
    impl_->scene = std::make_shared<HostScene>(std::move(scene));
    ++impl_->serial;
    impl_->wake.notify_one();
  }
}
void GlassBackdropSource::Stop(const KrShellUI& ui) {
  auto& i = *impl_;
  {
    std::lock_guard lock(i.mutex);
    if (i.active)
      ++i.stops;
    i.active = false;
  }
  if (i.registered && ui.self && ui.drop_glass_backdrop) {
    (void)ui.drop_glass_backdrop(ui.self, i.registered);
    i.registered = 0;
  }
}
}  // namespace kr::preview
