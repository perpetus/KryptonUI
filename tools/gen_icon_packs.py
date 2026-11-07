"""Chrome icon packs: fetch pinned releases, then compile them into the module.

  gen_icon_packs.py fetch <packs dir> <download dir>
      Downloads each pack's pinned npm tarball (design/icons/packs/
      manifest.json) into <download dir>, verifies its integrity and copies
      only the listed icons and the pack's license into <packs dir>/<pack>/.
      Run by hand when updating a pack; the results are committed.

  gen_icon_packs.py generate <packs dir> <output.hpp>
      Turns the committed SVGs into XAML path data. A role a pack lacks
      uses the default (first) pack's icon. Strokes and fills are
      kept apart: outline packs (Lucide, Tabler) draw a round-capped stroke,
      the others a fill. Nothing is read at run time; the data lives in the
      module's read-only image.
"""

import base64
import hashlib
import io
import json
import re
import sys
import tarfile
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

NUMBER = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"


# ------------------------------------------------------------------ fetch --

def download(url, target):
    with urllib.request.urlopen(url, timeout=120) as response:
        target.write_bytes(response.read())


def verify(data, integrity):
    algorithm, expected = integrity.split("-", 1)
    digest = base64.b64encode(hashlib.new(algorithm, data).digest()).decode()
    if digest != expected:
        raise ValueError("integrity mismatch")


def radix_paths(source, name):
    match = re.search(
        rf"var {name}Icon = /\*#__PURE__\*/forwardRef\((.*?)\n\}}\);", source,
        re.S)
    if not match:
        return None
    paths = re.findall(r'createElement\("path", \{\s*d: "([^"]+)"',
                       match.group(1))
    if not paths:
        return None
    body = "".join(
        f'<path d="{d}" fill="currentColor" fill-rule="evenodd" '
        f'clip-rule="evenodd"/>' for d in paths)
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="15" height="15" '
            f'viewBox="0 0 15 15" fill="none">{body}</svg>\n')


def fetch(packs_dir, download_dir):
    manifest = json.loads((packs_dir / "manifest.json").read_text("utf-8"))
    for pack in manifest["packs"]:
        work = download_dir / pack["id"]
        work.mkdir(parents=True, exist_ok=True)
        archive = work / "package.tgz"
        if not archive.exists():
            download(pack["tarball"], archive)
        data = archive.read_bytes()
        verify(data, pack["integrity"])
        out = packs_dir / pack["id"]
        out.mkdir(parents=True, exist_ok=True)
        for old in out.glob("*.svg"):
            old.unlink()
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
            def read(member):
                try:
                    handle = tar.extractfile(member)
                except KeyError:
                    return None
                return handle.read().decode("utf-8") if handle else None

            license_text = read("package/LICENSE")
            if license_text is None:
                with urllib.request.urlopen(pack["license_url"],
                                            timeout=60) as response:
                    license_text = response.read().decode("utf-8")
            (out / "LICENSE").write_text(license_text, "utf-8")
            radix = read(pack["path"]) if pack["id"] == "radix" else None
            for role, names in manifest["roles"].items():
                name = names.get(pack["id"])
                if not name:
                    continue
                svg = (radix_paths(radix, name) if radix is not None else
                       read(pack["path"].format(name=name)))
                if svg is None:
                    raise ValueError(f"{pack['id']}: no icon {name}")
                (out / f"{role}.svg").write_text(svg, "utf-8")
        print(f"{pack['id']}: {pack['package']}@{pack['version']}")


# --------------------------------------------------------------- generate --

def normalize(data):
    """Emit absolute, explicit XAML commands, preserving SVG subpath origins.

    WinUI's figure builder does not restore the SVG current point after Z in
    every geometry mode. Resolve relative moves here, before that parser sees
    them. Compact arc flags (for example `011`) still need separate tokens.
    """
    counts = {"m": 2, "l": 2, "h": 1, "v": 1, "c": 6, "s": 4, "q": 4, "t": 2,
              "a": 7, "z": 0}
    out, command, stream = [], None, data
    x, y, start_x, start_y = 0.0, 0.0, 0.0, 0.0
    position = 0
    pattern = re.compile(rf"\s*,?\s*([MmLlHhVvCcSsQqTtAaZz]|{NUMBER})")
    flag = re.compile(r"\s*,?\s*([01])")
    number = re.compile(rf"\s*,?\s*({NUMBER})")
    while True:
        match = pattern.match(stream, position)
        if not match:
            if stream[position:].strip():
                raise ValueError(f"bad path data near {stream[position:]!r}")
            break
        token = match.group(1)
        if token.isalpha():
            command = token
            position = match.end()
            if command in "zZ":
                out.append("Z")
                x, y = start_x, start_y
                command = None
            continue
        if command is None:
            raise ValueError("path data without a command")
        arguments = []
        for index in range(counts[command.lower()]):
            found = (flag if command in "aA" and index in (3, 4) else
                     number).match(stream, position)
            if not found:
                raise ValueError("truncated path data")
            arguments.append(found.group(1))
            position = found.end()
        values = [float(argument) for argument in arguments]
        absolute = command.upper()
        relative = command.islower()
        if absolute in ("M", "L", "C", "S", "Q", "T"):
            if relative:
                for index in range(0, len(values), 2):
                    values[index] += x
                    values[index + 1] += y
            x, y = values[-2:]
            if absolute == "M":
                start_x, start_y = x, y
        elif absolute == "H":
            values[0] += x if relative else 0
            x = values[0]
        elif absolute == "V":
            values[0] += y if relative else 0
            y = values[0]
        elif absolute == "A":
            if relative:
                values[5] += x
                values[6] += y
            x, y = values[5:]
        out.append(absolute + " " + " ".join(fmt(value) for value in values))
        if command in "mM":  # implicit commands after a move are lines
            command = "l" if command == "m" else "L"
    return " ".join(out)


def fmt(value):
    text = f"{value:.4f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def attribute(element, name, default=0.0):
    value = element.get(name)
    return float(value) if value is not None else default


def shape(element):
    kind = element.tag.rsplit("}", 1)[-1]
    if kind == "path":
        # A path's first move is absolute even when written relative; keep it
        # so once paths are joined into one geometry.
        data = normalize(element.get("d"))
        return "M" + data[1:] if data.startswith("m") else data
    if kind in ("circle", "ellipse"):
        cx, cy = attribute(element, "cx"), attribute(element, "cy")
        rx = attribute(element, "rx", attribute(element, "r"))
        ry = attribute(element, "ry", attribute(element, "r"))
        return (f"M {fmt(cx - rx)} {fmt(cy)} A {fmt(rx)} {fmt(ry)} 0 1 0 "
                f"{fmt(cx + rx)} {fmt(cy)} A {fmt(rx)} {fmt(ry)} 0 1 0 "
                f"{fmt(cx - rx)} {fmt(cy)} Z")
    if kind == "rect":
        x, y = attribute(element, "x"), attribute(element, "y")
        w, h = attribute(element, "width"), attribute(element, "height")
        rx = attribute(element, "rx", attribute(element, "ry"))
        ry = attribute(element, "ry", rx)
        rx, ry = min(rx, w / 2), min(ry, h / 2)
        if not rx:
            return (f"M {fmt(x)} {fmt(y)} H {fmt(x + w)} V {fmt(y + h)} "
                    f"H {fmt(x)} Z")
        arc = f"A {fmt(rx)} {fmt(ry)} 0 0 1"
        return (f"M {fmt(x + rx)} {fmt(y)} H {fmt(x + w - rx)} {arc} "
                f"{fmt(x + w)} {fmt(y + ry)} V {fmt(y + h - ry)} {arc} "
                f"{fmt(x + w - rx)} {fmt(y + h)} H {fmt(x + rx)} {arc} "
                f"{fmt(x)} {fmt(y + h - ry)} V {fmt(y + ry)} {arc} "
                f"{fmt(x + rx)} {fmt(y)} Z")
    if kind == "line":
        return (f"M {fmt(attribute(element, 'x1'))} "
                f"{fmt(attribute(element, 'y1'))} L "
                f"{fmt(attribute(element, 'x2'))} "
                f"{fmt(attribute(element, 'y2'))}")
    if kind in ("polyline", "polygon"):
        points = [float(v) for v in re.findall(NUMBER, element.get("points"))]
        pairs = [f"{fmt(points[i])} {fmt(points[i + 1])}"
                 for i in range(0, len(points), 2)]
        return "M " + " L ".join(pairs) + (" Z" if kind == "polygon" else "")
    raise ValueError(f"unsupported element {kind}")


def compile_svg(path):
    root = ET.fromstring(path.read_text("utf-8"))
    view = root.get("viewBox")
    width, height = ((float(v) for v in view.split()[2:]) if view else
                     (float(root.get("width")), float(root.get("height"))))
    stroked = root.get("stroke") not in (None, "none")
    stroke = float(root.get("stroke-width", "1")) if stroked else 0.0
    evenodd = False
    parts = []
    for element in root.iter():
        if element is root:
            continue
        kind = element.tag.rsplit("}", 1)[-1]
        if kind in ("g", "title", "desc", "defs"):
            if element.get("transform"):
                raise ValueError(f"{path}: transformed group")
            continue
        if element.get("transform"):
            raise ValueError(f"{path}: transformed {kind}")
        if element.get("stroke") == "none" and element.get("fill") == "none":
            continue  # Tabler's invisible bounding box
        if stroked and element.get("fill") not in (None, "none"):
            raise ValueError(f"{path}: filled shape in an outline icon")
        evenodd |= element.get("fill-rule") == "evenodd"
        parts.append(shape(element))
    if not parts:
        raise ValueError(f"{path}: empty icon")
    data = f"F{0 if evenodd else 1} " + " ".join(parts)
    return data, width, height, stroke


def generate(packs_dir, output):
    manifest = json.loads((packs_dir / "manifest.json").read_text("utf-8"))
    roles = list(manifest["roles"])
    rows = []
    for pack in manifest["packs"]:
        cells = []
        for role in roles:
            svg = packs_dir / pack["id"] / f"{role}.svg"
            if not svg.exists():  # every gap falls back to the default pack
                svg = packs_dir / manifest["packs"][0]["id"] / f"{role}.svg"
            data, width, height, stroke = compile_svg(svg)
            if ')"' in data:
                raise ValueError("delimiter collision")
            cells.append(f'      {{LR"({data})", {fmt(width)}, {fmt(height)}, '
                         f"{fmt(stroke)}}}")
        rows.append("    {{\n" + ",\n".join(cells) + "\n    }}")
    identifiers = ["k" + "".join(p.title() for p in role.split("_"))
                   for role in roles]
    names = ", ".join(f'"{pack["name"]}"' for pack in manifest["packs"])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "// Generated from design/icons/packs; do not edit. Each pack keeps its\n"
        "// license beside its icons.\n"
        "#pragma once\n#include <array>\n#include <cstddef>\n"
        "#include <cstdint>\n#include <string_view>\n"
        "namespace kr::design::icon_packs {\n"
        "// stroke > 0: an outline drawn with round caps and joins at that\n"
        "// width, in the icon's own units; otherwise a fill.\n"
        "struct PackIcon {\n  std::wstring_view data;\n  double width, height,"
        " stroke;\n};\n"
        "enum class IconRole : std::uint8_t {\n  "
        + ",\n  ".join(identifiers) + ",\n  kCount\n};\n"
        f"inline constexpr std::size_t kPackCount = {len(rows)};\n"
        f"inline constexpr std::size_t kRoleCount = {len(roles)};\n"
        "inline constexpr std::array<std::string_view, kPackCount> kPackNames{"
        f"{names}}};\n"
        "inline constexpr std::array<std::array<PackIcon, kRoleCount>, "
        "kPackCount> kIcons{{\n" + ",\n".join(rows) + "\n}};\n"
        "}  // namespace kr::design::icon_packs\n",
        encoding="utf-8")


def main():
    command, packs_dir, target = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    if command == "fetch":
        fetch(packs_dir, target)
    elif command == "generate":
        generate(packs_dir, target)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
