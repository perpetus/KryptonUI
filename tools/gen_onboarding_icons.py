"""Embed the checked-in onboarding artwork. No downloads or runtime file I/O."""

import json
from pathlib import Path
import re
import struct
import sys
import xml.etree.ElementTree as ET

from gen_icon_packs import normalize


def string(text):
    return json.dumps(text, ensure_ascii=False)


def main():
    manifest, output = map(Path, sys.argv[1:])
    root = manifest.parent
    entries = json.loads(manifest.read_text(encoding='utf-8'))
    declarations, browsers, domains, searches = [], [], [], []
    names = set()
    for item in entries:
        name = item['id']
        if name in names or not re.fullmatch('[a-z][a-z0-9-]*', name):
            raise ValueError(f'Invalid or duplicate icon id: {name}')
        names.add(name)
        identifier = 'k' + ''.join(part.title() for part in name.split('-'))
        source = root / item['file']
        geometry, svg, dark_svg, png = 'L""', '""', '""', '{}'
        if source.suffix == '.svg':
            text = source.read_text(encoding='utf-8')
            element = ET.fromstring(text)
            if item.get('tint'):
                if element.attrib.get('viewBox') != '0 0 24 24':
                    raise ValueError(f'Unsupported monochrome viewBox: {source}')
                paths = []
                for child in element:
                    tag = child.tag.rsplit('}', 1)[-1]
                    if tag == 'title':
                        continue
                    if tag != 'path' or 'transform' in child.attrib:
                        raise ValueError(f'Unsupported monochrome element: {source}')
                    # Reuse the chrome compiler's SVG-to-XAML normalization:
                    # WinUI's current point after Z differs from SVG's.
                    paths.append(normalize(child.attrib['d']))
                geometry = 'L' + string('F1 ' + ' '.join(paths))
            else:
                svg = string(text)
                if 'dark_file' in item:
                    dark_text = (root / item['dark_file']).read_text(encoding='utf-8')
                    ET.fromstring(dark_text)
                    dark_svg = string(dark_text)
        elif source.suffix == '.png':
            data = source.read_bytes()
            if data[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack('>II', data[16:24]) == (0, 0):
                raise ValueError(f'Invalid PNG: {source}')
            declarations.append(f'constexpr std::uint8_t {identifier}Png[] = {{\n' +
                                '\n'.join(','.join(f'0x{x:02x}' for x in data[i:i + 24]) + ','
                                          for i in range(0, len(data), 24)) + '\n};')
            png = identifier + 'Png'
        else:
            raise ValueError(f'Unsupported icon format: {source}')
        declarations.append(f'constexpr OnboardingIcon {identifier}{{{string(name)}, {geometry}, '
                            f'{svg}, {dark_svg}, {png}}};')
        for field, records in [('browsers', browsers), ('domains', domains), ('search', searches)]:
            records.extend('{' + string(alias) + ', &' + identifier + '}'
                           for alias in item.get(field, []))
    for name, records in [('Browsers', browsers), ('Domains', domains), ('Searches', searches)]:
        declarations.append(f'constexpr std::array k{name}{{\n' +
                            ',\n'.join('IconAlias' + value for value in records) + '\n};')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('// Generated from design/icons/onboarding.json; do not edit.\n'
                      '// Artwork sources and licenses: design/icons/onboarding/licenses/.\n'
                      '#pragma once\n#include <array>\n'
                      '#include "krypton/ui/presentation/onboarding_icons.hpp"\n'
                      'namespace kr::design::onboarding_icons {\n'
                      'struct IconAlias { std::string_view text; const OnboardingIcon* icon; };\n' +
                      '\n'.join(declarations) + '\n}\n', encoding='utf-8')


if __name__ == '__main__':
    main()
