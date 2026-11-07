"""Compile Zen's animated library sprites for WinUI's SvgImageSource.

Each sprite is a horizontal strip of 36 nested <svg> frames. Direct2D's SVG
renderer, which backs SvgImageSource, supports neither masks nor <style>, so
this script rewrites every sprite into that subset:

* frames become translated groups, each clipped to its own viewBox;
* the library button's identical box is a stationary layer, outside the
  animated strip, so stepping or loading the animation never hides the icon;
* masks ("white cover minus black shapes") become even-odd clip paths, with
  transformed rects baked into path data;
* the library button's glow becomes a separate alpha strip, blurred by the
  native compositor; unsupported 10% mix-blend-mode overlays are dropped;
* context-fill / context-stroke / context-fill-opacity become tokens that the
  native layer replaces with theme colours.

Usage: gen_sprites.py <sprite dir> <output.hpp>
"""

import copy
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SVG = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG)
SPRITES = {
    "kLibrarySprite": "library-sprite",
    "kLibraryMediaSprite": "library-media-sprite",
    "kLibraryDownloadsSprite": "library-downloads-sprite",
    "kLibraryBoostsSprite": "library-boosts-sprite",
    "kLibrarySpacesSprite": "library-spaces-sprite",
    "kLibraryHistorySprite": "library-history-sprite",
    "kLibraryRoutingSprite": "library-space-routing-sprite",
    "kBoostIndicatorSprite": "boost-indicator",
}
TOKENS = {
    "context-fill-opacity": "KRFILLOPACITY",
    "context-fill": "KRFILL",
    "context-stroke": "KRSTROKE",
}
CHUNK = 12000


def tag(element):
    return element.tag.rsplit("}", 1)[-1]


def number(text):
    return float(text) if text is not None else 0.0


def fmt(value):
    text = f"{value:.3f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def parse_transform(text):
    """Returns the affine (a, b, c, d, e, f) of an SVG transform list."""
    matrix = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)

    def multiply(m, n):
        a, b, c, d, e, f = m
        g, h, i, j, k, l = n
        return (a * g + c * h, b * g + d * h, a * i + c * j, b * i + d * j,
                a * k + c * l + e, b * k + d * l + f)

    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", text or ""):
        values = [float(v) for v in re.findall(r"-?[\d.]+(?:e-?\d+)?", args)]
        if name == "translate":
            step = (1, 0, 0, 1, values[0], values[1] if len(values) > 1 else 0)
        elif name == "scale":
            sx = values[0]
            sy = values[1] if len(values) > 1 else sx
            step = (sx, 0, 0, sy, 0, 0)
        elif name == "rotate":
            angle = math.radians(values[0])
            cos, sin = math.cos(angle), math.sin(angle)
            step = (cos, sin, -sin, cos, 0, 0)
            if len(values) == 3:
                cx, cy = values[1], values[2]
                step = multiply(multiply((1, 0, 0, 1, cx, cy), step),
                                (1, 0, 0, 1, -cx, -cy))
        elif name == "matrix":
            step = tuple(values)
        else:
            raise ValueError(f"Unsupported transform {name}")
        matrix = multiply(matrix, step)
    return matrix


def rect_path(element, reverse):
    """A rect (optionally rounded and transformed) as absolute path data."""
    x, y = number(element.get("x")), number(element.get("y"))
    w, h = number(element.get("width")), number(element.get("height"))
    r = min(number(element.get("rx") or element.get("ry")), w / 2, h / 2)
    m = parse_transform(element.get("transform"))
    a, b, c, d, e, f = m

    def point(px, py):
        return f"{fmt(a * px + c * py + e)} {fmt(b * px + d * py + f)}"

    scale = math.sqrt(abs(a * d - b * c))
    # Clockwise in y-down coordinates: points with the segment reaching each.
    points = [(x + r, y), (x + w - r, y), (x + w, y + r), (x + w, y + h - r),
              (x + w - r, y + h), (x + r, y + h), (x, y + h - r), (x, y + r),
              (x + r, y)]
    kinds = ["L", "A", "L", "A", "L", "A", "L", "A"]
    sweep = 1
    if reverse:  # holes wind the other way
        points.reverse()
        kinds.reverse()
        sweep = 0
    if (a * d - b * c) < 0:
        sweep = 1 - sweep
    parts = [f"M{point(*points[0])}"]
    for kind, target in zip(kinds, points[1:]):
        if kind == "A" and r > 0:
            parts.append(f"A{fmt(r * scale)} {fmt(r * scale)} 0 0 {sweep} "
                         f"{point(*target)}")
        else:
            parts.append(f"L{point(*target)}")
    parts.append("Z")
    return "".join(parts)


def shape_path(element, reverse):
    kind = tag(element)
    if kind == "rect":
        return rect_path(element, reverse)
    if kind == "path":
        if element.get("transform"):
            raise ValueError("Transformed mask paths are unsupported")
        return element.get("d")
    raise ValueError(f"Unsupported mask shape {kind}")


def convert_frame(frame, index, defs, glow=False, exclude_box=False):
    x = number(frame.get("x"))
    width, height = number(frame.get("width")), number(frame.get("height"))
    vx, vy, vw, vh = (float(v) for v in frame.get("viewBox").split())
    masks = {}
    for element in list(frame.iter()):
        for child in list(element):
            name = tag(child)
            classes = child.get("class", "")
            if name in ("style", "filter") or (not glow and "glow" in classes) or \
                    (exclude_box and classes == "zen-library-box") or \
                    "mix-blend-mode" in child.get("style", ""):
                element.remove(child)
            elif name == "defs":
                for definition in list(child):
                    if tag(definition) == "clipPath":
                        defs.append(copy.deepcopy(definition))
                        continue
                    if tag(definition) != "mask":
                        raise ValueError(f"Unsupported definition "
                                         f"{tag(definition)}")
                    masks[definition.get("id")] = definition
                element.remove(child)
    for element in frame.iter():
        reference = element.attrib.pop("mask", None)
        if reference:
            mask = masks[reference[5:-1]]
            shapes = list(mask)
            if not shapes or shapes[0].get("fill") != "#fff" or any(
                    shape.get("fill") != "#000" for shape in shapes[1:]):
                raise ValueError("Unsupported mask")
            transform = None
            if len(shapes) == 2 and tag(shapes[1]) == "path" and shapes[1].get("transform"):
                # Express the enclosing rectangle in the hole's coordinate
                # space, then transform the complete even-odd clip once.
                # Direct2D SVG does not implement masks.
                outer = shapes[0]
                values = [float(v) for v in re.findall(r"-?[\d.]+", outer.get("d", ""))]
                if tag(outer) != "path" or len(values) != 5 or values[0] != values[4] or re.findall(r"[A-Za-z]", outer.get("d", "")) != ["M", "h", "v", "H", "z"]:
                    raise ValueError("Unsupported transformed mask boundary")
                transform = shapes[1].get("transform")
                a, b, c, d, e, f = parse_transform(transform)
                det = a * d - b * c
                if abs(det) < 1e-12:
                    raise ValueError("Singular mask transform")
                inverse = (d / det, -b / det, -c / det, a / det,
                           (c * f - d * e) / det, (b * e - a * f) / det)
                rectangle = ET.Element(
                    f"{{{SVG}}}rect", x=fmt(values[0]), y=fmt(values[1]),
                    width=fmt(values[2]), height=fmt(values[3]),
                    transform="matrix(" + " ".join(fmt(v) for v in inverse) + ")")
                data = rect_path(rectangle, False) + shapes[1].get("d")
            else:
                data = shape_path(shapes[0], False) + "".join(
                    shape_path(shape, True) for shape in shapes[1:])
            clip = ET.SubElement(defs, f"{{{SVG}}}clipPath",
                                 id=f"m{len(defs)}")
            path = ET.SubElement(clip, f"{{{SVG}}}path", d=data,
                                 **{"clip-rule": "evenodd"})
            if transform:
                path.set("transform", transform)
            element.set("clip-path", f"url(#{clip.get('id')})")
        element.attrib.pop("class", None)
        element.attrib.pop("style", None)
    frame_clip = ET.SubElement(defs, f"{{{SVG}}}clipPath", id=f"f{index}")
    ET.SubElement(frame_clip, f"{{{SVG}}}rect", x=fmt(vx), y=fmt(vy),
                  width=fmt(vw), height=fmt(vh))
    group = ET.Element(f"{{{SVG}}}g", {
        "transform": f"translate({fmt(x)} 0) scale({fmt(width / vw)} "
                     f"{fmt(height / vh)}) translate({fmt(-vx)} {fmt(-vy)})",
        "clip-path": f"url(#f{index})",
    })
    group.extend(list(frame))
    return group


def glow_frame(frame):
    """Retain Zen's dark-theme glow geometry, transforms, masks and opacity."""
    frame = copy.deepcopy(frame)

    def retain(element):
        for child in list(element):
            kind = tag(child)
            if kind == "defs":
                continue
            classes = child.get("class", "")
            if kind in ("g", "svg"):
                retain(child)
            elif "glow" not in classes:
                element.remove(child)
                continue
            if "glow-rays" in classes:
                child.set("stroke", "white")
                child.set("fill", "none")
            elif "glow-bg" in classes or "glow-element" in classes:
                child.set("fill", "white")
                child.set("stroke", "none")

    retain(frame)
    return frame


def box_frame(frame):
    """Keep Zen's stationary box, with the source's inherited fill rule."""
    frame = copy.deepcopy(frame)

    def retain(element):
        for child in list(element):
            kind = tag(child)
            if kind == "defs":
                continue
            if kind in ("g", "svg"):
                retain(child)
            elif child.get("class") != "zen-library-box":
                element.remove(child)

    retain(frame)
    return frame


def convert(path, glow=False, box=False):
    lines = path.read_text(encoding="utf-8").splitlines()
    text = re.sub(r"<!--.*?-->", "",
                  "\n".join(l for l in lines if not l.startswith("#")),
                  flags=re.S)
    root = ET.fromstring(text)
    frames = [child for child in root if tag(child) == "svg"]
    expected_frames = 90 if path.stem == "boost-indicator" else 36
    if len(frames) != expected_frames:
        raise ValueError(f"{path.name}: expected {expected_frames} frames")
    if box:
        frames = frames[:1]
    width = frames[0].get("width") if box else root.get("width")
    out = ET.Element(f"{{{SVG}}}svg", {
        "width": width,
        "height": root.get("height"),
        "viewBox": f"0 0 {width} {root.get('height')}",
    })
    for key in ("fill", "fill-opacity", "stroke-linecap", "stroke-linejoin", "stroke-width"):
        if root.get(key):
            out.set(key, root.get(key))
    if glow:
        out.set("fill", "white")
        out.set("fill-opacity", "1")
    defs = ET.SubElement(out, f"{{{SVG}}}defs")
    for index, frame in enumerate(frames):
        selected = glow_frame(frame) if glow else box_frame(frame) if box else frame
        out.append(convert_frame(
            selected, index, defs, glow,
            exclude_box=path.stem == "library-sprite" and not glow and not box))
    result = ET.tostring(out, encoding="unicode")
    for name, token in TOKENS.items():
        result = result.replace(f'"{name}"', f'"{token}"')
    if re.search(r"<(mask|style|filter)\b|context-|mix-blend", result):
        raise ValueError(f"{path.name}: unsupported content remains")
    return result


def literal(text):
    chunks = [text[i:i + CHUNK] for i in range(0, len(text), CHUNK)]
    if any(")KRSVG\"" in chunk for chunk in chunks):
        raise ValueError("Delimiter collision")
    return ",\n".join(f'    R"KRSVG({chunk})KRSVG"' for chunk in chunks), \
        len(chunks)


def main() -> None:
    source, output = map(Path, sys.argv[1:])
    blocks = []
    for identifier, name in SPRITES.items():
        body, count = literal(convert(source / f"{name}.svg"))
        blocks.append(
            f"inline constexpr std::array<std::string_view, {count}> "
            f"{identifier}{{\n{body}}};")
    body, count = literal(convert(source / "library-sprite.svg", glow=True))
    blocks.append(
        f"inline constexpr std::array<std::string_view, {count}> "
        f"kLibraryGlowSprite{{\n{body}}};")
    body, count = literal(convert(source / "library-sprite.svg", box=True))
    blocks.append(
        f"inline constexpr std::array<std::string_view, {count}> "
        f"kLibraryBox{{\n{body}}};")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "// Generated from design/icons/library; do not edit. Source assets\n"
        "// retain MPL-2.0 notices. Colour tokens: KRFILL, KRSTROKE,\n"
        "// KRFILLOPACITY.\n"
        "#pragma once\n#include <array>\n#include <string_view>\n"
        "namespace kr::design::sprites {\n"
        "inline constexpr int kFrames = 36;\n"
        "inline constexpr int kBoostIndicatorFrames = 90;\n"
        + "\n".join(blocks)
        + "\n}  // namespace kr::design::sprites\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
