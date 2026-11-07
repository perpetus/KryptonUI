"""Import theme-picker constants and filled icons from the checked-in reference.

Run explicitly when updating reference/desktop; normal builds use design/.
Source files retain their Mozilla Public License 2.0 notices.
"""
from pathlib import Path
import json
import re
import shutil
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
reference = root / "reference/desktop/src"
panel = (reference / "browser/base/content/zen-panels/theme-picker.inc").read_text()
presets = []
page = 0
for group in re.findall(r"<hbox>\s*((?:#.*\n|\s|<box[^>]+/>|<box[^>]+></box>)+)\s*</hbox>", panel):
    boxes = re.findall(r"<box\b([^>]+)(?:/>|></box>)", group)
    if not boxes:
        continue
    for attrs in boxes:
        attrs = dict(re.findall(r'([\w-]+)="([^"]*)"', attrs))
        if "data-position" not in attrs:
            continue
        x, y = map(float, attrs["data-position"].split(","))
        style = attrs.get("style", "")
        color = re.search(r"#([0-9a-fA-F]{6})", style)
        rgb = re.search(r"rgb\((\d+),\s*(\d+),\s*(\d+)\)", style)
        rgba = "#FF" + color[1].upper() if color else "#FF" + "".join(f"{int(v):02X}" for v in rgb.groups())
        presets.append({"$type": "EditorPreset", "page": page, "color": rgba,
                        "x": x, "y": y, "lightness": float(attrs.get("data-lightness", 50)),
                        "points": int(attrs["data-num-dots"]),
                        "harmony": 4 if attrs["data-algo"] == "analogous" else 0,
                        "mode": 2 if attrs.get("data-type") == "explicit-black-white" else 1})
    page += 1
assert len(presets) == 41 and page == 5, (len(presets), page)
script = (reference / "zen/spaces/ZenGradientGenerator.mjs").read_text()
wave = re.search(r"#sinePath = `([^`]+)`", script)[1]
editor = {
    "$comment": "ZenGradientGenerator.mjs, zen-gradient-generator.css and theme-picker.inc. Source CSS rem units use the native type.size token. Point geometry is normalized against the reference's 358-DIP board.",
    "width": 380, "padding": 10, "borderWidth": 1, "radius": 8,
    "boardRadiusInset": 4, "boardDotPitch": 6, "boardDotRadius": 1,
    "boardDotPhase": 1, "schemeTop": 15, "actionBottom": 8,
    "buttonSize": 30, "buttonGap": 5, "iconSize": 18,
    "primarySize": 38, "primaryBorder": 6, "secondarySize": 16, "secondaryBorder": 3,
    "swatchSize": 26, "pageButtonSize": 28, "rowGap": 10,
    "controlGapRem": 1.5, "dialSizeRem": 5, "dialInnerFraction": 0.6,
    "dialDots": 16, "dialDotSize": 4, "dialInactiveOpacity": 0.4,
    "dialHandlerWidth": 6, "dialHandlerHeight": 8, "dialHandlerRadius": 2,
    "thumbHeight": 40, "thumbWidth": 10, "thumbGrowth": 15,
    "opacityStep": 0.001, "waveWidth": 455, "waveHeight": 70,
    "waveOriginY": -7.605, "waveReferenceY": 27.3, "waveStroke": 8,
    "wavePath": wave, "colorReferenceSize": 358, "colorPadding": 30, "colorOffset": 29,
    "colorDistanceBias": 0.2, "keyboardStep": 0.01,
    "meshSubdivisions": 16
}
colors = {
    "$type": "EditorPalette", "board": "#0D000000", "dots": "#1A000000",
    "foreground": "#B3000000", "hover": "#1A000000", "selection": "#1A000000",
    "dialDots": "#4D000000", "dialHandler": "#FF757575", "wave": "#FF5A5A5A",
    "waveMuted": "#804D4D4D", "thumb": "#FF000000", "pointBorder": "#FFFFFFFF"
}
dark = dict(colors, board="#08FFFFFF", dots="#0DFFFFFF", foreground="#E6FFFFFF",
            hover="#1AFFFFFF", selection="#1AFFFFFF", dialDots="#4DFFFFFF",
            dialHandler="#FFD1D1D1", wave="#FFA1A1A1", waveMuted="#80A1A1A1", thumb="#FFFFFFFF")
token_path = root / "design/tokens.json"
text = token_path.read_text()
assert '"themeEditor"' not in text
blocks = []
for name, value in [("themeEditor", editor), ("editorLight", colors), ("editorDark", dark)]:
    blocks.append('  "' + name + '": ' + json.dumps(value, indent=2).replace('\n', '\n  ') + ',\n')
blocks.append('  "editorPresets": [\n' + ',\n'.join('    ' + json.dumps(p) for p in presets) + '\n  ],\n')
text = text.replace('  "tabHover": {', '\n'.join(blocks) + '\n  "tabHover": {', 1)
token_path.write_text(text)
icons = reference / "browser/themes/shared/zen-icons/nucleo"
for name in ["sparkles", "face-sun", "moon-stars", "algorithm"]:
    shutil.copyfile(icons / (name + ".svg"), root / "design/icons" / (name + ".svg"))
textures = root / "design/textures"
textures.mkdir(exist_ok=True)
shutil.copyfile(reference / "zen/images/grain-bg.png", textures / "grain-bg.png")
print(f"Imported {len(presets)} swatches, theme-picker tokens, four icons and the grain asset.")
