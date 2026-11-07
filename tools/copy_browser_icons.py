"""Copy browser logos from .deps/browser-logos and additional sources to design/icons/browsers."""

import os
import shutil
import ssl
import sys
import urllib.request
from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
DEPS_LOGOS = REPO_ROOT / ".deps" / "browser-logos" / "src"
DEST_DIR = REPO_ROOT / "design" / "icons" / "browsers"

BROWSERS_FROM_DEPS = [
    "chrome",
    "chrome-beta",
    "chrome-dev",
    "chrome-canary",
    "chromium",
    "edge",
    "edge-beta",
    "edge-dev",
    "edge-canary",
    "firefox",
    "firefox-beta",
    "firefox-developer-edition",
    "firefox-nightly",
    "brave",
    "brave-beta",
    "brave-dev",
    "brave-nightly",
    "safari",
    "safari-technology-preview",
    "opera",
    "opera-gx",
    "opera-developer",
    "vivaldi",
    "vivaldi-snapshot",
    "tor",
    "yandex",
    "samsung-internet",
    "maxthon",
    "midori",
]

SSL_CTX = ssl._create_unverified_context()
HEADERS = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}


def download_file(url: str, dest: Path) -> bool:
    req = urllib.request.Request(url, headers=HEADERS)
    try:
        with urllib.request.urlopen(req, context=SSL_CTX) as resp:
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(resp.read())
        return True
    except Exception as e:
        print(f"Failed to download {url}: {e}", file=sys.stderr)
        return False


def copy_from_deps():
    print("Copying from .deps/browser-logos/src...")
    for browser in BROWSERS_FROM_DEPS:
        src_browser_dir = DEPS_LOGOS / browser
        if not src_browser_dir.exists():
            print(f"Warning: {src_browser_dir} does not exist", file=sys.stderr)
            continue

        dest_browser_dir = DEST_DIR / browser
        dest_browser_dir.mkdir(parents=True, exist_ok=True)

        # Copy all image files into dest_browser_dir
        primary_svg = None
        primary_png = None
        for item in src_browser_dir.iterdir():
            if item.suffix.lower() in [".svg", ".png"]:
                dest_file = dest_browser_dir / item.name
                shutil.copy2(item, dest_file)
                if item.suffix.lower() == ".svg" and (primary_svg is None or item.name == f"{browser}.svg"):
                    primary_svg = item
                if item.suffix.lower() == ".png":
                    if item.name == f"{browser}.png" or item.name == f"{browser}_256x256.png" or item.name == f"{browser}_128x128.png":
                        if primary_png is None or item.name == f"{browser}.png" or item.name == f"{browser}_256x256.png":
                            primary_png = item

        # Also copy primary SVG and PNG to DEST_DIR root
        if primary_svg and primary_svg.exists():
            shutil.copy2(primary_svg, DEST_DIR / f"{browser}.svg")
        if primary_png and primary_png.exists():
            shutil.copy2(primary_png, DEST_DIR / f"{browser}.png")
        print(f"  [+] {browser}")


def setup_arc():
    print("Setting up Arc browser icons...")
    arc_dir = DEST_DIR / "arc"
    arc_dir.mkdir(parents=True, exist_ok=True)

    arc_svg_url = "https://upload.wikimedia.org/wikipedia/commons/3/37/Arc_%28browser%29_logo.svg"
    arc_png_url = "https://upload.wikimedia.org/wikipedia/commons/thumb/3/37/Arc_%28browser%29_logo.svg/1280px-Arc_%28browser%29_logo.svg.png"

    dest_svg = arc_dir / "arc.svg"
    dest_large_png = arc_dir / "arc_1280x1063.png"

    if download_file(arc_svg_url, dest_svg):
        shutil.copy2(dest_svg, DEST_DIR / "arc.svg")

    if download_file(arc_png_url, dest_large_png):
        try:
            im = Image.open(dest_large_png)
            # Create 256x256
            im_256 = im.resize((256, int(256 * im.height / im.width)), Image.Resampling.LANCZOS)
            im_256.save(arc_dir / "arc_256x256.png")
            im_256.save(arc_dir / "arc.png")
            im_256.save(DEST_DIR / "arc.png")

            im_128 = im.resize((128, int(128 * im.height / im.width)), Image.Resampling.LANCZOS)
            im_128.save(arc_dir / "arc_128x128.png")
        except Exception as e:
            print(f"Failed to process Arc PNG: {e}", file=sys.stderr)
    print("  [+] arc")


def setup_thorium():
    print("Setting up Thorium browser icons...")
    thorium_dir = DEST_DIR / "thorium"
    thorium_dir.mkdir(parents=True, exist_ok=True)

    svg_url = "https://raw.githubusercontent.com/Alex313031/thorium/main/logos/NEW/product_logo.svg"
    png_url = "https://raw.githubusercontent.com/Alex313031/thorium/main/logos/NEW/product_logo_256.png"

    dest_svg = thorium_dir / "thorium.svg"
    dest_png = thorium_dir / "thorium_256x256.png"

    if download_file(svg_url, dest_svg):
        shutil.copy2(dest_svg, DEST_DIR / "thorium.svg")

    if download_file(png_url, dest_png):
        shutil.copy2(dest_png, thorium_dir / "thorium.png")
        shutil.copy2(dest_png, DEST_DIR / "thorium.png")
        try:
            im = Image.open(dest_png)
            im_128 = im.resize((128, 128), Image.Resampling.LANCZOS)
            im_128.save(thorium_dir / "thorium_128x128.png")
        except Exception as e:
            print(f"Failed to resize Thorium PNG: {e}", file=sys.stderr)
    print("  [+] thorium")


def setup_helium():
    print("Setting up Helium browser icons...")
    helium_dir = DEST_DIR / "helium"
    helium_dir.mkdir(parents=True, exist_ok=True)

    src_svg = REPO_ROOT / "reference" / "helium-windows" / "helium-chromium" / "resources" / "branding" / "product_logo.svg"
    src_png = REPO_ROOT / "reference" / "helium-windows" / "helium-chromium" / "resources" / "branding" / "product_logo_200.png"

    if src_svg.exists():
        shutil.copy2(src_svg, helium_dir / "helium.svg")
        shutil.copy2(src_svg, DEST_DIR / "helium.svg")

    if src_png.exists():
        shutil.copy2(src_png, helium_dir / "helium.png")
        shutil.copy2(src_png, DEST_DIR / "helium.png")
        try:
            im = Image.open(src_png)
            im_256 = im.resize((256, 256), Image.Resampling.LANCZOS)
            im_256.save(helium_dir / "helium_256x256.png")
            im_128 = im.resize((128, 128), Image.Resampling.LANCZOS)
            im_128.save(helium_dir / "helium_128x128.png")
        except Exception as e:
            print(f"Failed to resize Helium PNG: {e}", file=sys.stderr)
    print("  [+] helium")


def setup_zen():
    print("Setting up Zen browser icons...")
    zen_dir = DEST_DIR / "zen"
    zen_dir.mkdir(parents=True, exist_ok=True)

    src_svg = REPO_ROOT / "reference" / "desktop" / "docs" / "assets" / "zen-browser.svg"
    src_ico = REPO_ROOT / "reference" / "desktop" / "configs" / "branding" / "release" / "firefox.ico"

    if src_svg.exists():
        shutil.copy2(src_svg, zen_dir / "zen.svg")
        shutil.copy2(src_svg, DEST_DIR / "zen.svg")

    if src_ico.exists():
        try:
            im = Image.open(src_ico)
            im_256 = im.resize((256, 256), Image.Resampling.LANCZOS)
            im_256.save(zen_dir / "zen_256x256.png")
            im_256.save(zen_dir / "zen.png")
            im_256.save(DEST_DIR / "zen.png")

            im_128 = im.resize((128, 128), Image.Resampling.LANCZOS)
            im_128.save(zen_dir / "zen_128x128.png")
        except Exception as e:
            print(f"Failed to extract Zen PNG from ICO: {e}", file=sys.stderr)
    print("  [+] zen")


def main():
    DEST_DIR.mkdir(parents=True, exist_ok=True)
    copy_from_deps()
    setup_arc()
    setup_thorium()
    setup_helium()
    setup_zen()
    print("\nBrowser icons successfully gathered and organized in design/icons/browsers!")


if __name__ == "__main__":
    main()
