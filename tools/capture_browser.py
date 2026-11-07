"""Capture a running browser at repeatable physical-pixel dimensions on Windows.

Uses only Python's standard library. Run with --help for framing and target options.
"""

from __future__ import annotations

import argparse
import ctypes as ct
from ctypes import wintypes as wt
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import sys
import time
import zlib


PRESETS = {
    # canvas width/height, visible window width/height, window offset in the canvas
    "reference": (822, 571, 1280, 800, 254, 169),
    "desktop": (1440, 900, 1200, 780, 120, 60),
    "window": (1280, 800, 1280, 800, 0, 0),
}


class WindowPlacement(ct.Structure):
    _fields_ = [
        ("length", wt.UINT), ("flags", wt.UINT), ("showCmd", wt.UINT),
        ("ptMinPosition", wt.POINT), ("ptMaxPosition", wt.POINT),
        ("rcNormalPosition", wt.RECT),
    ]


class MonitorInfo(ct.Structure):
    _fields_ = [
        ("cbSize", wt.DWORD), ("rcMonitor", wt.RECT),
        ("rcWork", wt.RECT), ("dwFlags", wt.DWORD),
    ]


class BitmapInfoHeader(ct.Structure):
    _fields_ = [
        ("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
        ("biPlanes", wt.WORD), ("biBitCount", wt.WORD),
        ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
        ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
        ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD),
    ]


def rect_tuple(rect):
    return rect.left, rect.top, rect.right, rect.bottom


def bind(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = list(arguments)
    return function


class Desktop:
    def __init__(self):
        user = ct.WinDLL("user32", use_last_error=True)
        desktop_handle = user.OpenDesktopW("Default", 0, False, 0x01FF)
        if desktop_handle:
            user.SetThreadDesktop(desktop_handle)
        gdi = ct.WinDLL("gdi32", use_last_error=True)
        kernel = ct.WinDLL("kernel32", use_last_error=True)
        dwm = ct.WinDLL("dwmapi", use_last_error=True)
        self.window_callback = ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
        self.monitor_callback = ct.WINFUNCTYPE(
            wt.BOOL, wt.HANDLE, wt.HDC, ct.POINTER(wt.RECT), wt.LPARAM
        )
        self.dpi_context = bind(user, "SetThreadDpiAwarenessContext", ct.c_void_p, ct.c_void_p)
        self.enum_windows = bind(user, "EnumWindows", wt.BOOL, self.window_callback, wt.LPARAM)
        self.visible = bind(user, "IsWindowVisible", wt.BOOL, wt.HWND)
        self.iconic = bind(user, "IsIconic", wt.BOOL, wt.HWND)
        self.zoomed = bind(user, "IsZoomed", wt.BOOL, wt.HWND)
        self.owner = bind(user, "GetWindow", wt.HWND, wt.HWND, wt.UINT)
        self.pid = bind(user, "GetWindowThreadProcessId", wt.DWORD, wt.HWND, ct.POINTER(wt.DWORD))
        self.title = bind(user, "GetWindowTextW", ct.c_int, wt.HWND, wt.LPWSTR, ct.c_int)
        self.window_rect = bind(user, "GetWindowRect", wt.BOOL, wt.HWND, ct.POINTER(wt.RECT))
        self.frame_rect = bind(dwm, "DwmGetWindowAttribute", wt.LONG, wt.HWND, wt.DWORD, ct.c_void_p, wt.DWORD)
        self.get_placement = bind(user, "GetWindowPlacement", wt.BOOL, wt.HWND, ct.POINTER(WindowPlacement))
        self.set_placement = bind(user, "SetWindowPlacement", wt.BOOL, wt.HWND, ct.POINTER(WindowPlacement))
        self.show = bind(user, "ShowWindowAsync", wt.BOOL, wt.HWND, ct.c_int)
        self.position = bind(user, "SetWindowPos", wt.BOOL, wt.HWND, wt.HWND, ct.c_int, ct.c_int, ct.c_int, ct.c_int, wt.UINT)
        self.foreground = bind(user, "SetForegroundWindow", wt.BOOL, wt.HWND)
        self.dpi = bind(user, "GetDpiForWindow", wt.UINT, wt.HWND)
        self.enum_monitors = bind(user, "EnumDisplayMonitors", wt.BOOL, wt.HDC, ct.c_void_p, self.monitor_callback, wt.LPARAM)
        self.monitor_info = bind(user, "GetMonitorInfoW", wt.BOOL, wt.HANDLE, ct.POINTER(MonitorInfo))
        self.open_process = bind(kernel, "OpenProcess", wt.HANDLE, wt.DWORD, wt.BOOL, wt.DWORD)
        self.process_image = bind(kernel, "QueryFullProcessImageNameW", wt.BOOL, wt.HANDLE, wt.DWORD, wt.LPWSTR, ct.POINTER(wt.DWORD))
        self.close_handle = bind(kernel, "CloseHandle", wt.BOOL, wt.HANDLE)
        self.get_dc = bind(user, "GetDC", wt.HDC, wt.HWND)
        self.release_dc = bind(user, "ReleaseDC", ct.c_int, wt.HWND, wt.HDC)
        self.create_dc = bind(gdi, "CreateCompatibleDC", wt.HDC, wt.HDC)
        self.delete_dc = bind(gdi, "DeleteDC", wt.BOOL, wt.HDC)
        self.create_dib = bind(gdi, "CreateDIBSection", wt.HBITMAP, wt.HDC, ct.c_void_p, wt.UINT, ct.POINTER(ct.c_void_p), wt.HANDLE, wt.DWORD)
        self.select_object = bind(gdi, "SelectObject", wt.HANDLE, wt.HDC, wt.HANDLE)
        self.delete_object = bind(gdi, "DeleteObject", wt.BOOL, wt.HANDLE)
        self.bit_blt = bind(gdi, "BitBlt", wt.BOOL, wt.HDC, ct.c_int, ct.c_int, ct.c_int, ct.c_int, wt.HDC, ct.c_int, ct.c_int, wt.DWORD)
        self.flush = bind(gdi, "GdiFlush", wt.BOOL)

    def windows(self, executable, process_id, window_handle):
        windows = []

        @self.window_callback
        def visit(hwnd, _):
            if not self.visible(hwnd) or self.owner(hwnd, 4):  # GW_OWNER
                return True
            if window_handle is not None and hwnd != window_handle:
                return True
            process = wt.DWORD()
            self.pid(hwnd, ct.byref(process))
            if process_id is not None and process.value != process_id:
                return True
            handle = self.open_process(0x1000, False, process.value)
            image = ct.create_unicode_buffer(32768)
            length = wt.DWORD(len(image))
            try:
                path = image.value if handle and self.process_image(handle, 0, image, ct.byref(length)) else ""
            finally:
                if handle:
                    self.close_handle(handle)
            if process_id is None and window_handle is None:
                if not path or Path(path) != executable:
                    return True
            title = ct.create_unicode_buffer(1024)
            self.title(hwnd, title, len(title))
            if title.value:
                windows.append({"handle": hex(hwnd), "process_id": process.value,
                                "executable": path, "title": title.value})
            return True

        if not self.enum_windows(visit, 0):
            raise ct.WinError(ct.get_last_error())
        return windows

    def monitors(self):
        monitors = []

        @self.monitor_callback
        def visit(handle, _dc, _rect, _data):
            info = MonitorInfo(cbSize=ct.sizeof(MonitorInfo))
            if self.monitor_info(handle, ct.byref(info)):
                monitors.append({"bounds": rect_tuple(info.rcMonitor),
                                 "work_area": rect_tuple(info.rcWork),
                                 "primary": bool(info.dwFlags & 1)})
            return True

        if not self.enum_monitors(None, None, visit, 0):
            raise ct.WinError(ct.get_last_error())
        return sorted(monitors, key=lambda item: (not item["primary"], item["bounds"]))

    def bounds(self, hwnd, visible=True):
        rect = wt.RECT()
        if visible and self.frame_rect(hwnd, 9, ct.byref(rect), ct.sizeof(rect)) >= 0:
            return rect_tuple(rect)
        if not self.window_rect(hwnd, ct.byref(rect)):
            raise ct.WinError(ct.get_last_error())
        return rect_tuple(rect)

    def place(self, hwnd, target):
        if self.iconic(hwnd) or self.zoomed(hwnd):
            self.show(hwnd, 9)  # SW_RESTORE
            time.sleep(0.2)
        x, y, right, bottom = target
        flags = 0x0004 | 0x0010 | 0x4000  # NOZORDER | NOACTIVATE | ASYNCWINDOWPOS
        deadline = time.monotonic() + 3
        retry_at = 0
        while time.monotonic() < deadline:
            frame = self.bounds(hwnd)
            if frame == target:
                self.foreground(hwnd)
                return
            if time.monotonic() >= retry_at:
                # Moving to another monitor can change the invisible frame's
                # thickness. Recompute it after Windows applies the new DPI.
                outer = self.bounds(hwnd, visible=False)
                width = right - x + (outer[2] - outer[0]) - (frame[2] - frame[0])
                height = bottom - y + (outer[3] - outer[1]) - (frame[3] - frame[1])
                if not self.position(hwnd, None, x + outer[0] - frame[0],
                                     y + outer[1] - frame[1], width, height, flags):
                    raise ct.WinError(ct.get_last_error())
                retry_at = time.monotonic() + 0.3
            time.sleep(0.05)
        raise RuntimeError("The browser did not accept the requested bounds; check its minimum size or DPI.")

    def capture(self, bounds):
        x, y, right, bottom = bounds
        width, height = right - x, bottom - y
        screen = self.get_dc(None)
        memory = bitmap = previous = None
        try:
            if not screen:
                raise ct.WinError(ct.get_last_error())
            memory = self.create_dc(screen)
            if not memory:
                raise ct.WinError(ct.get_last_error())
            # Top-down, 32-bit BI_RGB. A zero alpha channel from GDI is discarded.
            header = BitmapInfoHeader(biSize=ct.sizeof(BitmapInfoHeader),
                                      biWidth=width, biHeight=-height,
                                      biPlanes=1, biBitCount=32)
            pixels = ct.c_void_p()
            bitmap = self.create_dib(screen, ct.byref(header), 0, ct.byref(pixels), None, 0)
            if not bitmap:
                raise ct.WinError(ct.get_last_error())
            previous = self.select_object(memory, bitmap)
            if not previous or previous == ct.c_void_p(-1).value:
                previous = None
                raise ct.WinError(ct.get_last_error())
            if not self.bit_blt(memory, 0, 0, width, height, screen, x, y, 0x40CC0020):
                raise ct.WinError(ct.get_last_error())  # SRCCOPY | CAPTUREBLT
            self.flush()
            return ct.string_at(pixels, width * height * 4)
        finally:
            if previous:
                self.select_object(memory, previous)
            if bitmap:
                self.delete_object(bitmap)
            if memory:
                self.delete_dc(memory)
            if screen:
                self.release_dc(None, screen)


def png_bytes(width, height, bgra):
    """Encode unscaled RGB pixels without an imaging dependency."""
    rgb = bytearray(width * height * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = bgra[2::4], bgra[1::4], bgra[0::4]
    stride = width * 3
    rows = b"".join(b"\0" + rgb[start:start + stride]
                    for start in range(0, len(rgb), stride))

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload)))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", type=Path, default=Path("out/screenshots/browser.png"))
    parser.add_argument("--framing", choices=PRESETS, default="reference",
                        help="reference: 822x571 crop like the example; desktop: 1440x900; window: 1280x800")
    parser.add_argument("--size", nargs=2, type=int, metavar=("WIDTH", "HEIGHT"), help="output size in physical pixels")
    parser.add_argument("--window-size", nargs=2, type=int, metavar=("WIDTH", "HEIGHT"), help="visible browser size for desktop/reference framing")
    parser.add_argument("--offset", nargs=2, type=int, metavar=("X", "Y"), help="browser position relative to the selected monitor")
    parser.add_argument("--browser", type=Path,
                        default=Path(__file__).resolve().parents[2] / "Krypton/build/src/out/Default/chrome.exe",
                        help="running browser executable (defaults to the sibling Krypton build)")
    target = parser.add_mutually_exclusive_group()
    target.add_argument("--process-id", type=int, help="target a specific browser process instead of an executable")
    target.add_argument("--window-handle", type=lambda value: int(value, 0), help="target one window, e.g. 0x15909f2")
    parser.add_argument("--monitor", type=int, default=0, help="monitor index; 0 is the primary monitor")
    parser.add_argument("--expect-dpi", type=int, help="require this window DPI (96=100%%, 120=125%%, 144=150%%)")
    parser.add_argument("--delay", type=float, default=2, help="seconds before capture; use time to open a menu (default: 2)")
    operation = parser.add_mutually_exclusive_group()
    operation.add_argument("--prepare-only", action="store_true", help="position/focus the browser without capturing")
    operation.add_argument("--capture-only", action="store_true", help="capture prepared geometry without moving or focusing the browser")
    operation.add_argument("--dry-run", action="store_true", help="print the capture plan without moving, focusing, or capturing")
    operation.add_argument("--list-windows", action="store_true", help="list matching windows and monitor bounds without capturing")
    parser.add_argument("--keep-window", action="store_true", help="leave the browser at the capture position instead of restoring it")
    args = parser.parse_args()
    if args.output.suffix.lower() != ".png":
        parser.error("output must have a .png extension")
    if not 0 <= args.delay <= 60:
        parser.error("--delay must be between 0 and 60 seconds")
    if args.framing == "window" and args.window_size:
        parser.error("use --size for window framing")
    if args.expect_dpi is not None and args.expect_dpi <= 0:
        parser.error("--expect-dpi must be positive")
    return args


def run(args):
    desktop = Desktop()
    previous_dpi = desktop.dpi_context(ct.c_void_p(-4))  # PER_MONITOR_AWARE_V2
    if not previous_dpi:
        raise ct.WinError(ct.get_last_error())
    hwnd = placement = None
    restore = False
    try:
        windows = desktop.windows(args.browser.resolve(), args.process_id, args.window_handle)
        monitors = desktop.monitors()
        if args.list_windows:
            print(json.dumps({"windows": windows, "monitors": monitors}, indent=2))
            return
        if len(windows) != 1:
            raise RuntimeError(f"Found {len(windows)} matching windows. Use --list-windows, then --window-handle to choose one. The script never launches the browser.")
        if not 0 <= args.monitor < len(monitors):
            raise RuntimeError("Invalid monitor index; use --list-windows to inspect monitors.")
        window = windows[0]
        hwnd = int(window["handle"], 16)
        monitor = monitors[args.monitor]["bounds"]
        cw, ch, ww, wh, dx, dy = PRESETS[args.framing]
        cw, ch = args.size or (cw, ch)
        ww, wh = (cw, ch) if args.framing == "window" else (args.window_size or (ww, wh))
        dx, dy = args.offset or (dx, dy)
        if min(cw, ch, ww, wh) <= 0 or max(cw, ch, ww, wh) > 16384:
            raise RuntimeError("Dimensions must be between 1 and 16384 physical pixels.")
        x, y = monitor[0] + dx, monitor[1] + dy
        target = (x, y, x + ww, y + wh)
        crop = target if args.framing == "window" else (monitor[0], monitor[1], monitor[0] + cw, monitor[1] + ch)
        for name, rect in (("browser", target), ("capture", crop)):
            if not (monitor[0] <= rect[0] < rect[2] <= monitor[2]
                    and monitor[1] <= rect[1] < rect[3] <= monitor[3]):
                raise RuntimeError(f"The {name} rectangle {rect} does not fit monitor {monitor}; choose a smaller --size/--window-size/--offset or another --monitor.")
        metadata = {"framing": args.framing, "image_size": [cw, ch],
                    "capture_bounds_px": crop, "window_bounds_px": target,
                    "monitor": monitors[args.monitor], "window": window,
                    "dpi": desktop.dpi(hwnd), "output": str(args.output.resolve())}
        if args.dry_run:
            metadata["current_window_bounds_px"] = desktop.bounds(hwnd)
            print(json.dumps(metadata, indent=2))
            return
        placement = WindowPlacement(length=ct.sizeof(WindowPlacement))
        if not desktop.get_placement(hwnd, ct.byref(placement)):
            raise ct.WinError(ct.get_last_error())
        if not args.capture_only:
            restore = True
            desktop.place(hwnd, target)
        dpi = desktop.dpi(hwnd)
        if args.expect_dpi is not None and dpi != args.expect_dpi:
            raise RuntimeError(f"Window DPI is {dpi}, expected {args.expect_dpi}. Keep Windows display scaling consistent; the script does not change display settings.")
        if args.prepare_only:
            restore = False
            print(f"Prepared {ww}x{wh} browser at ({dx}, {dy}), DPI {dpi}. Use --capture-only with the same framing options next.")
            return
        print(f"Capturing {cw}x{ch} in {args.delay:g}s; keep the browser and crop unobscured.", flush=True)
        time.sleep(args.delay)
        if desktop.iconic(hwnd) or not desktop.visible(hwnd) or desktop.bounds(hwnd) != target:
            raise RuntimeError("The browser is minimized, hidden, or moved. Prepare it again before capturing.")
        # Read DPI again after the delay so a display-scale change cannot go unnoticed.
        if desktop.dpi(hwnd) != dpi:
            raise RuntimeError("Display DPI changed while waiting; prepare and capture again.")
        pixels = desktop.capture(crop)
        metadata.update(dpi=dpi, captured_at=datetime.now(timezone.utc).isoformat())
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(png_bytes(cw, ch, pixels))
        args.output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        if args.keep_window:
            restore = False
        print(f"Saved {args.output.resolve()} ({cw}x{ch}, DPI {dpi}) and geometry metadata.")
    finally:
        try:
            if restore and hwnd and placement:
                placement.flags |= 0x0004  # WPF_ASYNCWINDOWPLACEMENT
                if not desktop.set_placement(hwnd, ct.byref(placement)):
                    print("Warning: could not restore the browser's original placement.", file=sys.stderr)
        finally:
            desktop.dpi_context(previous_dpi)


def main():
    args = arguments()
    if sys.platform != "win32":
        raise SystemExit("This screenshot helper requires Windows.")
    try:
        run(args)
    except (OSError, RuntimeError) as error:
        raise SystemExit(f"Capture failed: {error}") from error


if __name__ == "__main__":
    main()
