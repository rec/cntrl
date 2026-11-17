# Additional font sources

## Utility body font

`TamzenBold7x13` is the default `page_settings.status_font`, shared by utility-page
body text. It uses the native Tamzen Bold 7x13 bitmap, with seven-pixel visible
capitals. Only blank bitmap rows are trimmed for compact layouts; pixels,
baseline positions and seven-pixel character advances are preserved.
Headings retain Atkinson Hyperlegible Next SemiBold.
See [the pinned sources and regeneration command](bitmap_samples/README.md).

The agreed **base characters** remain the 139-character repertoire: printable
ASCII, the French repertoire, and `Ä Å Ñ Ö Ā Ō ä å ñ ö ā ō` for familiar English
loanwords. The selected Status asset now includes all 182 drawable characters
Tamzen supplies from the 373-character comparison repertoire and occupies 5723 bytes.
The bitmap drawings for `Œ œ Ÿ Ā ā Ō ō` supplement the pinned upstream source.

English and French packs now reuse flash-resident glyphs and carry no font data.
The renderer still supports a language-specific font and Status supplement when
a pack supplies one, preserving the native Status baseline and advances.

Regenerate the Status base offline with:

```sh
uv run --no-sync python -m cntrl.fonts.status
```

## Shared flash repertoire

`characters.py` records the fixed 373-character comparison repertoire. Fonts
include every character they actually support, excluding the soft-hyphen format
control. Missing glyphs are omitted rather than replaced by a missing-glyph box.
Arimo contains all 372 drawable characters. This is a comparison repertoire,
not a guarantee of support for every smaller European language.

The four UI families retain their existing sizes: Tamzen 13; Courier Prime
12, 16, 20, 24, 28, 32, 40, 48; Atkinson and Arimo 8 through 32, then
40, 48, 60, 72, 96. Other bundled performance families use exactly one size:
the largest whose full glyph bounds, including accents and descenders, fit
one third of the target display height with two blank pixels above and below.
Long performance labels end with an ellipsis instead of selecting a smaller size.
Different performance families may therefore use different nominal point sizes.
Custom font overrides, explicit horizontal scaling and authored Unicode outside
the shared repertoire retain the existing sparse setup-asset path.

The adapter's export command regenerates the portable files and flash header;
the target build calls it automatically. See the adapter font documentation
for its commands. The firmware and emulator use the same portable resources.

Always use FreeType's automatic hinting when generating shared outline-font
assets, including performance fonts. Do not disable it or force native-only
hinting. Font sizing must measure the same automatically hinted glyphs that are
exported. The packaged-font tests compare their pixels, bearings and advances
with automatically hinted source glyphs.

## Arimo UI trial

`Arimo` and `ArimoBold` use the unmodified Regular and Bold TrueType outlines from
[googlefonts/Arimo revision 22e482e2ce0ebb2e6f88ba49d029d60f558d5d67](https://github.com/googlefonts/Arimo/tree/22e482e2ce0ebb2e6f88ba49d029d60f558d5d67).
It was downloaded on 2026-11-16 with its [SIL OFL 1.1 licence](arimo/OFL.txt).
The source SHA-256 values are:

```text
41b22bc8f0b51f932825d37bc55b5eb6ba67dfe599a626e4aff2b43b624f9f8c  Arimo-Regular.ttf
d7a8b187cf8444d4cfee102e8eae9e3043682fd5106d5d33ed677fe268a0e2ba  Arimo-Bold.ttf
```

The portable monochrome UI asset is generated at sizes 8 through 32 and
40, 48, 60, 72 and 96. The shared asset exporter now uses FreeType's automatic
hinting for Arimo along with the other outline families.

Arimo Regular and Bold were previewed through the shared `page_settings.status_font`
setting before selecting Tamzen Bold 7x13.
In the emulator preview, native hinting improved readability compared with
forced automatic hinting, but the small Status text was still unsatisfactory.

## Other sources

These unmodified regular-weight TrueType files were downloaded on
2026-11-02 from [Google Fonts at revision 6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec).
Only the generated bitmap versions use the selectable names below.

| Source family | Selectable bitmap name | Source file | License |
| --- | --- | --- | --- |
| [Righteous](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec/ofl/righteous) | `DecoRound` | [Righteous-Regular.ttf](righteous/Righteous-Regular.ttf) | [SIL OFL 1.1](righteous/OFL.txt) |
| [DM Serif Display](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec/ofl/dmserifdisplay) | `DMSerifDisplay` | [DMSerifDisplay-Regular.ttf](dm-serif-display/DMSerifDisplay-Regular.ttf) | [SIL OFL 1.1](dm-serif-display/OFL.txt) |
| [Graduate](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec/ofl/graduate) | `CollegeBlock` | [Graduate-Regular.ttf](graduate/Graduate-Regular.ttf) | [SIL OFL 1.1](graduate/OFL.txt) |
| [Ewert](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec/ofl/ewert) | `OrnamentalSlab` | [Ewert-Regular.ttf](ewert/Ewert-Regular.ttf) | [SIL OFL 1.1](ewert/OFL.txt) |
| [Rubik Distressed](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec/ofl/rubikdistressed) | `RubikDistressed` | [RubikDistressed-Regular.ttf](rubik-distressed/RubikDistressed-Regular.ttf) | [SIL OFL 1.1](rubik-distressed/OFL.txt) |
| [Special Elite](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec/apache/specialelite) | `SpecialElite` | [SpecialElite-Regular.ttf](special-elite/SpecialElite-Regular.ttf) | [Apache 2.0](special-elite/LICENSE.txt) |

Righteous, Graduate and Ewert reserve their original names. Their
converted bitmap families use different names to respect those licenses.
DM Serif Display's reserved name is Source, which is not used by its bitmap
family. Rubik Distressed declares no reserved name. Preserve the complete
license and copyright notices alongside each source and derived font.

Special Elite is Copyright (c) 2010 by Brian J. Bonislawsky DBA Astigmatic
(AOETI), licensed under Apache 2.0. Its generated files identify the bitmap
conversion as a modification.

SHA-256 checksums of the original source files:

```text
2ffb3fe5c27d7e6571210b800448c4e234e651b46c6b4426c1bb567e5341348a  righteous/Righteous-Regular.ttf
8cc3643535edf039aa5d95440a8542735e9197e4f4b8d9303e980fefbf5ab616  dm-serif-display/DMSerifDisplay-Regular.ttf
971222b309851d86f2513f89b510dc52d52ef7798b5f43af2c6f58d43df568e5  graduate/Graduate-Regular.ttf
8a7eb23adce5193b61bc1843d752f37c208c163dd6db2fc1b64791791969f4ab  ewert/Ewert-Regular.ttf
64a9962c862451507b5d8dbc1c4bf023ddbcad701bc73d277eae46723718327a  rubik-distressed/RubikDistressed-Regular.ttf
a776fcb4ceb8bdf03e2967688ebdad42680de5b91a7e62c17e718ae212d14bc4  special-elite/SpecialElite-Regular.ttf
```


## Readability trial

The comparison setup [`readability-demo.toml`](../maker/morningstar/mc/readability-demo.toml)
selects these semibold faces explicitly through the custom-font path. Both are
unmodified upstream files, licensed under SIL OFL 1.1 without Reserved Font Names
in their bundled licenses. They were downloaded on 2026-11-02.

| Family | Source revision | Font | License |
| --- | --- | --- | --- |
| Atkinson Hyperlegible Next SemiBold | [7925f50f](https://github.com/googlefonts/atkinson-hyperlegible-next/tree/7925f50f649b3813257faf2f4c0b381011f434f1) | [TTF](atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-SemiBold.ttf) | [OFL](atkinson-hyperlegible-next/OFL.txt) |
| Barlow Condensed SemiBold | [6c1dbe27](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec/ofl/barlowcondensed) | [TTF](barlow-condensed/BarlowCondensed-SemiBold.ttf) | [OFL](barlow-condensed/OFL.txt) |

Source SHA-256 checksums:

```text
8e2fff779f35ac25239656bb0f3b2bd35959a2009e3dd21a12141c3b9030362b  atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-SemiBold.ttf
7b619d14bc2327509a9ef32b0890f709626f7ecc9ff61191c2a4314c5499d2d9  barlow-condensed/BarlowCondensed-SemiBold.ttf
```

The comparison displays identical text in Atkinson on the second line and Barlow
on the third, using their shared fitting size. Browse the four samples using the
normal patch navigation. Sample actions are empty sequences and send no MIDI.
The existing `simple-setup` remains available in the setup selector.
