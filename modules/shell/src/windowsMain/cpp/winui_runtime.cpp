#include "winui_runtime.hpp"

#include "native_input.hpp"

// clang-format off
#include <windows.h>
#include <appmodel.h>
#include <MddBootstrap.h>
#include <roapi.h>
// clang-format on
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <filesystem>
#include <format>
#include <vector>

namespace kr::windows {
namespace {

using namespace winrt::Microsoft::UI::Xaml;
thread_local const char* startup_stage = "bootstrap";

struct XamlApp : ApplicationT<XamlApp, Markup::IXamlMetadataProvider> {
  XamlApp() {
    // Initialize DXamlCore while constructing the custom Application and
    // before touching any control resources (the XAML Islands contract).
    startup_stage = "XAML island manager";
    manager_ = Hosting::WindowsXamlManager::InitializeForCurrentThread();
    startup_stage = "XAML control resources";
    Resources().MergedDictionaries().Append(Controls::XamlControlsResources{});
  }

  Markup::IXamlType GetXamlType(
      const winrt::Windows::UI::Xaml::Interop::TypeName& type) {
    return provider_.GetXamlType(type);
  }
  Markup::IXamlType GetXamlType(const winrt::hstring& name) {
    return provider_.GetXamlType(name);
  }
  winrt::com_array<Markup::XmlnsDefinition> GetXmlnsDefinitions() {
    return provider_.GetXmlnsDefinitions();
  }

  void CloseManager() { manager_.Close(); }

 private:
  XamlTypeInfo::XamlControlsXamlMetaDataProvider provider_;
  Hosting::WindowsXamlManager manager_{nullptr};
};

using Bootstrap = HRESULT(WINAPI*)(UINT32,
                                   PCWSTR,
                                   PACKAGE_VERSION,
                                   MddBootstrapInitializeOptions) noexcept;
using PreTranslate = BOOL(WINAPI*)(const MSG*);

struct Runtime {
  HMODULE bootstrap = nullptr;
  HMODULE windowing = nullptr;
  PreTranslate pre_translate = nullptr;
  winrt::Microsoft::UI::Dispatching::DispatcherQueueController queue{nullptr};
  Application application{nullptr};
  bool bootstrapped = false;
  bool terminated = false;
  bool shutting_down = false;
  DWORD ui_thread = 0;
};

Runtime& State() {
  // XAML has process lifetime. Deliberately do not uninitialize or unload it.
  static Runtime* runtime = new Runtime;
  return *runtime;
}

std::filesystem::path ModuleDirectory() {
  HMODULE module = nullptr;
  winrt::check_bool(
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module));
  std::wstring path(32768, L'\0');
  const auto length =
      GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  winrt::check_bool(length != 0 && length < path.size());
  path.resize(length);
  return std::filesystem::path(path).parent_path();
}

Error ToError(const winrt::hresult_error& error) {
  return {ErrorCode::kPlatform,
          std::format("Windows App SDK {}: {} (0x{:08x})", startup_stage,
                      winrt::to_string(error.message()),
                      static_cast<std::uint32_t>(error.code().value))};
}

void BootstrapRuntime() {
  auto& runtime = State();
  if (runtime.bootstrapped)
    return;
  const auto dll =
      ModuleDirectory() / L"Microsoft.WindowsAppRuntime.Bootstrap.dll";
  runtime.bootstrap = LoadLibraryExW(
      dll.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  winrt::check_bool(runtime.bootstrap != nullptr);
  const auto initialize = reinterpret_cast<Bootstrap>(
      GetProcAddress(runtime.bootstrap, "MddBootstrapInitialize2"));
  winrt::check_bool(initialize != nullptr);
  winrt::check_hresult(initialize(KR_WINAPPSDK_RUNTIME_MAJOR_MINOR, nullptr,
                                  PACKAGE_VERSION{},
                                  MddBootstrapInitializeOptions_None));
  runtime.bootstrapped = true;
}

void ResolvePreTranslate() {
  auto& runtime = State();
  HMODULE xaml = GetModuleHandleW(L"Microsoft.UI.Xaml.dll");
  winrt::check_bool(xaml != nullptr);
  std::wstring path(32768, L'\0');
  const auto length =
      GetModuleFileNameW(xaml, path.data(), static_cast<DWORD>(path.size()));
  winrt::check_bool(length != 0 && length < path.size());
  path.resize(length);
  const auto dll = std::filesystem::path(path).parent_path() /
                   L"Microsoft.UI.Windowing.Core.dll";
  runtime.windowing = LoadLibraryExW(
      dll.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  winrt::check_bool(runtime.windowing != nullptr);
  runtime.pre_translate = reinterpret_cast<PreTranslate>(
      GetProcAddress(runtime.windowing, "ContentPreTranslateMessage"));
  winrt::check_bool(runtime.pre_translate != nullptr);
}

}  // namespace

Result<void> CheckRuntimeSupport() noexcept {
  if (State().terminated || State().shutting_down)
    return Fail(ErrorCode::kUnsupported, "The UI runtime has shut down");
  try {
    BootstrapRuntime();
    return {};
  } catch (const winrt::hresult_error& error) {
    return std::unexpected(ToError(error));
  } catch (...) {
    return Fail(ErrorCode::kPlatform, "Windows App SDK bootstrap failed");
  }
}

Result<void> EnsureRuntime() noexcept {
  if (State().terminated || State().shutting_down)
    return Fail(ErrorCode::kUnsupported, "The UI runtime has shut down");
  if (State().ui_thread && State().ui_thread != GetCurrentThreadId())
    return Fail(ErrorCode::kInvalidArgument,
                "The UI runtime belongs to another thread");
  State().ui_thread = GetCurrentThreadId();
  try {
    BootstrapRuntime();
    auto& runtime = State();
    if (runtime.application && runtime.pre_translate)
      return {};
    // The browser owns apartment lifetime; RoInitialize is balanced only if
    // this module initialized it. XAML's process lifetime keeps it initialized.
    if (!runtime.application) {
      startup_stage = "STA apartment";
      winrt::check_hresult(RoInitialize(RO_INIT_SINGLETHREADED));
      startup_stage = "dispatcher queue";
      if (!winrt::Microsoft::UI::Dispatching::DispatcherQueue::
              GetForCurrentThread()) {
        runtime.queue = winrt::Microsoft::UI::Dispatching::
            DispatcherQueueController::CreateOnCurrentThread();
      }
      startup_stage = "XAML application";
      runtime.application = winrt::make<XamlApp>();
    }
    startup_stage = "message pretranslation";
    ResolvePreTranslate();
    return {};
  } catch (const winrt::hresult_error& error) {
    return std::unexpected(ToError(error));
  } catch (...) {
    return Fail(ErrorCode::kPlatform, "WinUI initialization failed");
  }
}

Result<void> ShutdownRuntime() noexcept {
  auto& runtime = State();
  if (runtime.ui_thread && runtime.ui_thread != GetCurrentThreadId())
    return Fail(ErrorCode::kInvalidArgument,
                "Runtime shutdown requires its UI thread");
  if (runtime.terminated)
    return {};
  if (runtime.shutting_down)
    return Fail(ErrorCode::kInvalidArgument,
                "Runtime shutdown is already in progress");
  // ShutdownQueue dispatches framework work. Prevent creation or another
  // shutdown from reentering while that terminal lifecycle is in progress.
  runtime.shutting_down = true;
  try {
    // The native shutdown method runs the framework's final dispatcher work.
    // Only the controller we created is ours to stop; a host-owned queue keeps
    // its own lifecycle and WinUI observes its framework shutdown notification.
    if (runtime.application)
      winrt::get_self<XamlApp>(
          runtime.application.as<Markup::IXamlMetadataProvider>())
          ->CloseManager();
    if (runtime.queue)
      runtime.queue.ShutdownQueue();
    // Keep the one Application object for process lifetime. Its XAML manager
    // is closed and its dispatcher has drained; the DLL still cannot unload.
    runtime.queue = nullptr;
    runtime.pre_translate = nullptr;
    runtime.terminated = true;
    runtime.shutting_down = false;
    return {};
  } catch (const winrt::hresult_error& error) {
    return std::unexpected(ToError(error));
  } catch (...) {
    return Fail(ErrorCode::kPlatform, "UI runtime shutdown failed");
  }
}

bool PreTranslateMessage(const void* message) noexcept {
  if (!message || !State().pre_translate ||
      !win::NativeInputTarget::Contains(static_cast<const MSG*>(message)->hwnd))
    return false;
  return State().pre_translate(static_cast<const MSG*>(message)) != FALSE;
}

}  // namespace kr::windows
