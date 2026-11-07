#include "native_frame.hpp"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

namespace kr::win {
namespace {
LRESULT NativeHit(FrameHit hit) noexcept {
  switch (hit) {
    case FrameHit::kCaption:
      return HTCAPTION;
    case FrameHit::kLeft:
      return HTLEFT;
    case FrameHit::kRight:
      return HTRIGHT;
    case FrameHit::kTop:
      return HTTOP;
    case FrameHit::kBottom:
      return HTBOTTOM;
    case FrameHit::kTopLeft:
      return HTTOPLEFT;
    case FrameHit::kTopRight:
      return HTTOPRIGHT;
    case FrameHit::kBottomLeft:
      return HTBOTTOMLEFT;
    case FrameHit::kBottomRight:
      return HTBOTTOMRIGHT;
    case FrameHit::kClient:
      return HTCLIENT;
  }
  return HTCLIENT;
}
}  // namespace

NativeFrame::~NativeFrame() {
  Reset();
}

Result<void> NativeFrame::Initialize(HWND parent,
                                     void* context,
                                     SettingsChanged settings,
                                     ActivationChanged activation) {
  if (parent_ || !IsWindow(parent) ||
      GetWindowThreadProcessId(parent, nullptr) != GetCurrentThreadId())
    return Fail(ErrorCode::kInvalidArgument,
                "Frame requires an owning UI thread");
  if (!SetWindowSubclass(parent, ParentProcedure,
                         reinterpret_cast<UINT_PTR>(this),
                         reinterpret_cast<DWORD_PTR>(this)))
    return Fail(ErrorCode::kPlatform, "Could not subclass the host frame");
  parent_ = parent;
  context_ = context;
  settings_ = settings;
  activation_ = activation;
  RefreshMetrics();
  return {};
}

Result<void> NativeFrame::AttachChild(HWND child) {
  if (std::ranges::find(children_, child) != children_.end())
    return {};
  children_.push_back(child);
  if (!SetWindowSubclass(child, IslandProcedure,
                         reinterpret_cast<UINT_PTR>(this),
                         reinterpret_cast<DWORD_PTR>(this))) {
    children_.pop_back();
    return Fail(ErrorCode::kPlatform, "Could not subclass the chrome island");
  }
  return {};
}

Result<void> NativeFrame::AttachIsland(HWND island) {
  if (!parent_ || !IsChild(parent_, island))
    return Fail(ErrorCode::kInvalidArgument,
                "Island is not a child of this frame");
  auto result = AttachChild(island);
  if (!result)
    return result;
  struct Visit {
    NativeFrame& frame;
    bool ok = true;
  } visit{*this};
  EnumChildWindows(
      island,
      [](HWND child, LPARAM context) noexcept -> BOOL {
        auto& visit = *reinterpret_cast<Visit*>(context);
        try {
          visit.ok = visit.frame.AttachChild(child).has_value();
        } catch (...) {
          visit.ok = false;
        }
        return visit.ok;
      },
      reinterpret_cast<LPARAM>(&visit));
  return visit.ok ? Result<void>{}
                  : Fail(ErrorCode::kPlatform, "Could not attach island input");
}

void NativeFrame::Reset() noexcept {
  for (HWND child : children_)
    if (IsWindow(child))
      RemoveWindowSubclass(child, IslandProcedure,
                           reinterpret_cast<UINT_PTR>(this));
  children_.clear();
  if (IsWindow(parent_))
    RemoveWindowSubclass(parent_, ParentProcedure,
                         reinterpret_cast<UINT_PTR>(this));
  parent_ = nullptr;
  context_ = nullptr;
  settings_ = nullptr;
  activation_ = nullptr;
  draggable_ = {};
  geometry_ = {};
  origin_ = {};
  scale_ = 1;
}

void NativeFrame::RefreshMetrics() noexcept {
  if (!parent_)
    return;
  const auto dpi = GetDpiForWindow(parent_);
  scale_ = dpi ? static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI : 1;
  RECT client{};
  GetClientRect(parent_, &client);
  origin_ = {};
  ClientToScreen(parent_, &origin_);
  geometry_.client = {0, 0, static_cast<float>(client.right) / scale_,
                      static_cast<float>(client.bottom) / scale_};
  const int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
  geometry_.resize_x =
      static_cast<float>(GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                         padding) /
      scale_;
  geometry_.resize_y =
      static_cast<float>(GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                         padding) /
      scale_;
  geometry_.resizable =
      (GetWindowLongPtrW(parent_, GWL_STYLE) & WS_THICKFRAME) &&
      !IsZoomed(parent_);
}

void NativeFrame::Update(std::span<const Rect> draggable,
                         std::span<const Rect> controls,
                         bool fullscreen) {
  geometry_.fullscreen = fullscreen;
  draggable_ = drag_regions_.Update(draggable, controls);
}

FrameHit NativeFrame::HitTest(LPARAM point) const noexcept {
  return HitTestFrame(
      geometry_,
      {static_cast<float>(GET_X_LPARAM(point) - origin_.x) / scale_,
       static_cast<float>(GET_Y_LPARAM(point) - origin_.y) / scale_},
      draggable_);
}

LRESULT CALLBACK NativeFrame::ParentProcedure(HWND window,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam,
                                              UINT_PTR id,
                                              DWORD_PTR data) noexcept {
  auto* frame = reinterpret_cast<NativeFrame*>(data);
  if (message == WM_ACTIVATEAPP && frame->activation_) {
    try {
      frame->activation_(frame->context_, wparam != 0);
    } catch (...) {
      OutputDebugStringW(L"KryptonUI activation notification failed\n");
    }
  }
  if (message == WM_NCHITTEST) {
    LRESULT native = 0;
    // Keep DWM caption hit targets, including HTMAXBUTTON for Snap Layouts.
    if (!frame->geometry_.fullscreen &&
        DwmDefWindowProc(window, message, wparam, lparam, &native) &&
        (native == HTMINBUTTON || native == HTMAXBUTTON || native == HTCLOSE ||
         native == HTSYSMENU || native == HTHELP))
      return native;
    const auto hit = frame->HitTest(lparam);
    if (hit != FrameHit::kClient)
      return NativeHit(hit);
  }
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(window, ParentProcedure, id);
    frame->parent_ = nullptr;
    return DefSubclassProc(window, message, wparam, lparam);
  }
  // Do not touch the instance after forwarding: host dispatch may destroy it.
  if (message == WM_WINDOWPOSCHANGED || message == WM_DPICHANGED ||
      message == WM_STYLECHANGED || message == WM_SETTINGCHANGE ||
      message == WM_THEMECHANGED)
    frame->RefreshMetrics();
  if ((message == WM_SETTINGCHANGE || message == WM_SYSCOLORCHANGE) &&
      frame->settings_) {
    try {
      frame->settings_(frame->context_);
    } catch (...) {
      OutputDebugStringW(L"KryptonUI native settings update failed\n");
    }
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK NativeFrame::IslandProcedure(HWND window,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam,
                                              UINT_PTR id,
                                              DWORD_PTR data) noexcept {
  auto* frame = reinterpret_cast<NativeFrame*>(data);
  if (message == WM_NCHITTEST && frame->HitTest(lparam) != FrameHit::kClient)
    return HTTRANSPARENT;
  if (message == WM_PARENTNOTIFY && LOWORD(wparam) == WM_CREATE) {
    try {
      if (!frame->AttachChild(reinterpret_cast<HWND>(lparam)))
        OutputDebugStringW(L"KryptonUI could not attach a new island child\n");
    } catch (...) {
      OutputDebugStringW(L"KryptonUI island child registration failed\n");
    }
  }
  if (message == WM_NCDESTROY) {
    RemoveWindowSubclass(window, IslandProcedure, id);
    std::erase(frame->children_, window);
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

}  // namespace kr::win
