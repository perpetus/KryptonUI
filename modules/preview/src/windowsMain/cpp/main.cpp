// clang-format off
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <wincodec.h>
// clang-format on
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "appearance_store.hpp"
#include "glass_backdrop_source.hpp"
#include "host_scene.hpp"
#include "input_smoke.hpp"
#include "krypton/fixtures/mock_browser.hpp"
#include "krypton/ui/design/tokens.generated.hpp"
#include "krypton_ui/abi.h"
#include "load_simulator.hpp"
#include "page_surface.hpp"

namespace kr::preview {
namespace {

constexpr UINT kRestart = WM_APP + 1;
constexpr UINT kScreenshotComplete = WM_APP + 2;
constexpr UINT kAppearanceLoaded = WM_APP + 3;
constexpr UINT kNewWindow = WM_APP + 4;
constexpr UINT kHostCommand = WM_APP + 5;
constexpr UINT kOpenSiteDataMenu = WM_APP + 6;
constexpr UINT kGlassReady = WM_APP + 7;
constexpr UINT kOpenSplitOptions = WM_APP + 8;
constexpr UINT_PTR kGlassTimer = 4;
enum class HostCommand : WPARAM {
  kFocusAddress,
  kNewTab,
  kReopenTab,
  kCloseTab,
  kNewWindow,
  kReload,
  kHistory,
  kDownloads,
  kFullscreen,
  kCompact,
  kSidebar,
  kRestart,
  kTaskManager
};
constexpr UINT_PTR kFinishTimer = 1;
constexpr UINT_PTR kBenchmarkTimer = 2;
constexpr UINT_PTR kImportTimer = 3;
constexpr int kBenchmarkFrames = 128;
constexpr wchar_t kWindowClass[] = L"KryptonUIPreview";
constexpr auto& kTokens = design::kTokens;

struct Options {
  bool folder_demo = false;
  std::filesystem::path module;
  std::filesystem::path screenshot;
  std::optional<std::uint32_t> glass_mode;
  bool glass_backdrop = false, glass_moving = false;
  bool dark = false;
  std::optional<fixtures::ThemeScheme> scheme;
  std::optional<std::uint32_t> layout_flags;
  bool no_state = false;
  std::optional<std::uint32_t> settings_section;
  bool smoke = false;
  bool smoke_modes = false;
  bool smoke_input = false;
  bool site_data_menu = false;
  bool app_menu = false;
  bool context_menu = false;
  bool task_manager = false, permission_manager = false,
       permission_prompt = false;
  std::optional<std::uint32_t> onboarding_step;
  std::optional<std::uint32_t> menu_material;
  int tabs = 0;
  std::size_t split_panes = 0;
  bool split_options = false;
  bool glance = false;
  UINT exit_after = 0;
};

bool SystemIsDark() {
  using namespace winrt::Windows::UI::ViewManagement;
  const auto background = UISettings().GetColorValue(UIColorType::Background);
  return background.R < 128 && background.G < 128 && background.B < 128;
}

std::filesystem::path AppearancePath() {
  const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
  if (!length)
    return {};
  std::wstring path(length, L'\0');
  const auto copied =
      GetEnvironmentVariableW(L"LOCALAPPDATA", path.data(), length);
  if (!copied || copied >= length)
    return {};
  path.resize(copied);
  return std::filesystem::path(path) / L"KryptonUI" / L"preview-appearance.txt";
}

std::wstring Wide(std::string_view text) {
  if (text.empty())
    return {};
  const int size = MultiByteToWideChar(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      result.data(), size);
  return result;
}

void Log(std::string_view message) noexcept {
  const HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
  if (output && output != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteFile(output, message.data(), static_cast<DWORD>(message.size()),
              &written, nullptr);
    WriteFile(output, "\n", 1, &written, nullptr);
  }
  try {
    const std::wstring wide = Wide(message) + L"\n";
    OutputDebugStringW(wide.c_str());
  } catch (...) {
  }
}

std::filesystem::path ExecutablePath() {
  std::wstring path(32768, L'\0');
  const DWORD size =
      GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!size || size == path.size())
    throw std::runtime_error("Cannot locate the preview executable.");
  path.resize(size);
  return path;
}

std::optional<Options> ParseOptions() {
  int count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments)
    return std::nullopt;
  const auto release =
      std::unique_ptr<wchar_t*, decltype(&LocalFree)>(arguments, LocalFree);
  Options options;
  options.module = ExecutablePath().parent_path() / L"krypton_ui.dll";
  for (int index = 1; index < count; ++index) {
    const std::wstring_view argument(arguments[index]);
    auto value = [&]() -> std::optional<std::wstring_view> {
      if (++index >= count)
        return std::nullopt;
      return arguments[index];
    };
    if (argument == L"--dark") {
      options.dark = true;
      options.scheme = fixtures::ThemeScheme::kDark;
    } else if (argument == L"--light") {
      options.dark = false;
      options.scheme = fixtures::ThemeScheme::kLight;
    } else if (argument == L"--system") {
      options.scheme = fixtures::ThemeScheme::kSystem;
    } else if (argument == L"--no-state") {
      options.no_state = true;
    } else if (argument == L"--layout") {
      const auto mode = value();
      if (!mode)
        return std::nullopt;
      auto& flags =
          options.layout_flags.emplace(options.layout_flags.value_or(0));
      flags &= ~(KR_LAYOUT_TOOLBAR_SINGLE | KR_LAYOUT_TOOLBAR_COLLAPSED |
                 KR_LAYOUT_SIDEBAR_ICON_ONLY | KR_LAYOUT_TABS_CLASSIC |
                 KR_LAYOUT_TABS_COMPACT | KR_LAYOUT_TABS_DYNAMIC);
      if (*mode == L"sidebar")
        flags |= KR_LAYOUT_TOOLBAR_SINGLE;
      else if (*mode == L"collapsed")
        flags |= KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY;
      else if (*mode == L"classic")
        flags |= KR_LAYOUT_TABS_CLASSIC;
      else if (*mode == L"compact")
        flags |= KR_LAYOUT_TABS_COMPACT;
      else if (*mode == L"dynamic")
        flags |= KR_LAYOUT_TABS_DYNAMIC;
      else if (*mode != L"toolbar")
        return std::nullopt;
    } else if (argument == L"--compact") {
      const auto mode = value();
      if (!mode)
        return std::nullopt;
      auto& flags =
          options.layout_flags.emplace(options.layout_flags.value_or(0));
      flags &=
          ~(KR_LAYOUT_COMPACT_HIDE_SIDEBAR | KR_LAYOUT_COMPACT_HIDE_TOOLBAR);
      flags |= KR_LAYOUT_COMPACT_MODE;
      if (*mode == L"sidebar" || *mode == L"both")
        flags |= KR_LAYOUT_COMPACT_HIDE_SIDEBAR;
      if (*mode == L"toolbar" || *mode == L"both")
        flags |= KR_LAYOUT_COMPACT_HIDE_TOOLBAR;
      if (*mode != L"sidebar" && *mode != L"toolbar" && *mode != L"both")
        return std::nullopt;
    } else if (argument == L"--sidebar-right") {
      options.layout_flags =
          options.layout_flags.value_or(0) | KR_LAYOUT_SIDEBAR_RIGHT;
    } else if (argument == L"--onboarding") {
      options.onboarding_step = KR_ONBOARDING_STEP_IGNITION;
      options.no_state = true;
    } else if (argument == L"--onboarding-step") {
      const auto step = value();
      constexpr std::array names{L"ignition", L"shape",   L"color",   L"import",
                                 L"search",   L"privacy", L"default", L"ready"};
      if (!step)
        return std::nullopt;
      const auto found = std::ranges::find(names, *step);
      if (found == names.end())
        return std::nullopt;
      options.onboarding_step =
          static_cast<std::uint32_t>(found - names.begin());
      options.no_state = true;
    } else if (argument == L"--task-manager" ||
               argument == L"--permission-manager" ||
               argument == L"--permission-prompt") {
      options.task_manager = argument == L"--task-manager";
      options.permission_manager = argument == L"--permission-manager";
      options.permission_prompt = argument == L"--permission-prompt";
      options.no_state = true;
    } else if (argument == L"--menu-material") {
      const auto material = value();
      if (material == L"acrylic")
        options.menu_material = KR_MENU_MATERIAL_ACRYLIC;
      else if (material == L"acrylic-thin")
        options.menu_material = KR_MENU_MATERIAL_ACRYLIC_THIN;
      else if (material == L"mica")
        options.menu_material = KR_MENU_MATERIAL_MICA;
      else
        return std::nullopt;
    } else if (argument == L"--app-menu" || argument == L"--context-menu") {
      options.app_menu = argument == L"--app-menu";
      options.context_menu = argument == L"--context-menu";
      options.no_state = true;
    } else if (argument == L"--site-data-menu") {
      options.site_data_menu = true;
      options.no_state = true;
    } else if (argument == L"--settings") {
      options.settings_section = KR_SETTINGS_APPEARANCE;
      options.no_state = true;
    } else if (argument == L"--settings-section") {
      const auto name = value();
      if (!name)
        return std::nullopt;
      static constexpr std::pair<std::wstring_view, std::uint32_t> sections[]{
          {L"general", KR_SETTINGS_GENERAL},
          {L"workspaces", KR_SETTINGS_WORKSPACES},
          {L"appearance", KR_SETTINGS_APPEARANCE},
          {L"privacy", KR_SETTINGS_PRIVACY},
          {L"search", KR_SETTINGS_SEARCH},
          {L"downloads", KR_SETTINGS_DOWNLOADS},
          {L"languages", KR_SETTINGS_LANGUAGES},
          {L"accessibility", KR_SETTINGS_ACCESSIBILITY},
          {L"about", KR_SETTINGS_ABOUT}};
      const auto found = std::ranges::find(
          sections, *name, [](const auto& item) { return item.first; });
      if (found == std::end(sections))
        return std::nullopt;
      options.settings_section = found->second;
      options.no_state = true;
    } else if (argument == L"--glance") {
      options.glance = true;
      options.no_state = true;
    } else if (argument == L"--split") {
      const auto split_count = value();
      if (!split_count || (*split_count != L"2" && *split_count != L"3" &&
                           *split_count != L"4"))
        return std::nullopt;
      options.split_panes = static_cast<std::size_t>((*split_count)[0] - L'0');
      options.no_state = true;
    } else if (argument == L"--split-options") {
      options.split_options = true;
      if (!options.split_panes)
        options.split_panes = 2;
    } else if (argument == L"--folders") {
      options.folder_demo = true;
    } else if (argument == L"--glass-performance") {
      options.glass_mode = KR_GLASS_PERFORMANCE;
    } else if (argument == L"--glass-quality") {
      options.glass_mode = KR_GLASS_QUALITY;
    } else if (argument == L"--glass-backdrop") {
      options.glass_backdrop = true;
    } else if (argument == L"--glass-moving") {
      options.glass_backdrop = options.glass_moving = true;
    } else if (argument == L"--smoke") {
      options.smoke = true;
    } else if (argument == L"--smoke-input") {
      options.smoke = true;
      options.smoke_input = true;
    } else if (argument == L"--smoke-modes") {
      options.smoke = true;
      options.smoke_modes = true;
    } else if (argument == L"--module" || argument == L"--screenshot") {
      const auto path = value();
      if (!path)
        return std::nullopt;
      (argument == L"--module" ? options.module : options.screenshot) = *path;
    } else if (argument == L"--tabs" || argument == L"--exit-after") {
      const auto number = value();
      if (!number || number->empty())
        return std::nullopt;
      unsigned long parsed = 0;
      for (wchar_t digit : *number) {
        if (digit < L'0' || digit > L'9' || parsed > 1000000)
          return std::nullopt;
        parsed = parsed * 10 + static_cast<unsigned long>(digit - L'0');
      }
      if (argument == L"--tabs")
        options.tabs = static_cast<int>(parsed);
      else
        options.exit_after = std::max(1UL, parsed);
    } else {
      return std::nullopt;
    }
  }
  options.module = std::filesystem::absolute(options.module);
  if (options.smoke)
    options.no_state = true;
  if (!options.screenshot.empty())
    options.screenshot = std::filesystem::absolute(options.screenshot);
  return options;
}

COLORREF Color(std::uint32_t argb) noexcept {
  return RGB((argb >> 16) & 255, (argb >> 8) & 255, argb & 255);
}

struct Capture {
  UINT width = 0;
  UINT height = 0;
  std::vector<BYTE> pixels;
};

struct WindowLifetime {
  std::atomic<HWND> window = nullptr;
  std::mutex appearance_mutex;
  std::optional<fixtures::AppearancePrefs> loaded_appearance;
};

// Renders the window itself, including its composited XAML islands. Copying
// screen pixels instead would capture whatever happens to cover the window.
// A transparent frame is the exception: DWM's system backdrop exists only on
// screen, and PrintWindow composes unpainted island pixels onto white.
std::optional<Capture> CaptureClient(HWND window,
                                     bool include_frame = false,
                                     bool from_screen = false) {
  RECT bounds{};
  if (include_frame)
    GetWindowRect(window, &bounds);
  else
    GetClientRect(window, &bounds);
  POINT origin{bounds.left, bounds.top};
  if (!include_frame)
    ClientToScreen(window, &origin);
  Capture capture{static_cast<UINT>(bounds.right - bounds.left),
                  static_cast<UINT>(bounds.bottom - bounds.top),
                  {}};
  if (!capture.width || !capture.height)
    return std::nullopt;
  const HDC screen = GetDC(nullptr);
  const HDC memory = CreateCompatibleDC(screen);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = static_cast<LONG>(capture.width);
  info.bmiHeader.biHeight = -static_cast<LONG>(capture.height);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* pixels = nullptr;
  const HBITMAP bitmap =
      CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  const HGDIOBJ previous = bitmap ? SelectObject(memory, bitmap) : nullptr;
  if (from_screen) {
    SetForegroundWindow(window);
    DwmFlush();
  }
  const bool copied =
      bitmap &&
      (from_screen ? BitBlt(memory, 0, 0, static_cast<int>(capture.width),
                            static_cast<int>(capture.height), screen, origin.x,
                            origin.y, SRCCOPY)
                   : PrintWindow(window, memory, PW_RENDERFULLCONTENT));
  if (copied) {
    const auto* begin = static_cast<const BYTE*>(pixels);
    capture.pixels.assign(
        begin,
        begin + static_cast<std::size_t>(capture.width) * capture.height * 4);
    for (std::size_t index = 3; index < capture.pixels.size(); index += 4)
      capture.pixels[index] = 255;
  }
  if (previous)
    SelectObject(memory, previous);
  if (bitmap)
    DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(nullptr, screen);
  return copied ? std::optional(std::move(capture)) : std::nullopt;
}

HRESULT SavePng(const std::filesystem::path& path, const Capture& capture) {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  try {
    if (!path.parent_path().empty())
      std::filesystem::create_directories(path.parent_path());
    auto factory =
        winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICStream> stream;
    winrt::check_hresult(factory->CreateStream(stream.put()));
    winrt::check_hresult(
        stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,
                                                nullptr, encoder.put()));
    winrt::check_hresult(
        encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
    winrt::check_hresult(frame->Initialize(nullptr));
    winrt::check_hresult(frame->SetSize(capture.width, capture.height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&format));
    if (format != GUID_WICPixelFormat32bppBGRA)
      throw winrt::hresult_error(E_UNEXPECTED);
    winrt::check_hresult(
        frame->WritePixels(capture.height, capture.width * 4,
                           static_cast<UINT>(capture.pixels.size()),
                           const_cast<BYTE*>(capture.pixels.data())));
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(encoder->Commit());
  } catch (...) {
    const HRESULT result = winrt::to_hresult();
    winrt::uninit_apartment();
    return result;
  }
  winrt::uninit_apartment();
  return S_OK;
}

// The loaded WinUI module intentionally stays loaded until process exit. A
// shadow copy keeps the build output writable while a preview is running.
HMODULE LoadModule(const std::filesystem::path& source) {
  std::wstring temporary(MAX_PATH, L'\0');
  const DWORD size =
      GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
  if (!size || size >= temporary.size())
    throw std::runtime_error("Cannot locate the temporary directory.");
  temporary.resize(size);
  const auto directory = std::filesystem::path(temporary) /
                         L"KryptonUI-preview" /
                         std::to_wstring(GetCurrentProcessId());
  std::filesystem::create_directories(directory);
  const auto shadow = directory / L"krypton_ui.dll";
  if (!CopyFileW(source.c_str(), shadow.c_str(), FALSE))
    throw std::runtime_error(
        "Cannot copy krypton_ui.dll. Build the module first.");
  const auto bootstrap =
      source.parent_path() / L"Microsoft.WindowsAppRuntime.Bootstrap.dll";
  if (!CopyFileW(bootstrap.c_str(), (directory / bootstrap.filename()).c_str(),
                 FALSE))
    throw std::runtime_error("Cannot copy the Windows App SDK bootstrap DLL.");
  // Settings images travel with the shadow module during the one-time
  // bootstrap.
  const auto assets = source.parent_path() / L"assets" / L"settings";
  std::error_code asset_error;
  if (std::filesystem::exists(assets, asset_error)) {
    std::filesystem::create_directories(directory / L"assets" / L"settings");
    std::filesystem::copy(
        assets, directory / L"assets" / L"settings",
        std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::overwrite_existing);
  }
  const HMODULE module = LoadLibraryExW(
      shadow.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (!module)
    throw std::runtime_error("Cannot load the preview's shadow module.");
  return module;
}

class Preview {
 public:
  explicit Preview(Options options) : options_(std::move(options)) {
    buffered_paint_ = SUCCEEDED(BufferedPaintInit());
  }

  ~Preview() {
    if (glass_source_)
      glass_source_->Stop(ui_);
    glass_source_.reset();
    load_simulator_.Stop(window_);
    appearance_store_.reset();
    if (watch_stop_)
      SetEvent(watch_stop_);
    if (watcher_.joinable())
      watcher_.join();
    if (watch_stop_)
      CloseHandle(watch_stop_);
    if (screenshot_worker_.joinable())
      screenshot_worker_.join();
    if (browser_)
      browser_->Detach();
    if (ui_.self && ui_.destroy)
      ui_.destroy(ui_.self);
    if (window_ && IsWindow(window_))
      DestroyWindow(window_);
    if (shutdown_ && shutdown_() != KR_OK)
      Log("UI runtime shutdown failed.");
    for (HFONT font : fonts_) {
      if (font)
        DeleteObject(font);
    }
    if (buffered_paint_)
      BufferedPaintUnInit();
  }

  int Run(HINSTANCE instance) {
    const HMODULE module = LoadModule(options_.module);
    shutdown_ =
        std::bit_cast<KrUiShutdownFn>(GetProcAddress(module, "kr_ui_shutdown"));
    const auto supported = std::bit_cast<KrUiIsSupportedFn>(
        GetProcAddress(module, "kr_ui_is_supported"));
    const auto create =
        std::bit_cast<KrUiCreateFn>(GetProcAddress(module, "kr_ui_create"));
    if (!supported || !create || !supported(KR_UI_ABI_VERSION))
      throw std::runtime_error("The UI module is unavailable or incompatible.");

    WNDCLASSEXW window_class{sizeof(WNDCLASSEXW)};
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class))
      throw std::runtime_error("Cannot register the preview window class.");
    const UINT dpi = GetDpiForSystem();
    const float scale = static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI;
    RECT bounds{0, 0, static_cast<LONG>(kTokens.preview.window_width * scale),
                static_cast<LONG>(kTokens.preview.window_height * scale)};
    constexpr DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    POINT pointer{};
    GetCursorPos(&pointer);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    GetMonitorInfoW(MonitorFromPoint(pointer, MONITOR_DEFAULTTOPRIMARY),
                    &monitor);
    const int width = std::min(bounds.right - bounds.left,
                               monitor.rcWork.right - monitor.rcWork.left);
    const int height = std::min(bounds.bottom - bounds.top,
                                monitor.rcWork.bottom - monitor.rcWork.top);
    window_ = CreateWindowExW(
        0, kWindowClass, L"KryptonUI preview", style,
        monitor.rcWork.left +
            (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
        monitor.rcWork.top +
            (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
        width, height, nullptr, nullptr, instance, this);
    if (!window_)
      throw std::runtime_error("Cannot create the preview window.");
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &corners,
                          sizeof(corners));
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER |
                     SWP_FRAMECHANGED);

    if (!options_.no_state) {
      const auto path = AppearancePath();
      if (!path.empty()) {
        appearance_store_ = std::make_unique<AppearanceStore>(
            path, [lifetime = std::weak_ptr(lifetime_)](
                      std::optional<fixtures::AppearancePrefs> preferences) {
              if (const auto live = lifetime.lock()) {
                {
                  const std::lock_guard lock(live->appearance_mutex);
                  live->loaded_appearance = preferences;
                }
                if (const HWND window = live->window.load())
                  PostMessageW(window, kAppearanceLoaded, 0, 0);
              }
            });
      }
    }

    fixtures::MockHostCallbacks callbacks;
    callbacks.window_command = [this](std::uint32_t command) {
      if (command == KR_WINDOW_CLOSE)
        PostMessageW(window_, WM_CLOSE, 0, 0);
      else
        ShowWindow(window_, command == KR_WINDOW_MINIMIZE ? SW_MINIMIZE
                            : IsZoomed(window_)           ? SW_RESTORE
                                                          : SW_MAXIMIZE);
    };
    callbacks.page_changed = [this] {
      if (browser_)
        ApplyWindowMaterial(browser_->transparent_chrome());
      if (glass_source_ && browser_)
        glass_source_->Update(SnapshotHostScene());
      if (browser_)
        SetWindowTextW(window_, browser_->active_page().blank
                                    ? L"Krypton"
                                    : L"KryptonUI preview");
      InvalidateRect(window_, nullptr, FALSE);
    };
    callbacks.focus_page = [this] { SetFocus(window_); };
    callbacks.site_data_anchor_changed = [this](KrRect anchor) {
      if (options_.split_options && !split_options_opened_) {
        split_options_opened_ = true;
        split_options_anchor_ = anchor;
        PostMessageW(window_, kOpenSplitOptions, 0, 0);
      }
      if ((options_.site_data_menu || options_.app_menu ||
           options_.context_menu || options_.task_manager ||
           options_.permission_manager || options_.permission_prompt) &&
          !site_data_opened_) {
        site_data_opened_ = true;
        PostMessageW(window_, kOpenSiteDataMenu, 0, 0);
      }
    };
    callbacks.unhandled_key = [this](const void* message) {
      const auto& native = *static_cast<const MSG*>(message);
      return (options_.smoke_input && input_smoke_.Observe(native)) ||
             QueueAccelerator(native);
    };
    callbacks.load_requested = [this](KrTabId tab, std::uint64_t navigation) {
      return load_simulator_.Start(window_, tab, navigation);
    };
    callbacks.glass_backdrop_requested =
        [this](const KrGlassBackdropRequest& request) {
          if (!ui_.self)
            return;
          if (!request.region_count) {
            if (glass_source_)
              glass_source_->Stop(ui_);
            KillTimer(window_, kGlassTimer);
            return;
          }
          if (!glass_source_)
            glass_source_ =
                std::make_unique<GlassBackdropSource>(window_, kGlassReady);
          glass_source_->Request(request, ui_, SnapshotHostScene());
          if (options_.glass_moving && request.region_count)
            SetTimer(window_, kGlassTimer, 16, nullptr);
          else
            KillTimer(window_, kGlassTimer);
        };
    callbacks.log = [](std::uint32_t, std::string_view message) {
      Log(message);
    };
    callbacks.import_requested = [this](KrImportSourceId) {
      return SetTimer(window_, kImportTimer, 250, nullptr) != 0;
    };
    callbacks.appearance_changed =
        [this](const fixtures::AppearancePrefs& preferences) {
          appearance_edited_ = true;
          if (appearance_store_)
            appearance_store_->Save(preferences);
        };
    callbacks.fullscreen_changed = [this](bool fullscreen) {
      SetFullscreen(fullscreen);
    };
    callbacks.new_window = [this] {
      if (appearance_store_) {
        appearance_store_->FlushAsync([lifetime = std::weak_ptr(lifetime_)] {
          if (const auto live = lifetime.lock()) {
            if (const HWND window = live->window.load())
              PostMessageW(window, kNewWindow, 0, 0);
          }
        });
      } else {
        Restart();
      }
    };
    fixtures::AppearancePrefs initial;
    initial.workspace_colors = {kTokens.theme.default_accent,
                                kTokens.theme_presets[9].color,
                                kTokens.theme_presets[12].color};
    ApplyOverrides(initial);
    browser_ = std::make_unique<fixtures::MockBrowser>(
        fixtures::MockOptions{
            .dark = SystemIsDark(),
            .extra_tabs = options_.tabs,
            .appearance = initial,
            .multi_page = options_.split_panes != 0 || options_.glance,
            .split_panes = options_.split_panes,
            .glance = options_.glance},
        std::move(callbacks));
    const auto initial_theme = browser_->theme_state();
    const KrCreateParams parameters{sizeof(KrCreateParams), KR_UI_ABI_VERSION,
                                    window_, &browser_->table(),
                                    &initial_theme};
    ui_.struct_size = sizeof(ui_);
    const KrResult result = create(&parameters, &ui_);
    if (result != KR_OK)
      throw std::runtime_error(
          std::format("kr_ui_create failed ({})", static_cast<int>(result)));
    browser_->Attach(ui_);
    if (options_.folder_demo)
      browser_->SeedFolderDemo();
    PushWindowState();
    if (options_.onboarding_step &&
        !browser_->ShowOnboarding(*options_.onboarding_step))
      throw std::runtime_error("Could not open native onboarding.");
    if (options_.settings_section &&
        !browser_->ShowSettings(*options_.settings_section))
      throw std::runtime_error("Could not open native settings.");
    CreateFonts();
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    if (!options_.screenshot.empty()) {
      BringWindowToTop(window_);
      SetForegroundWindow(window_);
    }

    if (options_.smoke) {
      // A bounded source of host updates for measuring 120+ distinct renders.
      // Ordinary preview sessions never install this benchmark timer.
      SetTimer(window_, kBenchmarkTimer, USER_TIMER_MINIMUM, nullptr);
    }
    if (!options_.screenshot.empty() || options_.smoke || options_.exit_after) {
      const UINT delay = options_.exit_after
                             ? options_.exit_after
                             : static_cast<UINT>(kTokens.motion.slow_ms * 3);
      if (!options_.smoke || options_.exit_after)
        SetTimer(window_, kFinishTimer, delay, nullptr);
    } else {
      WatchModule();
    }

    MSG message{};
    int status = 0;
    while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
      // Module-owned input runs from WH_GETMESSAGE, including nested loops.
      // The preview handles accelerators only when its placeholder page owns
      // focus; XAML sends unhandled keys through the ABI callback instead.
      if (message.hwnd == window_ && QueueAccelerator(message))
        continue;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (status < 0)
      return 1;
    // The window has closed; flush the last background save before a new
    // process reads it. Active UI work never waits for this filesystem step.
    appearance_store_.reset();
    if (restart_)
      Restart();
    return exit_code_;
  }

 private:
  bool QueueAccelerator(const MSG& message) {
    if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN)
      return false;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    std::optional<HostCommand> command;
    if (message.wParam == VK_ESCAPE && shift && !control && !alt)
      command = HostCommand::kTaskManager;
    else if (message.wParam == VK_F11 ||
             (message.wParam == VK_ESCAPE && browser_->fullscreen()))
      command = HostCommand::kFullscreen;
    else if (control) {
      if (alt && message.wParam == 'C')
        command = HostCommand::kCompact;
      else if (alt && message.wParam == 'S')
        command = HostCommand::kSidebar;
      else if (message.wParam == 'L')
        command = HostCommand::kFocusAddress;
      else if (message.wParam == 'T')
        command = shift ? HostCommand::kReopenTab : HostCommand::kNewTab;
      else if (message.wParam == 'W')
        command = HostCommand::kCloseTab;
      else if (message.wParam == 'R')
        command = shift ? HostCommand::kRestart : HostCommand::kReload;
      else if (message.wParam == 'N')
        command = HostCommand::kNewWindow;
      else if (message.wParam == 'H')
        command = HostCommand::kHistory;
      else if (message.wParam == 'J')
        command = HostCommand::kDownloads;
    }
    return command && PostMessageW(window_, kHostCommand,
                                   static_cast<WPARAM>(*command), 0);
  }

  void ExecuteHostCommand(HostCommand command) {
    using fixtures::PreviewShortcut;
    switch (command) {
      case HostCommand::kTaskManager:
        browser_->ShowTaskManager();
        break;
      case HostCommand::kFocusAddress:
        browser_->FocusAddress(true);
        break;
      case HostCommand::kNewTab:
        browser_->ExecuteShortcut(PreviewShortcut::kNewTab);
        break;
      case HostCommand::kReopenTab:
        browser_->ExecuteShortcut(PreviewShortcut::kReopenTab);
        break;
      case HostCommand::kCloseTab:
        browser_->CloseActiveTab();
        break;
      case HostCommand::kNewWindow:
        browser_->ExecuteShortcut(PreviewShortcut::kNewWindow);
        break;
      case HostCommand::kReload:
        browser_->ExecuteShortcut(PreviewShortcut::kReload);
        break;
      case HostCommand::kHistory:
        browser_->ExecuteShortcut(PreviewShortcut::kHistory);
        break;
      case HostCommand::kDownloads:
        browser_->ExecuteShortcut(PreviewShortcut::kDownloads);
        break;
      case HostCommand::kFullscreen:
        browser_->ToggleFullscreen();
        break;
      case HostCommand::kCompact:
        browser_->ToggleCompactMode();
        break;
      case HostCommand::kSidebar:
        browser_->ToggleSidebar();
        break;
      case HostCommand::kRestart:
        PostMessageW(window_, kRestart, 0, 0);
        break;
    }
  }

  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) noexcept {
    Preview* preview =
        reinterpret_cast<Preview*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      preview = static_cast<Preview*>(
          reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
      preview->window_ = window;
      preview->lifetime_->window.store(window);
      SetWindowLongPtrW(window, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(preview));
    }
    if (preview) {
      try {
        return preview->HandleMessage(message, wparam, lparam);
      } catch (...) {
        preview->exit_code_ = 1;
        Log("Preview window operation failed.");
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
      }
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }

  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (options_.smoke_input)
      input_smoke_.ObservePage(message, wparam, lparam);
    switch (message) {
      case kHostCommand:
        ExecuteHostCommand(static_cast<HostCommand>(wparam));
        return 0;
      case WM_NCCALCSIZE:
        if (wparam) {
          // Custom host frame, like the eventual Views host. Keep maximized
          // client contents inside the monitor work area.
          if (IsZoomed(window_)) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfoW(
                    MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST),
                    &monitor))
              reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam)->rgrc[0] =
                  monitor.rcWork;
          }
          return 0;
        }
        break;
      case kGlassReady:
        if (glass_source_)
          glass_source_->Present(ui_);
        return 0;
      case kOpenSplitOptions:
        if (!browser_->ShowSplitOptions(0, split_options_anchor_)) {
          Log("The split-options preview could not be opened.");
          exit_code_ = 1;
          PostMessageW(window_, WM_CLOSE, 0, 0);
        }
        return 0;
      case kAppearanceLoaded: {
        std::optional<fixtures::AppearancePrefs> loaded;
        {
          const std::lock_guard lock(lifetime_->appearance_mutex);
          loaded = std::exchange(lifetime_->loaded_appearance, std::nullopt);
        }
        if (loaded && browser_ && !appearance_edited_) {
          ApplyOverrides(*loaded);
          browser_->ApplyAppearance(*loaded);
        }
        return 0;
      }
      case kNewWindow:
        Restart();
        return 0;
      case WM_SETTINGCHANGE:
      case WM_THEMECHANGED:
        if (browser_) {
          browser_->SetSystemDark(SystemIsDark());
          InvalidateRect(window_, nullptr, FALSE);
        }
        break;
      case WM_GETMINMAXINFO: {
        const auto result = DefWindowProcW(window_, message, wparam, lparam);
        if (!browser_)
          return result;
        const float scale = Scale();
        auto* sizing = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto& minimum = browser_->shell_metrics();
        // This preview's custom client frame has no nonclient inset.
        sizing->ptMinTrackSize.x = std::max(
            sizing->ptMinTrackSize.x,
            static_cast<LONG>(std::ceil(minimum.minimum_client_width * scale)));
        sizing->ptMinTrackSize.y =
            std::max(sizing->ptMinTrackSize.y,
                     static_cast<LONG>(
                         std::ceil(minimum.minimum_client_height * scale)));
        return result;
      }
      case WM_PAINT:
        Paint();
        return 0;
      case WM_ERASEBKGND:
        return 1;
      case WM_SIZE:
      case WM_ACTIVATE:
        PushWindowState();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
      case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window_, nullptr, rect->left, rect->top,
                     rect->right - rect->left, rect->bottom - rect->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        CreateFonts();
        PushWindowState();
        return 0;
      }
      case WM_NCHITTEST: {
        LRESULT dwm_hit = 0;
        if (DwmDefWindowProc(window_, message, wparam, lparam, &dwm_hit))
          return dwm_hit;
        const auto native_hit =
            DefWindowProcW(window_, message, wparam, lparam);
        if (native_hit == HTMINBUTTON || native_hit == HTMAXBUTTON ||
            native_hit == HTCLOSE || native_hit == HTSYSMENU)
          return native_hit;
        POINT point{static_cast<short>(LOWORD(lparam)),
                    static_cast<short>(HIWORD(lparam))};
        ScreenToClient(window_, &point);
        if (!IsZoomed(window_) && !(browser_ && browser_->fullscreen())) {
          const UINT dpi = GetDpiForWindow(window_);
          const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                             GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
          RECT client{};
          GetClientRect(window_, &client);
          const bool left = point.x < border;
          const bool right = point.x >= client.right - border;
          const bool top = point.y < border;
          const bool bottom = point.y >= client.bottom - border;
          if (top)
            return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
          if (bottom)
            return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
          if (left)
            return HTLEFT;
          if (right)
            return HTRIGHT;
        }

        return HTCLIENT;
      }
      case WM_CONTEXTMENU: {
        if (!browser_ || !KR_HAS_SLOT(&ui_, KrShellUI, request_context_menu) ||
            !ui_.request_context_menu)
          return 0;
        const auto content = browser_->content_bounds();
        float x = content.x + content.width / 2;
        float y = content.y + content.height / 2;
        if (lparam != -1) {
          POINT point{static_cast<short>(LOWORD(lparam)),
                      static_cast<short>(HIWORD(lparam))};
          if (!ScreenToClient(window_, &point))
            return 0;
          x = static_cast<float>(point.x) / Scale();
          y = static_cast<float>(point.y) / Scale();
        }
        if (x >= content.x && y >= content.y && x < content.x + content.width &&
            y < content.y + content.height)
          ui_.request_context_menu(ui_.self, KR_MENU_PAGE, 0, {x, y, 0, 0});
        return 0;
      }
      case kOpenSiteDataMenu: {
        if (options_.smoke) {
          // The bounded input check needs an active owner. A hidden process
          // launch can override the first ShowWindow call during startup.
          ShowWindow(window_, SW_SHOW);
          SetActiveWindow(window_);
          SetFocus(window_);
        }
        if (options_.task_manager) {
          browser_->ShowTaskManager();
          return 0;
        }
        if (options_.permission_manager) {
          browser_->ShowSitePermissions();
          return 0;
        }
        if (options_.permission_prompt) {
          browser_->ShowPermissionPrompt();
          return 0;
        }
        const auto anchor = options_.app_menu ? browser_->app_menu_anchor()
                                              : browser_->site_data_anchor();
        const auto kind = options_.app_menu       ? KR_MENU_APP
                          : options_.context_menu ? KR_MENU_PAGE
                                                  : KR_MENU_SITE_DATA;
        ui_.request_context_menu(ui_.self, kind, 0, anchor);
        return 0;
      }
      case WM_LBUTTONDOWN:
        SetFocus(window_);
        PreviewPageClick(lparam);
        return 0;
      case WM_TIMER:
        if (wparam == kGlassTimer) {
          ++glass_phase_;
          if (glass_source_)
            glass_source_->Update(SnapshotHostScene());
          InvalidateRect(window_, nullptr, FALSE);
          return 0;
        }
        if (wparam == kImportTimer) {
          if (!browser_->AdvanceImport())
            KillTimer(window_, kImportTimer);
          return 0;
        }
        if (wparam == LoadSimulator::kTimer) {
          load_simulator_.Step(window_, *browser_);
          return 0;
        }
        if (wparam == kBenchmarkTimer) {
          if ((options_.task_manager || options_.permission_manager ||
               options_.permission_prompt) &&
              !ExerciseBrowserTools()) {
            exit_code_ = 1;
            KillTimer(window_, kBenchmarkTimer);
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return 0;
          }
          if ((options_.site_data_menu || options_.context_menu) &&
              !ExerciseMenu()) {
            exit_code_ = 1;
            KillTimer(window_, kBenchmarkTimer);
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return 0;
          }
          if (options_.smoke_input &&
              !input_smoke_.Advance(static_cast<unsigned>(benchmark_frame_),
                                    window_, *browser_)) {
            Log(input_smoke_.Failure());
            exit_code_ = 1;
            KillTimer(window_, kBenchmarkTimer);
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return 0;
          }
          if (options_.smoke_modes && benchmark_frame_ % 4 == 0)
            ExerciseMode(static_cast<unsigned>(benchmark_frame_ / 4));
          if ((!options_.smoke_input || input_smoke_.Complete()) &&
              (!(options_.site_data_menu || options_.context_menu ||
                 options_.task_manager || options_.permission_manager ||
                 options_.permission_prompt) ||
               benchmark_frame_ > 70))
            browser_->CycleActiveTab();
          ++benchmark_frame_;
          const bool finished = options_.smoke_input
                                    ? input_smoke_.Complete()
                                    : benchmark_frame_ == kBenchmarkFrames;
          if (finished) {
            KillTimer(window_, kBenchmarkTimer);
            if (!options_.exit_after)
              SetTimer(window_, kFinishTimer,
                       static_cast<UINT>(kTokens.motion.slow_ms * 3), nullptr);
          }
          return 0;
        }
        if (wparam == kFinishTimer) {
          KillTimer(window_, kFinishTimer);
          Finish();
          return 0;
        }
        break;
      case kScreenshotComplete:
        exit_code_ = FAILED(static_cast<HRESULT>(lparam)) ? 1 : 0;
        if (exit_code_)
          Log("PNG screenshot encoding failed.");
        else
          Log("PNG screenshot saved.");
        PostMessageW(window_, WM_CLOSE, 0, 0);
        return 0;
      case kRestart:
        restart_ = true;
        PostMessageW(window_, WM_CLOSE, 0, 0);
        return 0;
      case WM_CLOSE:
        KillTimer(window_, kGlassTimer);
        if (glass_source_)
          glass_source_->Stop(ui_);
        glass_source_.reset();
        // Destroy the island before its host HWND. Never FreeLibrary: WinUI's
        // process-wide Application prevents safely unloading the module.
        if (browser_)
          browser_->Detach();
        if (ui_.self && ui_.destroy)
          ui_.destroy(std::exchange(ui_.self, nullptr));
        lifetime_->window.store(nullptr);
        DestroyWindow(window_);
        return 0;
      case WM_DESTROY:
        PostQuitMessage(exit_code_);
        return 0;
      default:
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
  }

  bool OwnsToolWindow(HWND candidate) const noexcept {
    // WinUI tool windows are owned overlapped windows. Follow their owner
    // links explicitly; GA_ROOTOWNER can return the tool window itself.
    for (unsigned depth = 0; candidate && depth < 32; ++depth) {
      if (candidate == window_)
        return true;
      const auto owner = GetWindow(candidate, GW_OWNER);
      candidate = owner ? owner : GetParent(candidate);
    }
    return false;
  }

  bool ExerciseBrowserTools() {
    if (benchmark_frame_ == 20) {
      const auto active = GetActiveWindow();
      const bool open = options_.task_manager ? browser_->task_manager_open()
                        : options_.permission_manager
                            ? browser_->permission_manager_open()
                            : browser_->permission_prompt_open();
      if (!open || !active || (active != window_ && !OwnsToolWindow(active))) {
        Log("Browser tools smoke: the requested surface did not become "
            "active.");
        return false;
      }
    }
    if (benchmark_frame_ == 40) {
      if (options_.permission_prompt) {
        const auto focus = GetFocus();
        if (!focus) {
          Log("Permission prompt did not acquire focus.");
          return false;
        }
        PostMessageW(focus, WM_KEYDOWN, VK_ESCAPE, 1);
        PostMessageW(focus, WM_KEYUP, VK_ESCAPE, LPARAM{0xc0000001});
      } else {
        const auto active = GetActiveWindow();
        if (!active || active == window_) {
          Log("Browser tool lost its native window.");
          return false;
        }
        PostMessageW(active, WM_CLOSE, 0, 0);
      }
    }
    if (benchmark_frame_ == 60) {
      const bool open = options_.task_manager ? browser_->task_manager_open()
                        : options_.permission_manager
                            ? browser_->permission_manager_open()
                            : browser_->permission_prompt_open();
      if (open) {
        Log("Browser tool dismissal was not delivered to its host.");
        return false;
      }
      Log("Native browser-tool open/focus/dismiss check passed.");
    }
    return true;
  }

  bool ExerciseMenu() {
    // Site data: Bookmark -> Tab -> Manage. Three Right keys in the context
    // strip reach Bookmark regardless of navigation availability, then Tab
    // reaches New Tab. Dispatch must precede closing the request.
    if (benchmark_frame_ == 20 || benchmark_frame_ == 40 ||
        (options_.context_menu && benchmark_frame_ == 30)) {
      const auto focus = GetFocus();
      if (!focus || (GetAncestor(focus, GA_ROOTOWNER) != window_ &&
                     !IsChild(window_, focus))) {
        Log("Menu smoke: popup did not acquire native keyboard focus.");
        return false;
      }
      const WPARAM key = benchmark_frame_ == 40 ? VK_RETURN
                         : options_.context_menu && benchmark_frame_ == 20
                             ? VK_RIGHT
                             : VK_TAB;
      const int repeats =
          options_.context_menu && benchmark_frame_ == 20 ? 3 : 1;
      for (int repeat = 0; repeat < repeats; ++repeat) {
        PostMessageW(focus, WM_KEYDOWN, key, 1);
        PostMessageW(focus, WM_KEYUP, key, LPARAM{0xc0000001});
      }
    }
    if (benchmark_frame_ == 60 &&
        (options_.context_menu
             ? browser_->active_page().url != "krypton://newtab"
             : browser_->active_page().url != "krypton://extensions")) {
      Log("Menu smoke: the selected command did not dispatch through the open "
          "request.");
      return false;
    }
    if (benchmark_frame_ == 65)
      Log("Native menu keyboard/command check passed.");
    return true;
  }

  float Scale() const noexcept {
    return static_cast<float>(GetDpiForWindow(window_)) /
           USER_DEFAULT_SCREEN_DPI;
  }

  void ApplyOverrides(fixtures::AppearancePrefs& preferences) const {
    if (options_.glass_mode)
      preferences.glass_mode = *options_.glass_mode;
    if (options_.menu_material)
      preferences.menu_material = *options_.menu_material;
    if (options_.scheme)
      preferences.scheme = *options_.scheme;
    if (options_.layout_flags)
      preferences.layout_flags = *options_.layout_flags;
  }

  void SetFullscreen(bool fullscreen) {
    if (fullscreen) {
      fullscreen_placement_.length = sizeof(fullscreen_placement_);
      GetWindowPlacement(window_, &fullscreen_placement_);
      fullscreen_style_ = GetWindowLongPtrW(window_, GWL_STYLE);
      MONITORINFO monitor{sizeof(MONITORINFO)};
      if (!GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST),
                           &monitor))
        return;
      SetWindowLongPtrW(window_, GWL_STYLE,
                        fullscreen_style_ & ~(WS_CAPTION | WS_THICKFRAME));
      SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left,
                   monitor.rcMonitor.top,
                   monitor.rcMonitor.right - monitor.rcMonitor.left,
                   monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                   SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
      SetWindowLongPtrW(window_, GWL_STYLE, fullscreen_style_);
      SetWindowPlacement(window_, &fullscreen_placement_);
      SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER |
                       SWP_FRAMECHANGED);
    }
    PushWindowState();
  }

  void ExerciseMode(unsigned index) {
    auto preferences = browser_->appearance();
    if (index < 24) {
      preferences.layout_flags =
          index % 3 == 0 ? 0u
          : index % 3 == 1
              ? KR_LAYOUT_TOOLBAR_SINGLE
              : KR_LAYOUT_TOOLBAR_COLLAPSED | KR_LAYOUT_SIDEBAR_ICON_ONLY;
      const auto compact = index / 3 % 4;
      if (compact) {
        preferences.layout_flags |= KR_LAYOUT_COMPACT_MODE;
        if (compact != 2)
          preferences.layout_flags |= KR_LAYOUT_COMPACT_HIDE_SIDEBAR;
        if (compact != 1)
          preferences.layout_flags |= KR_LAYOUT_COMPACT_HIDE_TOOLBAR;
      }
      if (index >= 12)
        preferences.layout_flags |= KR_LAYOUT_SIDEBAR_RIGHT;
    } else if (index < 30) {
      preferences.layout_flags = 0;
      preferences.scheme = static_cast<fixtures::ThemeScheme>((index - 24) % 3);
    } else {
      browser_->ToggleFullscreen();
    }
    browser_->ApplyAppearance(preferences);
  }

  void PushWindowState() {
    if (!browser_ || !ui_.self)
      return;
    RECT client{};
    GetClientRect(window_, &client);
    const float scale = Scale();
    browser_->SetWindowState(static_cast<float>(client.right) / scale,
                             static_cast<float>(client.bottom) / scale, scale,
                             GetActiveWindow() == window_, IsZoomed(window_));
  }

  void CreateFonts() {
    const std::wstring family = Wide(kTokens.type.family);
    const std::array sizes{kTokens.type.size, kTokens.preview.page_title_size,
                           kTokens.preview.page_heading_size};
    for (std::size_t index = 0; index < fonts_.size(); ++index) {
      if (fonts_[index])
        DeleteObject(fonts_[index]);
      fonts_[index] = CreateFontW(
          -static_cast<int>(std::lround(sizes[index] * Scale())), 0, 0, 0,
          index == 0 ? FW_NORMAL : FW_SEMIBOLD, FALSE, FALSE, FALSE,
          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
          CLEARTYPE_QUALITY, DEFAULT_PITCH, family.c_str());
    }
  }

  void PreviewPageClick(LPARAM position) {
    if (!browser_ || (!options_.split_panes && !options_.glance))
      return;
    const float x =
        static_cast<float>(static_cast<short>(LOWORD(position))) / Scale();
    const float y =
        static_cast<float>(static_cast<short>(HIWORD(position))) / Scale();
    const auto contains = [x, y](KrRect bounds) {
      return x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width &&
             y < bounds.y + bounds.height;
    };
    const auto layout = browser_->page_layout();
    if (layout.count && layout.surfaces[layout.count - 1].glance &&
        contains(layout.glance.toolbar)) {
      const float fraction =
          (x - layout.glance.toolbar.x) / layout.glance.toolbar.width;
      if (fraction < 1.0f / 3)
        browser_->CloseGlance();
      else
        (void)browser_->PromoteGlance(fraction >= 2.0f / 3);
      return;
    }
    for (std::size_t i = layout.count; i > 0; --i) {
      if (contains(layout.surfaces[i - 1].bounds)) {
        const auto& host = browser_->table();
        host.activate_tab(host.self, layout.surfaces[i - 1].tab);
        return;
      }
    }
  }

  HostScene SnapshotHostScene() const {
    HostScene scene;
    GetClientRect(window_, &scene.client);
    scene.scale = Scale();
    scene.dark = browser_ ? browser_->dark() : options_.dark;
    scene.checkerboard = options_.glass_backdrop;
    scene.phase = glass_phase_;
    const auto& palette = scene.dark ? kTokens.dark : kTokens.light;
    scene.gradient = {palette.gradient_base, palette.gradient_base};
    if (browser_) {
      scene.has_browser = true;
      scene.content = browser_->content_bounds();
      scene.radius = browser_->content_radius();
      scene.gradient = browser_->background_gradient();
      scene.theme = browser_->background_theme();
      scene.generation = browser_->background_theme_generation();
      scene.layout = browser_->page_layout();
      scene.zoom = browser_->page_zoom();
      scene.transparent = browser_->transparent_chrome();
    }
    return scene;
  }
  // Quality glass: like the browser host, leave the frame unpainted over
  // DWM's acrylic system backdrop and keep only the page opaque.
  void ApplyWindowMaterial(bool transparent) {
    if (transparent_ == transparent)
      return;
    transparent_ = transparent;
    const MARGINS margins = transparent ? MARGINS{-1, -1, -1, -1} : MARGINS{};
    DwmExtendFrameIntoClientArea(window_, &margins);
    const DWM_SYSTEMBACKDROP_TYPE backdrop =
        transparent ? DWMSBT_TRANSIENTWINDOW : DWMSBT_AUTO;
    DwmSetWindowAttribute(window_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                          sizeof(backdrop));
    InvalidateRect(window_, nullptr, FALSE);
  }
  void Paint() {
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(window_, &paint);
    const auto scene = SnapshotHostScene();
    if (!scene.transparent) {
      PaintHostScene(dc, scene, page_surface_, fonts_);
      EndPaint(window_, &paint);
      return;
    }
    // BeginPaint clips out the islands because of WS_CLIPCHILDREN. Clear the
    // parent surface beneath them too, or their transparency exposes stale
    // opaque pixels instead of the DWM backdrop. Omit DCX_USESTYLE and
    // DCX_CLIPCHILDREN so this DC covers the entire client area.
    const HDC frame = GetDCEx(window_, nullptr, DCX_CACHE | DCX_CLIPSIBLINGS);
    if (!frame) {
      EndPaint(window_, &paint);
      return;
    }
    // GDI leaves alpha undefined. Paint into a cleared 32-bit buffer, then
    // make only the rounded page opaque; the frame keeps alpha zero.
    BP_PAINTPARAMS params{sizeof(params), BPPF_ERASE | BPPF_NOCLIP, nullptr,
                          nullptr};
    HDC target = nullptr;
    if (const HPAINTBUFFER buffer = BeginBufferedPaint(
            frame, &scene.client, BPBF_TOPDOWNDIB, &params, &target)) {
      PaintHostScene(target, scene, page_surface_, fonts_);
      RGBQUAD* bits = nullptr;
      int stride = 0;
      if (SUCCEEDED(GetBufferedPaintBits(buffer, &bits, &stride)))
        kr::preview::OpaquePage(
            {bits, static_cast<std::size_t>(stride) *
                       static_cast<std::size_t>(scene.client.bottom)},
            stride, scene);
      EndBufferedPaint(buffer, TRUE);
    }
    ReleaseDC(window_, frame);
    EndPaint(window_, &paint);
  }

  void Finish() {
    if (options_.screenshot.empty()) {
      if (options_.smoke_input) {
        if (!input_smoke_.Complete()) {
          Log(std::format("Input smoke did not complete (step {}).",
                          input_smoke_.PendingStep()));
          exit_code_ = 1;
        } else {
          Log("Workspace editor, native key fallback, page key/text input, "
              "fullscreen and "
              "maximize/restore smoke "
              "completed.");
        }
      }
      Log("Preview smoke check completed.");
      PostMessageW(window_, WM_CLOSE, 0, 0);
      return;
    }
    HWND capture_target = window_;
    if (options_.task_manager || options_.permission_manager) {
      const auto active = GetActiveWindow();
      if (!active || active == window_ || !OwnsToolWindow(active)) {
        Log("The requested native browser-tool window did not open.");
        exit_code_ = 1;
        PostMessageW(window_, WM_CLOSE, 0, 0);
        return;
      }
      capture_target = active;
    }
    auto capture = CaptureClient(capture_target, capture_target != window_,
                                 capture_target == window_ && transparent_);
    if (!capture) {
      exit_code_ = 1;
      Log("Cannot capture the preview client area.");
      PostMessageW(window_, WM_CLOSE, 0, 0);
      return;
    }
    screenshot_worker_ =
        std::thread([lifetime = std::weak_ptr(lifetime_),
                     path = options_.screenshot, pixels = std::move(*capture)] {
          HRESULT result = E_FAIL;
          try {
            result = SavePng(path, pixels);
          } catch (...) {
          }
          if (const auto live = lifetime.lock()) {
            if (const HWND window = live->window.load())
              PostMessageW(window, kScreenshotComplete, 0,
                           static_cast<LPARAM>(result));
          }
        });
  }

  void WatchModule() {
    watch_stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!watch_stop_)
      return;
    const HANDLE stop = watch_stop_;
    watcher_ = std::thread([lifetime = std::weak_ptr(lifetime_), stop,
                            source = options_.module] {
      const HANDLE directory = CreateFileW(
          source.parent_path().c_str(), FILE_LIST_DIRECTORY,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
          nullptr);
      if (directory == INVALID_HANDLE_VALUE)
        return;
      OVERLAPPED overlapped{};
      overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (!overlapped.hEvent) {
        CloseHandle(directory);
        return;
      }
      alignas(FILE_NOTIFY_INFORMATION) std::array<BYTE, 4096> buffer{};
      const std::array handles{stop, overlapped.hEvent};
      while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
        ResetEvent(overlapped.hEvent);
        if (!ReadDirectoryChangesW(
                directory, buffer.data(), static_cast<DWORD>(buffer.size()),
                FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                nullptr, &overlapped, nullptr))
          break;
        if (WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                   handles.data(), FALSE,
                                   INFINITE) != WAIT_OBJECT_0 + 1) {
          CancelIoEx(directory, &overlapped);
          DWORD transferred = 0;
          GetOverlappedResult(directory, &overlapped, &transferred, TRUE);
          break;
        }
        DWORD transferred = 0;
        if (!GetOverlappedResult(directory, &overlapped, &transferred, FALSE))
          break;
        bool changed = false;
        for (DWORD offset = 0; offset < transferred;) {
          const auto* notification =
              reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() +
                                                               offset);
          const std::wstring name(
              notification->FileName,
              notification->FileNameLength / sizeof(wchar_t));
          changed |= _wcsicmp(name.c_str(), source.filename().c_str()) == 0;
          if (!notification->NextEntryOffset)
            break;
          offset += notification->NextEntryOffset;
        }
        // Debounce a linker's burst of writes without polling browser state.
        if (changed && WaitForSingleObject(
                           stop, static_cast<DWORD>(kTokens.motion.slow_ms)) ==
                           WAIT_TIMEOUT) {
          if (const auto live = lifetime.lock()) {
            if (const HWND window = live->window.load())
              PostMessageW(window, kRestart, 0, 0);
          }
          break;
        }
      }
      CloseHandle(overlapped.hEvent);
      CloseHandle(directory);
    });
  }

  void Restart() const {
    std::wstring command_line = GetCommandLineW();
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(ExecutablePath().c_str(), command_line.data(), nullptr,
                        nullptr, FALSE, 0, nullptr, nullptr, &startup,
                        &process)) {
      Log("Cannot restart the preview process.");
      return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }

  Options options_;
  InputSmoke input_smoke_;
  HWND window_ = nullptr;
  kr::preview::PageSurface page_surface_;
  LoadSimulator load_simulator_;
  KrShellUI ui_{};
  std::unique_ptr<GlassBackdropSource> glass_source_;
  unsigned glass_phase_ = 0;
  bool transparent_ = false, buffered_paint_ = false;
  KrUiShutdownFn shutdown_ = nullptr;
  std::unique_ptr<fixtures::MockBrowser> browser_;
  std::unique_ptr<AppearanceStore> appearance_store_;
  bool appearance_edited_ = false;
  WINDOWPLACEMENT fullscreen_placement_{};
  LONG_PTR fullscreen_style_ = 0;
  std::array<HFONT, 3> fonts_{};
  std::shared_ptr<WindowLifetime> lifetime_ =
      std::make_shared<WindowLifetime>();
  HANDLE watch_stop_ = nullptr;
  std::thread watcher_;
  std::thread screenshot_worker_;
  int exit_code_ = 0;
  int benchmark_frame_ = 0;
  bool site_data_opened_ = false;
  bool split_options_opened_ = false;
  KrRect split_options_anchor_{};
  bool restart_ = false;
};

}  // namespace
}  // namespace kr::preview

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  try {
    // Chromium supplies an STA UI thread before loading the module; the
    // standalone host must establish the same apartment before bootstrap.
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    auto options = kr::preview::ParseOptions();
    if (!options) {
      kr::preview::Log(
          "Usage: kr_preview [--module dll] [--dark|--light] [--tabs n] "
          "[--layout sidebar|toolbar|collapsed|classic|compact|dynamic] "
          "[--compact "
          "sidebar|toolbar|both] "
          "[--sidebar-right] [--system] [--no-state] "
          "[--glass-performance|--glass-quality] "
          "[--folders] "
          "[--glass-backdrop|--glass-moving] "
          "[--site-data-menu] "
          "[--app-menu|--context-menu] [--menu-material "
          "acrylic|acrylic-thin|mica] "
          "[--task-manager|--permission-manager|--permission-prompt] "
          "[--onboarding] "
          "[--onboarding-step "
          "ignition|shape|color|import|search|privacy|default|ready] "
          "[--screenshot file.png] [--smoke|--smoke-modes|--smoke-input] "
          "[--settings|--settings-section "
          "general|workspaces|appearance|privacy|search|downloads|languages|"
          "accessibility|about] [--exit-after "
          "milliseconds]");
      return 2;
    }
    kr::preview::Preview preview(std::move(*options));
    return preview.Run(instance);
  } catch (const std::exception& error) {
    kr::preview::Log(error.what());
    return 1;
  } catch (...) {
    kr::preview::Log("Unexpected preview startup failure.");
    return 1;
  }
}
