"""Download and organize search engine icons into design/icons/search_engines."""

import io
import shutil
import ssl
import sys
import urllib.request
from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
DEST_DIR = REPO_ROOT / "design" / "icons" / "search_engines"
BROWSERS_DIR = REPO_ROOT / "design" / "icons" / "browsers"

SSL_CTX = ssl._create_unverified_context()
HEADERS = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}

SEARCH_ENGINES = {
    "google": {
        "domain": "google.com",
        "svg_urls": [
            "https://upload.wikimedia.org/wikipedia/commons/c/c1/Google_%22G%22_logo.svg",
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/google.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=google.com&sz=128",
    },
    "bing": {
        "domain": "bing.com",
        "svg_urls": [
            "https://upload.wikimedia.org/wikipedia/commons/e/e8/Microsoft_Bing_logo.svg",
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/microsoftbing.svg",
        ],
        "png_url": "https://icons.duckduckgo.com/ip3/bing.com.ico",
    },
    "duckduckgo": {
        "domain": "duckduckgo.com",
        "svg_urls": [
            "https://duckduckgo.com/assets/logo_header.v108.svg",
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/duckduckgo.svg",
        ],
        "png_url": "https://duckduckgo.com/assets/icons/meta/DDG-icon_256x256.png",
    },
    "yahoo": {
        "domain": "yahoo.com",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/yahoo.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=yahoo.com&sz=128",
    },
    "ecosia": {
        "domain": "ecosia.org",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/ecosia.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=ecosia.org&sz=128",
    },
    "brave": {
        "domain": "search.brave.com",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/brave.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=search.brave.com&sz=128",
        "local_browser": "brave",
    },
    "baidu": {
        "domain": "baidu.com",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/baidu.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=baidu.com&sz=128",
    },
    "yandex": {
        "domain": "yandex.com",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/yandex.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=yandex.com&sz=128",
        "local_browser": "yandex",
    },
    "qwant": {
        "domain": "qwant.com",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/qwant.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=qwant.com&sz=128",
    },
    "startpage": {
        "domain": "startpage.com",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/startpage.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=startpage.com&sz=128",
    },
    "kagi": {
        "domain": "kagi.com",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/kagi.svg",
        ],
        "png_url": "https://icons.duckduckgo.com/ip3/kagi.com.ico",
    },
    "perplexity": {
        "domain": "perplexity.ai",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/perplexity.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=perplexity.ai&sz=128",
    },
    "wikipedia": {
        "domain": "wikipedia.org",
        "svg_urls": [
            "https://cdn.jsdelivr.net/npm/simple-icons/icons/wikipedia.svg",
        ],
        "png_url": "https://www.google.com/s2/favicons?domain=wikipedia.org&sz=128",
    },
}


def download_bytes(url: str) -> bytes | None:
    req = urllib.request.Request(url, headers=HEADERS)
    try:
        with urllib.request.urlopen(req, context=SSL_CTX) as resp:
            return resp.read()
    except Exception as e:
        print(f"  [x] Download failed {url}: {e}", file=sys.stderr)
        return None


def main():
    DEST_DIR.mkdir(parents=True, exist_ok=True)
    print("Downloading and organizing search engine icons...")

    for name, config in SEARCH_ENGINES.items():
        engine_dir = DEST_DIR / name
        engine_dir.mkdir(parents=True, exist_ok=True)

        # SVG
        svg_saved = False
        if config.get("local_browser"):
            local_svg = BROWSERS_DIR / f"{config['local_browser']}.svg"
            if local_svg.exists():
                shutil.copy2(local_svg, engine_dir / f"{name}.svg")
                shutil.copy2(local_svg, DEST_DIR / f"{name}.svg")
                svg_saved = True

        if not svg_saved:
            for url in config.get("svg_urls", []):
                data = download_bytes(url)
                if data:
                    (engine_dir / f"{name}.svg").write_bytes(data)
                    (DEST_DIR / f"{name}.svg").write_bytes(data)
                    svg_saved = True
                    break

        # PNG
        png_saved = False
        if config.get("local_browser"):
            local_png = BROWSERS_DIR / f"{config['local_browser']}.png"
            if local_png.exists():
                shutil.copy2(local_png, engine_dir / f"{name}.png")
                shutil.copy2(local_png, DEST_DIR / f"{name}.png")
                png_saved = True

        if not png_saved and config.get("png_url"):
            data = download_bytes(config["png_url"])
            if data:
                try:
                    im = Image.open(io.BytesIO(data))
                    # Convert to RGBA
                    if im.mode != "RGBA":
                        im = im.convert("RGBA")
                    # Save native
                    im.save(engine_dir / f"{name}.png")
                    im.save(DEST_DIR / f"{name}.png")

                    # Generate 128x128
                    if im.size != (128, 128):
                        im_128 = im.resize((128, 128), Image.Resampling.LANCZOS)
                        im_128.save(engine_dir / f"{name}_128x128.png")
                    else:
                        im.save(engine_dir / f"{name}_128x128.png")

                    # Generate 256x256 if high res enough
                    im_256 = im.resize((256, 256), Image.Resampling.LANCZOS)
                    im_256.save(engine_dir / f"{name}_256x256.png")
                    png_saved = True
                except Exception as e:
                    print(f"  [x] Processing PNG for {name} failed: {e}", file=sys.stderr)

        print(f"  [+] {name} (SVG: {'yes' if svg_saved else 'no'}, PNG: {'yes' if png_saved else 'no'})")

    print("\nSearch engine icons successfully saved in design/icons/search_engines/!")


if __name__ == "__main__":
    main()
