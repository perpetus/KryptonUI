# Third-party notices

See [CREDITS.md](CREDITS.md) for the complete provenance inventory. KryptonUI's
GPL-3.0 license applies to its original code and does not replace the licenses
or notices of the following third-party work.

## Zen source assets and adaptations

Zen Browser and its contributors supplied the source assets for Library,
downloads, media, folders, Boost controls, the Tools Boost indicator and theme
grain. Covered SVG/source files retain their MPL-2.0 headers. A complete copy of
the license is included in [licenses/MPL-2.0.txt](licenses/MPL-2.0.txt). Public
source: <https://github.com/zen-browser/desktop>. Native Windows implementations
and build-time conversions adapt this material for KryptonUI; the research
checkout itself is not included as a public submodule.

## Icon packs, symbols and onboarding data

The pinned Fluent, Lucide, Phosphor, Tabler and Radix icon packs retain their
full MIT/ISC notices in `design/icons/packs/*/LICENSE`; package versions and
integrity hashes are in `design/icons/packs/manifest.json`. Fluent glyph
metadata and Unicode emoji data retain the complete MIT and Unicode License v3
notices in [design/symbols.NOTICE](design/symbols.NOTICE). Microsoft WinUI/WinUI
Gallery MIT license copies are also included in `licenses/`.

Browser and search-provider logos retain their owners' artwork and trademark
rights; [the asset notice](design/icons/browsers/NOTICE.md) explains their source
and identification-only purpose. The Windows desktop artwork visible in layout
preview screenshots remains Microsoft artwork.

## Zen folder icon

The vector shapes and open/close transforms in
`modules/shell/src/windowsMain/cpp/folder_icon.cpp` and its header are adapted
from Zen's `src/zen/folders/ZenFolder.mjs` and `zen-folder-icon.inc.css`, available
locally in `reference/desktop`. These files retain the Mozilla Public License
2.0 notice; the license is available at <https://mozilla.org/MPL/2.0/>. The
native implementation uses Windows compositor animations and design tokens.

## Liquid glass references

The project-owned liquid glass filter follows the optical model and Searchbox
filter sequence published by Kube in
[Liquid Glass in the Browser: Refraction with CSS and SVG](https://kube.io/blog/liquid-glass-css-svg#searchbox).
The reference is available locally at `reference/kube.io`, particularly
`graphics/Searchbox.tsx`, `components/Filter.tsx`, `lib/displacementMap.ts`,
`lib/specular.ts`, and `lib/surfaceEquations.ts` in the article's directory.
The C++/HLSL implementation retains this attribution for the refraction,
RG8 displacement normalization, and directional specular treatment.

Optical research also used `reference/WinUI.Composition.Hlsl`. KryptonUI does
not load that library or its private interop. Its license is reproduced here:

MIT License

Copyright (c) 2026 hoshiizumiya

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Onboarding brand icons

The onboarding subset in `design/icons/onboarding/` includes artwork from
[SVGL](https://github.com/pheralb/svgl) (MIT) and
[Simple Icons](https://github.com/simple-icons/simple-icons) (CC0-1.0).
Their complete license texts, pinned revisions, and available per-brand source
metadata are preserved in `design/icons/onboarding/licenses/`.
Dia's icon is its official favicon from <https://www.diabrowser.com/favicon-96x96.png>.
Brave and browser channel artwork also reuse the supplied `design/icons/browsers/`
files, copied from [Browser Logos](https://github.com/alrra/browser-logos).
