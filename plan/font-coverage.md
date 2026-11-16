# Base font coverage

This report checks the agreed **139-character base repertoire** against the
actual committed font files, without downloading fonts or changing assets.
It includes current bundled sources, prepared assets, language packs and generated
font declarations. Historical archives and test-only fixtures are excluded.

## Base repertoire

* 95 printable ASCII characters, U+0020 through U+007E, including space.
* 32 French letters: `à â æ ç è é ê ë î ï ô œ ù û ü ÿ` and their uppercase forms.
* 12 additional letters: `ä å ñ ö ā ō` and `Ä Å Ñ Ö Ā Ō`.

This is the specifically agreed repertoire, not every character that can appear
in French typography or English loanwords. Curly quotes, nonbreaking spaces,
guillemets, currency symbols and decomposed combining sequences are outside it.
Counts refer to Unicode characters, not necessarily distinct bitmap shapes.

## Method and interpretation

Regenerate from the repository root:

```sh
uv run --no-sync python -m cntrl.maker.morningstar.mc.font_coverage
```

Outline coverage uses each local TTF/OTF Unicode cmap and does not claim visual
quality at small sizes. Packaged CFN2 coverage uses the runtime character map at
every stored size, with separate rows if coverage differs. Prepared whole-string
runs do not establish general runtime character support. Embedded portable arrays
are decoded using the same CFN2 reader. Legacy generated bitmap coverage uses the
character ranges in its font declarations. This does not launch the application.
Files are read from Git HEAD, excluding concurrent uncommitted font edits.

Source coverage and packaged coverage are separate: a complete outline does not
mean its selected glyphs have been included in a generated asset. In particular,
ASCII-only bitmap assets omit all 44 non-ASCII base extensions.
The upstream bitmap source files are not bundled, so their broader repertoire is
not inferred from these ASCII-only samples. Language-pack fonts are subsets for
their catalogue text, not a promise of full base-font coverage.

Inspected source revision: `569f60abac47baf26492425524236872a1541f39`.

## Outline sources

| Font / asset | Base covered | Missing set |
| --- | ---: | --- |
| ArimoBold: `cntrl/fonts/arimo/Arimo-Bold.ttf` | 139/139 | None |
| Arimo: `cntrl/fonts/arimo/Arimo-Regular.ttf` | 139/139 | None |
| AtkinsonNextSemiBold: `cntrl/fonts/atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-SemiBold.ttf` | 137/139 | M1 |
| BarlowCondensed-SemiBold: `cntrl/fonts/barlow-condensed/BarlowCondensed-SemiBold.ttf` | 139/139 | None |
| CaveatBold: `cntrl/fonts/caveat/Caveat-Bold.ttf` | 139/139 | None |
| CourierPrimeBold: `cntrl/fonts/courier-prime/CourierPrime-Bold.ttf` | 139/139 | None |
| CourierPrime: `cntrl/fonts/courier-prime/CourierPrime-Regular.ttf` | 139/139 | None |
| DMSerifDisplay: `cntrl/fonts/dm-serif-display/DMSerifDisplay-Regular.ttf` | 139/139 | None |
| OrnamentalSlab: `cntrl/fonts/ewert/Ewert-Regular.ttf` | 133/139 | M2 |
| CollegeBlock: `cntrl/fonts/graduate/Graduate-Regular.ttf` | 139/139 | None |
| MetalLord: `cntrl/fonts/metal-lord/MetalLord.otf` | 139/139 | None |
| CircuitBold: `cntrl/fonts/orbitron/Orbitron-Bold.ttf` | 134/139 | M3 |
| Circuit: `cntrl/fonts/orbitron/Orbitron-Medium.ttf` | 134/139 | M3 |
| DecoRound: `cntrl/fonts/righteous/Righteous-Regular.ttf` | 139/139 | None |
| RubikDistressed: `cntrl/fonts/rubik-distressed/RubikDistressed-Regular.ttf` | 139/139 | None |
| SpecialElite: `cntrl/fonts/special-elite/SpecialElite-Regular.ttf` | 139/139 | None |
| StillTime: `cntrl/fonts/still-time/StillTime.otf` | 133/139 | M4 |

## Packaged bitmap assets

| Font / asset | Base covered | Missing set |
| --- | ---: | --- |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-10-b.cfont` (size 10) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-10-r.cfont` (size 10) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-11-b.cfont` (size 11) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-11-r.cfont` (size 11) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-12-b.cfont` (size 12) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-12-r.cfont` (size 12) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-13-b.cfont` (size 13) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-13-r.cfont` (size 13) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Spleen-spleen-5x8.cfont` (size 8) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Spleen-spleen-6x12.cfont` (size 12) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Spleen-spleen-8x16.cfont` (size 16) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen5x9b.cfont` (size 9) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen5x9r.cfont` (size 9) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen6x12b.cfont` (size 12) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen6x12r.cfont` (size 12) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen7x13b.cfont` (size 13) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen7x13r.cfont` (size 13) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen7x14b.cfont` (size 14) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen7x14r.cfont` (size 14) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen8x15b.cfont` (size 15) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen8x15r.cfont` (size 15) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen8x16b.cfont` (size 16) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen8x16r.cfont` (size 16) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/TamzenBold7x13.cfont` (size 13) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-11.cfont` (size 11) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-11b.cfont` (size 11) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-12.cfont` (size 12) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-12b.cfont` (size 12) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-13.cfont` (size 13) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-13b.cfont` (size 13) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-14.cfont` (size 14) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-14b.cfont` (size 14) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-15.cfont` (size 15) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-15b.cfont` (size 15) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-16.cfont` (size 16) | 95/139 | M5 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-16b.cfont` (size 16) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/distribution/fonts/Arial.cfont` (18 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/distribution/fonts/Arimo.cfont` (30 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/distribution/fonts/ArimoBold.cfont` (30 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/distribution/fonts/ComicSansMS.cfont` (18 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/distribution/fonts/TamzenBold7x13.cfont` (size 13) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/distribution/fonts/classic.cfont` (18 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/font_sources/Arial.cfont` (18 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/font_sources/Arimo.cfont` (30 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/font_sources/ArimoBold.cfont` (30 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/font_sources/ComicSansMS.cfont` (18 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/font_sources/classic.cfont` (18 sizes: 8–96) | 95/139 | M5 |

## Language-pack fonts

| Font / asset | Base covered | Missing set |
| --- | ---: | --- |
| `cntrl/localization/packs/en.bin` (6 sizes: 8–20) | 105/139 | M6 |
| `cntrl/localization/packs/fr.bin` (6 sizes: 8–20) | 105/139 | M6 |
| `cntrl/maker/morningstar/mc/distribution/languages/en.bin` (6 sizes: 8–20) | 105/139 | M6 |
| `cntrl/maker/morningstar/mc/distribution/languages/fr.bin` (6 sizes: 8–20) | 105/139 | M6 |

## Embedded portable UI fonts

| Font / asset | Base covered | Missing set |
| --- | ---: | --- |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: classic` (18 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: Arial` (18 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: ComicSansMS` (18 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: StillTime` (8 sizes: 12–48) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: CourierPrime` (8 sizes: 12–48) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: CourierPrimeBold` (8 sizes: 12–48) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: AtkinsonNextSemiBold` (30 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: Arimo` (30 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: ArimoBold` (30 sizes: 8–96) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: TamzenBold7x13` (size 13) | 95/139 | M5 |

## Legacy generated bitmap fonts

| Font / asset | Base covered | Missing set |
| --- | ---: | --- |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_AtkinsonNextSemiBold.cpp` (30 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CaveatBold.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_Circuit.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CircuitBold.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CollegeBlock.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CourierPrime.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CourierPrimeBold.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_DMSerifDisplay.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_DecoRound.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_MetalLord.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_OrnamentalSlab.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_RubikDistressed.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_SpecialElite.cpp` (8 sizes) | 95/139 | M5 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_StillTime.cpp` (8 sizes) | 95/139 | M5 |

## Exact missing characters

Characters are ordered by Unicode code point. Each set applies to every row referencing it.

### M1: 2 missing

`Ō` `ō`

### M2: 6 missing

`Ā` `ā` `Ō` `ō` `Œ` `œ`

### M3: 5 missing

`^` `Ā` `ā` `Ō` `ō`

### M4: 6 missing

`[` `]` `{` `|` `}` `~`

### M5: 44 missing

`À` `Â` `Ä` `Å` `Æ` `Ç` `È` `É` `Ê` `Ë` `Î` `Ï` `Ñ` `Ô` `Ö` `Ù` `Û` `Ü` `à` `â` `ä` `å` `æ` `ç` `è` `é` `ê` `ë` `î` `ï` `ñ` `ô` `ö` `ù` `û` `ü` `ÿ` `Ā` `ā` `Ō` `ō` `Œ` `œ` `Ÿ`

### M6: 34 missing

`À` `Â` `Ä` `Å` `Æ` `Ç` `Ê` `Ë` `Î` `Ï` `Ñ` `Ô` `Ö` `Ù` `Û` `Ü` `ä` `å` `æ` `ë` `î` `ï` `ñ` `ö` `ù` `û` `ü` `ÿ` `Ā` `ā` `Ō` `ō` `Œ` `Ÿ`

## Additional work beyond the prompt

None. The inspection script is retained to reproduce this report; no font assets or rendering behavior were changed.
