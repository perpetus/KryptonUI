# Credits and provenance

KryptonUI is developed by the Krypton Authors. It is a native browser UI, with
platform-specific rendering and a shared C++ behavior layer. The following
projects and contributors supplied code, artwork, data, platform libraries or
design references. Their work is credited independently of whether a research
checkout is included in this repository.

| Project or contributors | Contribution | License or notice |
| --- | --- | --- |
| [Zen Browser and its contributors](https://github.com/zen-browser/desktop) | Folder icon geometry and motion; Library, downloads and media assets; Boost editor and Tools indicator assets; theme grain; theme/editor behavior references | MPL-2.0 for imported and adapted covered files. Original headers are preserved; see [the MPL text](licenses/MPL-2.0.txt) and [third-party notices](THIRD_PARTY_NOTICES.md). The local implementation reference was revision `7a3552d2a9d7cba6464387e41a3fbdfab555ee7d`. Native C++/WinRT implementations adapt these ideas to Chromium and Windows. |
| [Helium / imputnet and contributors](https://github.com/imputnet/helium), [helium-windows contributors](https://github.com/imputnet/helium-windows) | Krypton's upstream browser and Windows integration; host and service behavior references | GPL-3.0 and retained upstream component licenses. Helium remains a pinned submodule. See Krypton's credits for the browser dependency chain. |
| [The Chromium Authors](https://www.chromium.org/) | Browser engine, host APIs and behavior | Chromium's BSD-style license and its component-specific licenses; see [Krypton](https://github.com/perpetus/Krypton/blob/main/CREDITS.md). |
| [Microsoft and WinUI contributors](https://github.com/microsoft/microsoft-ui-xaml) | WinUI 3, Windows App SDK, C++/WinRT, island/input behavior and native control guidance | Upstream MIT notices and Windows App SDK package terms. [WinUI license](licenses/WinUI-MIT.txt). Windows system fonts are used, not redistributed. |
| [Microsoft WinUI Gallery contributors](https://github.com/microsoft/WinUI-Gallery) | Fluent glyph metadata and native UI examples | MIT; [license](licenses/WinUI-Gallery-MIT.txt). Glyph metadata attribution is also preserved in [symbols.NOTICE](design/symbols.NOTICE). |
| [Unicode, Inc. and contributors](https://www.unicode.org/) | Emoji 17.0 sequences, descriptions and search metadata | Unicode License v3, reproduced in [symbols.NOTICE](design/symbols.NOTICE). Emoji rendering uses the installed Windows font. |
| [Microsoft Fluent System Icons](https://github.com/microsoft/fluentui-system-icons) | Default native icon pack | MIT; [license](design/icons/packs/fluent/LICENSE). |
| [Lucide contributors](https://github.com/lucide-icons/lucide) and Cole Bemis / Feather contributors | Optional Lucide icon pack and its Feather-derived portions | ISC and MIT; [full notices](design/icons/packs/lucide/LICENSE). |
| [Phosphor Icons contributors](https://github.com/phosphor-icons/core), [PaweÅ‚ Kuna / Tabler contributors](https://github.com/tabler/tabler-icons), [WorkOS / Radix contributors](https://github.com/radix-ui/icons) | Optional native icon packs | MIT; licenses are retained in each [pack directory](design/icons/packs/). Versions and package integrity hashes are in [manifest.json](design/icons/packs/manifest.json). |
| [Pablo Hdez and SVGL contributors](https://github.com/pheralb/svgl), [Simple Icons contributors](https://github.com/simple-icons/simple-icons) | Onboarding brand artwork | MIT and CC0-1.0 respectively. Source revisions, metadata and full notices are in [onboarding/licenses](design/icons/onboarding/licenses/). |
| [Browser Logos contributors](https://github.com/alrra/browser-logos) and the respective product owners | Browser/search-provider identification artwork | Individual owners retain their artwork and trademark rights; see [the asset notice](design/icons/browsers/NOTICE.md). These marks are not relicensed as Krypton artwork. |
| [Kube](https://kube.io/blog/liquid-glass-css-svg#searchbox) | Liquid-glass optical model and filter-sequence research | Attribution and scope are recorded in [third-party notices](THIRD_PARTY_NOTICES.md). The research checkout is not vendored. |
| [hoshiizumiya / WinUI.Composition.Hlsl](https://github.com/Millennium-Science-Technology-R-D-Inst/WinUI.Composition.Hlsl) | Windows composition/glass research | The original MIT notice is reproduced in [third-party notices](THIRD_PARTY_NOTICES.md). KryptonUI does not load that library or its private interop. |

The layout-preview images contain demonstration browser data. Microsoft retains
rights in the Windows desktop artwork visible in those screenshots. Product
names and marks identify interoperability or provenance; they do not imply
endorsement or affiliation.

Only Helium and Microsoft WinUI Gallery remain as source submodules. Other
browser research checkouts are excluded from the public tree. Normal builds use
the checked-in source assets; optional import scripts may require an explicitly
obtained upstream reference checkout. Removing a checkout does not remove the
license, attribution or source-availability obligations of its derived files.

Original KryptonUI code is GPL-3.0; third-party exceptions are described above
and in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Preserve those notices in
redistributions. This document records provenance, not additional permissions
for third-party trademarks or artwork.
