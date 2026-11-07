"""Compile the small, filled SVG icons used by the native chrome."""

from pathlib import Path
import sys
import xml.etree.ElementTree as ET
from gen_icon_packs import normalize, shape


def main() -> None:
    source, output = map(Path, sys.argv[1:])
    declarations = []
    for name in ("sidebar", "add", "sparkles", "face-sun", "moon-stars", "algorithm", "library", "site-tools", "dart-down", "globe",
                 # Library, download stack and media cards.
                 "back", "forward", "close", "security", "screen",
                 "media-play", "media-pause", "media-next", "media-previous",
                 "media-mute", "media-unmute", "microphone-fill",
                 "microphone-blocked-fill", "video-fill", "video-blocked-fill",
                 "site-tools-active",
                 "boost-dice", "boost-code", "boost-share", "boost-zap",
                 "boost-focus", "boost-controls", "boost-appearance", "boost-disable",
                 "boost-close", "boost-magic", "boost-size", "boost-case",
                 "boost-uppercase", "boost-lowercase", "boost-chevron", "boost-delete"):
        # Source assets contain build-preprocessor directives and license
        # comments prefixed with '#'; preserve the files and parse their SVG.
        svg = "\n".join(
            line for line in (source / f"{name}.svg").read_text(encoding="utf-8").splitlines()
            if not line.startswith("#")
        )
        root = ET.fromstring(svg)
        x, y, width, height = map(float, root.attrib["viewBox"].split())
        if x or y:
            raise ValueError(f"Unsupported viewBox origin in {name}")
        paths = []
        styles = set()
        evenodd = False

        def visit(element, inherited):
            nonlocal evenodd
            style = inherited | element.attrib
            tag = element.tag.rsplit("}", 1)[-1]
            if tag == "defs":
                return
            if "transform" in element.attrib:
                raise ValueError(f"Unsupported SVG element in {name}")
            if tag in ("svg", "g", "defs"):
                for child in element:
                    visit(child, style)
                return
            stroke = style.get("stroke", "none")
            width = float(style.get("stroke-width", "1")) if stroke != "none" else 0
            if width and style.get("fill", "none") != "none":
                raise ValueError(f"Mixed fill/stroke icon in {name}")
            styles.add(width)
            evenodd |= style.get("fill-rule") == "evenodd"
            paths.append(shape(element))

        if name.startswith("boost-") or name == "site-tools-active":
            visit(root, {})
            if name == "site-tools-active":
                # The filled source uses a white cover with black cut-outs.
                # Emit native geometry, as with the sprite mask conversion.
                for mask in root.iter(f"{{{root.tag.split('}')[0][1:]}}}mask"):
                    for element in mask:
                        if element.get("fill") in ("black", "#000"):
                            paths.append(shape(element))
                            evenodd = True
        else:
            for element in root:
                if element.tag.rsplit("}", 1)[-1] != "path" or "transform" in element.attrib:
                    raise ValueError(f"Unsupported SVG element in {name}")
                paths.append(element.attrib["d"])
            styles.add(0)
        if len(styles) != 1:
            raise ValueError(f"Inconsistent icon style in {name}")
        stroke = styles.pop()
        data = " ".join(paths)
        if name.startswith("site-tools"):
            # Match SVG subpath origins and arc syntax in XAML's mini-language.
            data = normalize(data)
        identifier = ''.join(part.title() for part in name.split('-'))
        declarations.append(
            f'inline constexpr Icon k{identifier}{{LR"(F{0 if evenodd else 1} {data})", {width:g}, {height:g}, {stroke:g}}};'
        )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "// Generated from design/icons; do not edit. Source assets retain their license notices.\n"
        "#pragma once\n#include <string_view>\nnamespace kr::design::icons {\n"
        "struct Icon { std::wstring_view data; double width; double height; double stroke = 0; };\n"
        + "\n".join(declarations)
        + "\n}  // namespace kr::design::icons\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
