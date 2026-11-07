"""Decode the reference's indexed PNG at build time; no runtime file I/O."""
from pathlib import Path
import struct
import sys
import zlib


def main():
    source, output = map(Path, sys.argv[1:])
    data = source.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    offset, compressed, palette, transparency = 8, bytearray(), b'', b''
    while offset < len(data):
        size, kind = struct.unpack('>I4s', data[offset:offset + 8])
        chunk = data[offset + 8:offset + 8 + size]
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlaced = struct.unpack('>IIBBBBB', chunk)
            assert (depth, color, interlaced) == (8, 3, 0)
        elif kind == b'PLTE': palette = chunk
        elif kind == b'tRNS': transparency = chunk
        elif kind == b'IDAT': compressed.extend(chunk)
        offset += size + 12
    raw = zlib.decompress(compressed)
    previous = bytearray(width)
    indices = bytearray()
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        row = bytearray(raw[offset + 1:offset + 1 + width])
        offset += width + 1
        for x in range(width):
            left, up = (row[x - 1] if x else 0), previous[x]
            upper_left = previous[x - 1] if x else 0
            if filter_type == 1: predictor = left
            elif filter_type == 2: predictor = up
            elif filter_type == 3: predictor = (left + up) // 2
            elif filter_type == 4:
                p = left + up - upper_left
                a, b, c = abs(p - left), abs(p - up), abs(p - upper_left)
                predictor = left if a <= b and a <= c else up if b <= c else upper_left
            else:
                assert filter_type == 0
                predictor = 0
            row[x] = (row[x] + predictor) & 255
        indices.extend(row)
        previous = row
    colors = []
    for i in range(len(palette) // 3):
        r, g, b = palette[i * 3:i * 3 + 3]
        alpha = transparency[i] if i < len(transparency) else 255
        if alpha == 0:
            colors.append(0)
            continue
        assert r == g == b, 'Visible grain pixels must be grayscale'
        strength = round(abs(2 * r - 255) * alpha / 255)
        # Hard-light grayscale is equivalent to a black/white alpha overlay.
        rgb = strength if r >= 128 else 0
        colors.append((strength << 24) | (rgb << 16) | (rgb << 8) | rgb)
    pixels = [f'0x{colors[i]:08X}u' for i in indices]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('// Generated from design/textures/grain-bg.png (MPL-2.0); do not edit.\n'
                      '#pragma once\n#include <array>\n#include <cstdint>\nnamespace kr::design {\n'
                      f'inline constexpr int kGrainWidth = {width};\ninline constexpr int kGrainHeight = {height};\n'
                      f'inline constexpr std::array<std::uint32_t, {len(pixels)}> kGrainPixels{{{{\n'
                      + ',\n'.join(','.join(pixels[i:i + 16]) for i in range(0, len(pixels), 16))
                      + '\n}};\n}\n', encoding='utf-8')


if __name__ == '__main__': main()
