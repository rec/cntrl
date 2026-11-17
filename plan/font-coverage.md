# Base font coverage

This report checks the agreed **139 base characters** and the requested
**373-character comparison repertoire** against the actual tracked font files.
Soft hyphen U+00AD is a formatting control and is intentionally excluded from
generated standalone glyphs, so a complete generated face covers 372/373.
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
cntrl font-coverage
```

Outline coverage uses each local TTF/OTF Unicode cmap and does not claim visual
quality at small sizes. Packaged CFN2 coverage uses the runtime character map at
every stored size, with separate rows if coverage differs. Prepared whole-string
runs do not establish general runtime character support. Embedded portable arrays
are decoded using the same CFN2 reader. Legacy generated bitmap coverage uses the
character ranges in its font declarations. This does not launch the application.
Files are read from the working tree so regeneration checks newly generated assets.

Source coverage and packaged coverage are separate: a complete outline does not
mean its selected glyphs have been included in a generated asset. In particular,
ASCII-only bitmap assets omit all 44 non-ASCII base extensions.
Tamzen's selected UI asset includes every supported drawable comparison character;
the other comparison samples retain their original ASCII-only purpose.

Inspected working tree based on revision: `1e271a595cfe9d4b7553a884c13d5e30e90ef79d`.

## Outline sources

| Font / asset | Base covered | Of requested 373 | Missing set |
| --- | ---: | ---: | --- |
| ArimoBold: `cntrl/fonts/arimo/Arimo-Bold.ttf` | 139/139 | 373/373 | None |
| Arimo: `cntrl/fonts/arimo/Arimo-Regular.ttf` | 139/139 | 373/373 | None |
| AtkinsonNextSemiBold: `cntrl/fonts/atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-SemiBold.ttf` | 137/139 | 337/373 | M1 |
| BarlowCondensedSemiBold: `cntrl/fonts/barlow-condensed/BarlowCondensed-SemiBold.ttf` | 139/139 | 360/373 | M2 |
| CaveatBold: `cntrl/fonts/caveat/Caveat-Bold.ttf` | 139/139 | 348/373 | M3 |
| CourierPrimeBold: `cntrl/fonts/courier-prime/CourierPrime-Bold.ttf` | 139/139 | 345/373 | M4 |
| CourierPrime: `cntrl/fonts/courier-prime/CourierPrime-Regular.ttf` | 139/139 | 345/373 | M4 |
| DMSerifDisplay: `cntrl/fonts/dm-serif-display/DMSerifDisplay-Regular.ttf` | 139/139 | 315/373 | M5 |
| OrnamentalSlab: `cntrl/fonts/ewert/Ewert-Regular.ttf` | 133/139 | 214/373 | M6 |
| CollegeBlock: `cntrl/fonts/graduate/Graduate-Regular.ttf` | 139/139 | 346/373 | M7 |
| MetalLord: `cntrl/fonts/metal-lord/MetalLord.otf` | 139/139 | 347/373 | M8 |
| CircuitBold: `cntrl/fonts/orbitron/Orbitron-Bold.ttf` | 134/139 | 186/373 | M9 |
| Circuit: `cntrl/fonts/orbitron/Orbitron-Medium.ttf` | 134/139 | 186/373 | M9 |
| DecoRound: `cntrl/fonts/righteous/Righteous-Regular.ttf` | 139/139 | 333/373 | M10 |
| RubikDistressed: `cntrl/fonts/rubik-distressed/RubikDistressed-Regular.ttf` | 139/139 | 345/373 | M11 |
| SpecialElite: `cntrl/fonts/special-elite/SpecialElite-Regular.ttf` | 139/139 | 331/373 | M12 |
| StillTime: `cntrl/fonts/still-time/StillTime.otf` | 133/139 | 304/373 | M13 |

## Packaged bitmap assets

| Font / asset | Base covered | Of requested 373 | Missing set |
| --- | ---: | ---: | --- |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-10-b.cfont` (size 10; 3261 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-10-r.cfont` (size 10; 3261 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-11-b.cfont` (size 11; 3356 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-11-r.cfont` (size 11; 3356 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-12-b.cfont` (size 12; 3451 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-12-r.cfont` (size 12; 3451 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-13-b.cfont` (size 13; 3546 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Cherry-cherry-13-r.cfont` (size 13; 3546 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Spleen-spleen-5x8.cfont` (size 8; 3071 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Spleen-spleen-6x12.cfont` (size 12; 3451 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Spleen-spleen-8x16.cfont` (size 16; 3831 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen5x9b.cfont` (size 9; 3166 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen5x9r.cfont` (size 9; 3166 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen6x12b.cfont` (size 12; 3451 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen6x12r.cfont` (size 12; 3451 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen7x13b.cfont` (size 13; 3546 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen7x13r.cfont` (size 13; 3546 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen7x14b.cfont` (size 14; 3641 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen7x14r.cfont` (size 14; 3641 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen8x15b.cfont` (size 15; 3736 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen8x15r.cfont` (size 15; 3736 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen8x16b.cfont` (size 16; 3847 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/Tamzen-Tamzen8x16r.cfont` (size 16; 3847 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/TamzenBold7x13.cfont` (size 13; 5723 bytes) | 139/139 | 182/373 | M15 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-11.cfont` (size 11; 3356 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-11b.cfont` (size 11; 3356 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-12.cfont` (size 12; 3451 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-12b.cfont` (size 12; 3451 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-13.cfont` (size 13; 3546 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-13b.cfont` (size 13; 3546 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-14.cfont` (size 14; 3641 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-14b.cfont` (size 14; 3641 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-15.cfont` (size 15; 3736 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-15b.cfont` (size 15; 3736 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-16.cfont` (size 16; 3831 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/fonts/bitmap_samples/UW-ttyp0-16b.cfont` (size 16; 3831 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/distribution/fonts/Arimo.cfont` (30 sizes: 8–96; 888506 bytes) | 139/139 | 372/373 | M16 |
| `cntrl/maker/morningstar/mc/distribution/fonts/ArimoBold.cfont` (size 22; 19641 bytes) | 139/139 | 372/373 | M16 |
| `cntrl/maker/morningstar/mc/distribution/fonts/AtkinsonNextSemiBold.cfont` (30 sizes: 8–96; 787656 bytes) | 137/139 | 337/373 | M1 |
| `cntrl/maker/morningstar/mc/distribution/fonts/BarlowCondensedSemiBold.cfont` (size 23; 17205 bytes) | 139/139 | 359/373 | M17 |
| `cntrl/maker/morningstar/mc/distribution/fonts/CaveatBold.cfont` (size 21; 16632 bytes) | 139/139 | 347/373 | M18 |
| `cntrl/maker/morningstar/mc/distribution/fonts/Circuit.cfont` (size 23; 10005 bytes) | 134/139 | 186/373 | M9 |
| `cntrl/maker/morningstar/mc/distribution/fonts/CircuitBold.cfont` (size 23; 10609 bytes) | 134/139 | 186/373 | M9 |
| `cntrl/maker/morningstar/mc/distribution/fonts/CollegeBlock.cfont` (size 22; 18826 bytes) | 139/139 | 346/373 | M7 |
| `cntrl/maker/morningstar/mc/distribution/fonts/ComicSansMS.cfont` (size 16; 4880 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/distribution/fonts/CourierPrime.cfont` (8 sizes: 12–48; 177500 bytes) | 139/139 | 344/373 | M19 |
| `cntrl/maker/morningstar/mc/distribution/fonts/CourierPrimeBold.cfont` (size 24; 18243 bytes) | 139/139 | 344/373 | M19 |
| `cntrl/maker/morningstar/mc/distribution/fonts/DMSerifDisplay.cfont` (size 24; 17113 bytes) | 139/139 | 314/373 | M20 |
| `cntrl/maker/morningstar/mc/distribution/fonts/DecoRound.cfont` (size 22; 17256 bytes) | 139/139 | 332/373 | M21 |
| `cntrl/maker/morningstar/mc/distribution/fonts/MetalLord.cfont` (size 22; 20238 bytes) | 139/139 | 346/373 | M22 |
| `cntrl/maker/morningstar/mc/distribution/fonts/OrnamentalSlab.cfont` (size 21; 11741 bytes) | 133/139 | 214/373 | M6 |
| `cntrl/maker/morningstar/mc/distribution/fonts/RubikDistressed.cfont` (size 22; 19472 bytes) | 139/139 | 344/373 | M23 |
| `cntrl/maker/morningstar/mc/distribution/fonts/SpecialElite.cfont` (size 21; 16613 bytes) | 139/139 | 330/373 | M24 |
| `cntrl/maker/morningstar/mc/distribution/fonts/StillTime.cfont` (size 21; 19980 bytes) | 133/139 | 304/373 | M13 |
| `cntrl/maker/morningstar/mc/distribution/fonts/TamzenBold7x13.cfont` (size 13; 5723 bytes) | 139/139 | 182/373 | M15 |
| `cntrl/maker/morningstar/mc/font_sources/Arimo.cfont` (30 sizes: 8–96; 888506 bytes) | 139/139 | 372/373 | M16 |
| `cntrl/maker/morningstar/mc/font_sources/ArimoBold.cfont` (size 22; 19641 bytes) | 139/139 | 372/373 | M16 |
| `cntrl/maker/morningstar/mc/font_sources/AtkinsonNextSemiBold.cfont` (30 sizes: 8–96; 787656 bytes) | 137/139 | 337/373 | M1 |
| `cntrl/maker/morningstar/mc/font_sources/BarlowCondensedSemiBold.cfont` (size 23; 17205 bytes) | 139/139 | 359/373 | M17 |
| `cntrl/maker/morningstar/mc/font_sources/CaveatBold.cfont` (size 21; 16632 bytes) | 139/139 | 347/373 | M18 |
| `cntrl/maker/morningstar/mc/font_sources/Circuit.cfont` (size 23; 10005 bytes) | 134/139 | 186/373 | M9 |
| `cntrl/maker/morningstar/mc/font_sources/CircuitBold.cfont` (size 23; 10609 bytes) | 134/139 | 186/373 | M9 |
| `cntrl/maker/morningstar/mc/font_sources/CollegeBlock.cfont` (size 22; 18826 bytes) | 139/139 | 346/373 | M7 |
| `cntrl/maker/morningstar/mc/font_sources/ComicSansMS.cfont` (size 16; 4880 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/font_sources/CourierPrime.cfont` (8 sizes: 12–48; 177500 bytes) | 139/139 | 344/373 | M19 |
| `cntrl/maker/morningstar/mc/font_sources/CourierPrimeBold.cfont` (size 24; 18243 bytes) | 139/139 | 344/373 | M19 |
| `cntrl/maker/morningstar/mc/font_sources/DMSerifDisplay.cfont` (size 24; 17113 bytes) | 139/139 | 314/373 | M20 |
| `cntrl/maker/morningstar/mc/font_sources/DecoRound.cfont` (size 22; 17256 bytes) | 139/139 | 332/373 | M21 |
| `cntrl/maker/morningstar/mc/font_sources/MetalLord.cfont` (size 22; 20238 bytes) | 139/139 | 346/373 | M22 |
| `cntrl/maker/morningstar/mc/font_sources/OrnamentalSlab.cfont` (size 21; 11741 bytes) | 133/139 | 214/373 | M6 |
| `cntrl/maker/morningstar/mc/font_sources/RubikDistressed.cfont` (size 22; 19472 bytes) | 139/139 | 344/373 | M23 |
| `cntrl/maker/morningstar/mc/font_sources/SpecialElite.cfont` (size 21; 16613 bytes) | 139/139 | 330/373 | M24 |
| `cntrl/maker/morningstar/mc/font_sources/StillTime.cfont` (size 21; 19980 bytes) | 133/139 | 304/373 | M13 |
| `cntrl/maker/morningstar/mc/font_sources/TamzenBold7x13.cfont` (size 13; 5723 bytes) | 139/139 | 182/373 | M15 |

## Language-pack fonts

| Font / asset | Base covered | Of requested 373 | Missing set |
| --- | ---: | ---: | --- |

The supplied packs contain translations only and reuse the flash fonts.

## Embedded portable UI fonts

| Font / asset | Base covered | Of requested 373 | Missing set |
| --- | ---: | ---: | --- |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: ComicSansMS` (size 16; 4880 bytes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: Circuit` (size 23; 10005 bytes) | 134/139 | 186/373 | M9 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: CircuitBold` (size 23; 10609 bytes) | 134/139 | 186/373 | M9 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: StillTime` (size 21; 19980 bytes) | 133/139 | 304/373 | M13 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: MetalLord` (size 22; 20238 bytes) | 139/139 | 346/373 | M22 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: CaveatBold` (size 21; 16632 bytes) | 139/139 | 347/373 | M18 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: CourierPrimeBold` (size 24; 18243 bytes) | 139/139 | 344/373 | M19 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: DecoRound` (size 22; 17256 bytes) | 139/139 | 332/373 | M21 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: DMSerifDisplay` (size 24; 17113 bytes) | 139/139 | 314/373 | M20 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: CollegeBlock` (size 22; 18826 bytes) | 139/139 | 346/373 | M7 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: OrnamentalSlab` (size 21; 11741 bytes) | 133/139 | 214/373 | M6 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: RubikDistressed` (size 22; 19472 bytes) | 139/139 | 344/373 | M23 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: SpecialElite` (size 21; 16613 bytes) | 139/139 | 330/373 | M24 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: ArimoBold` (size 22; 19641 bytes) | 139/139 | 372/373 | M16 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: BarlowCondensedSemiBold` (size 23; 17205 bytes) | 139/139 | 359/373 | M17 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: CourierPrime` (8 sizes: 12–48; 177500 bytes) | 139/139 | 344/373 | M19 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: AtkinsonNextSemiBold` (30 sizes: 8–96; 787656 bytes) | 137/139 | 337/373 | M1 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: Arimo` (30 sizes: 8–96; 888506 bytes) | 139/139 | 372/373 | M16 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/embedded_fonts.h: TamzenBold7x13` (size 13; 5723 bytes) | 139/139 | 182/373 | M15 |

## Legacy generated bitmap fonts

| Font / asset | Base covered | Of requested 373 | Missing set |
| --- | ---: | ---: | --- |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_AtkinsonNextSemiBold.cpp` (30 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CaveatBold.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_Circuit.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CircuitBold.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CollegeBlock.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CourierPrime.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_CourierPrimeBold.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_DMSerifDisplay.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_DecoRound.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_MetalLord.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_OrnamentalSlab.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_RubikDistressed.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_SpecialElite.cpp` (8 sizes) | 95/139 | 95/373 | M14 |
| `cntrl/maker/morningstar/mc/firmware/patch_switcher/font_StillTime.cpp` (8 sizes) | 95/139 | 95/373 | M14 |

## Exact missing characters

Characters are ordered by Unicode code point. Each set applies to every row referencing it.

### M1: 36 missing

`­` `µ` `Ŋ` `ŋ` `Ō` `ō` `Ŗ` `ŗ` `Ŧ` `ŧ` `Ə` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `₺` `₽` `Ω` `∆` `ﬁ` `ﬂ`

### M2: 13 missing

`Ʒ` `Ǟ` `ǟ` `Ǥ` `Ǧ` `Ǩ` `Ǭ` `ǭ` `Ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ`

### M3: 25 missing

`Ʒ` `Ǟ` `ǟ` `Ǥ` `Ǧ` `Ǩ` `Ǭ` `ǭ` `Ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `π` `ℓ` `Ω` `℮` `∂` `∆` `∏` `∑` `√` `∞` `∫` `◊`

### M4: 28 missing

`Ə` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `₺` `₽` `ℓ` `Ω` `℮` `∆` `∙`

### M5: 58 missing

`Ċ` `ċ` `Ġ` `ġ` `Ħ` `ħ` `Ŋ` `ŋ` `Ŧ` `ŧ` `Ŵ` `ŵ` `Ŷ` `ŷ` `Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `π` `Ẁ` `ẁ` `Ẃ` `ẃ` `Ẅ` `ẅ` `Ỳ` `ỳ` `₺` `ℓ` `Ω` `∂` `∆` `∏` `∑` `√` `∞` `∫` `◊` `ﬁ` `ﬂ`

### M6: 159 missing

` ` `­` `µ` `Ā` `ā` `Ă` `ă` `Ą` `ą` `Ć` `ć` `Ċ` `ċ` `Ď` `ď` `Đ` `đ` `Ē` `ē` `Ė` `ė` `Ę` `ę` `Ě` `ě` `Ğ` `ğ` `Ġ` `ġ` `Ģ` `ģ` `Ħ` `ħ` `Ī` `ī` `Į` `į` `İ` `Ķ` `ķ` `Ĺ` `ĺ` `Ļ` `ļ` `Ľ` `ľ` `Ł` `ł` `Ń` `ń` `Ņ` `ņ` `Ň` `ň` `Ŋ` `ŋ` `Ō` `ō` `Ő` `ő` `Œ` `œ` `Ŕ` `ŕ` `Ŗ` `ŗ` `Ř` `ř` `Ś` `ś` `Ş` `ş` `Š` `š` `Ţ` `ţ` `Ť` `ť` `Ŧ` `ŧ` `Ū` `ū` `Ů` `ů` `Ű` `ű` `Ų` `ų` `Ŵ` `ŵ` `Ŷ` `ŷ` `Ź` `ź` `Ż` `ż` `Ž` `ž` `Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ș` `ș` `Ț` `ț` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `π` `Ẁ` `ẁ` `Ẃ` `ẃ` `Ẅ` `ẅ` `Ỳ` `ỳ` `‚` `†` `‡` `‰` `€` `₺` `₽` `ℓ` `™` `Ω` `℮` `∂` `∆` `∏` `∑` `∙` `√` `∞` `∫` `≈` `≠` `≤` `≥` `◊` `ﬁ` `ﬂ`

### M7: 27 missing

`­` `Ə` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `₺` `₽` `ℓ` `∕` `∙`

### M8: 26 missing

`µ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ʒ` `ˉ` `ℓ` `Ω` `℮` `∆` `∕` `∙` `ﬁ` `ﬂ`

### M9: 187 missing

`^` `¤` `¥` `¦` `§` `©` `ª` `«` `¬` `­` `®` `¯` `±` `²` `³` `µ` `·` `¹` `º` `»` `¼` `½` `¾` `Ð` `Ø` `Þ` `ð` `ø` `þ` `Ā` `ā` `Ă` `ă` `Ą` `ą` `Ć` `ć` `Ċ` `ċ` `Č` `č` `Ď` `ď` `Đ` `đ` `Ē` `ē` `Ė` `ė` `Ę` `ę` `Ě` `ě` `Ğ` `ğ` `Ġ` `ġ` `Ģ` `ģ` `Ħ` `ħ` `Ī` `ī` `Į` `į` `İ` `Ķ` `ķ` `Ĺ` `ĺ` `Ļ` `ļ` `Ľ` `ľ` `Ł` `ł` `Ń` `ń` `Ņ` `ņ` `Ň` `ň` `Ŋ` `ŋ` `Ō` `ō` `Ő` `ő` `Ŕ` `ŕ` `Ŗ` `ŗ` `Ř` `ř` `Ś` `ś` `Ş` `ş` `Ţ` `ţ` `Ť` `ť` `Ŧ` `ŧ` `Ū` `ū` `Ů` `ů` `Ű` `ű` `Ų` `ų` `Ŵ` `ŵ` `Ŷ` `ŷ` `Ź` `ź` `Ż` `ż` `Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ș` `ș` `Ț` `ț` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `˛` `˝` `π` `Ẁ` `ẁ` `Ẃ` `ẃ` `Ẅ` `ẅ` `Ỳ` `ỳ` `‚` `„` `†` `‡` `‰` `‹` `›` `⁄` `₺` `₽` `ℓ` `™` `Ω` `℮` `∂` `∆` `∏` `∑` `∕` `∙` `√` `∞` `∫` `≈` `≠` `≤` `≥` `◊` `ﬁ` `ﬂ`

### M10: 40 missing

`Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ș` `ș` `Ț` `ț` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `₺` `₽` `ℓ` `Ω` `℮` `∆` `∑` `∕` `∙` `√` `∫` `≤` `≥` `◊`

### M11: 28 missing

`Ţ` `ţ` `Ə` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `π` `₺` `ℓ` `Ω` `∙`

### M12: 42 missing

`Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ș` `ș` `Ț` `ț` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `π` `₺` `₽` `ℓ` `Ω` `℮` `∆` `∑` `∕` `∙` `√` `∞` `∫` `≤` `≥` `◊`

### M13: 69 missing

`[` `]` `{` `|` `}` `~` ` ` `¤` `¦` `§` `¬` `­` `°` `±` `²` `³` `µ` `¶` `¹` `¼` `½` `¾` `×` `÷` `Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `π` `†` `‡` `‰` `₺` `₽` `ℓ` `Ω` `℮` `∂` `∆` `∏` `∑` `∕` `∙` `√` `∞` `∫` `≈` `≠` `≤` `≥` `◊`

### M14: 278 missing

` ` `¡` `¢` `£` `¤` `¥` `¦` `§` `¨` `©` `ª` `«` `¬` `­` `®` `¯` `°` `±` `²` `³` `´` `µ` `¶` `·` `¸` `¹` `º` `»` `¼` `½` `¾` `¿` `À` `Á` `Â` `Ã` `Ä` `Å` `Æ` `Ç` `È` `É` `Ê` `Ë` `Ì` `Í` `Î` `Ï` `Ð` `Ñ` `Ò` `Ó` `Ô` `Õ` `Ö` `×` `Ø` `Ù` `Ú` `Û` `Ü` `Ý` `Þ` `ß` `à` `á` `â` `ã` `ä` `å` `æ` `ç` `è` `é` `ê` `ë` `ì` `í` `î` `ï` `ð` `ñ` `ò` `ó` `ô` `õ` `ö` `÷` `ø` `ù` `ú` `û` `ü` `ý` `þ` `ÿ` `Ā` `ā` `Ă` `ă` `Ą` `ą` `Ć` `ć` `Ċ` `ċ` `Č` `č` `Ď` `ď` `Đ` `đ` `Ē` `ē` `Ė` `ė` `Ę` `ę` `Ě` `ě` `Ğ` `ğ` `Ġ` `ġ` `Ģ` `ģ` `Ħ` `ħ` `Ī` `ī` `Į` `į` `İ` `ı` `Ķ` `ķ` `Ĺ` `ĺ` `Ļ` `ļ` `Ľ` `ľ` `Ł` `ł` `Ń` `ń` `Ņ` `ņ` `Ň` `ň` `Ŋ` `ŋ` `Ō` `ō` `Ő` `ő` `Œ` `œ` `Ŕ` `ŕ` `Ŗ` `ŗ` `Ř` `ř` `Ś` `ś` `Ş` `ş` `Š` `š` `Ţ` `ţ` `Ť` `ť` `Ŧ` `ŧ` `Ū` `ū` `Ů` `ů` `Ű` `ű` `Ų` `ų` `Ŵ` `ŵ` `Ŷ` `ŷ` `Ÿ` `Ź` `ź` `Ż` `ż` `Ž` `ž` `Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ș` `ș` `Ț` `ț` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˆ` `ˇ` `ˉ` `˘` `˙` `˚` `˛` `˜` `˝` `π` `Ẁ` `ẁ` `Ẃ` `ẃ` `Ẅ` `ẅ` `Ỳ` `ỳ` `–` `—` `‘` `’` `‚` `“` `”` `„` `†` `‡` `•` `…` `‰` `‹` `›` `⁄` `€` `₺` `₽` `ℓ` `™` `Ω` `℮` `∂` `∆` `∏` `∑` `−` `∕` `∙` `√` `∞` `∫` `≈` `≠` `≤` `≥` `◊` `ﬁ` `ﬂ`

### M15: 191 missing

`§` `ª` `¬` `­` `®` `¯` `±` `²` `³` `µ` `¶` `·` `¹` `º` `¼` `½` `¾` `Ă` `ă` `Ą` `ą` `Ć` `ć` `Ċ` `ċ` `Č` `č` `Ď` `ď` `Đ` `đ` `Ē` `ē` `Ė` `ė` `Ę` `ę` `Ě` `ě` `Ğ` `ğ` `Ġ` `ġ` `Ģ` `ģ` `Ħ` `ħ` `Ī` `ī` `Į` `į` `İ` `ı` `Ķ` `ķ` `Ĺ` `ĺ` `Ļ` `ļ` `Ľ` `ľ` `Ł` `ł` `Ń` `ń` `Ņ` `ņ` `Ň` `ň` `Ŋ` `ŋ` `Ő` `ő` `Ŕ` `ŕ` `Ŗ` `ŗ` `Ř` `ř` `Ś` `ś` `Ş` `ş` `Š` `š` `Ţ` `ţ` `Ť` `ť` `Ŧ` `ŧ` `Ū` `ū` `Ů` `ů` `Ű` `ű` `Ų` `ų` `Ŵ` `ŵ` `Ŷ` `ŷ` `Ź` `ź` `Ż` `ż` `Ž` `ž` `Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ș` `ș` `Ț` `ț` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˆ` `ˇ` `ˉ` `˘` `˙` `˚` `˛` `˜` `˝` `π` `Ẁ` `ẁ` `Ẃ` `ẃ` `Ẅ` `ẅ` `Ỳ` `ỳ` `–` `—` `‘` `‚` `“` `”` `„` `†` `‡` `•` `…` `‰` `‹` `›` `⁄` `€` `₺` `₽` `ℓ` `™` `Ω` `℮` `∂` `∆` `∏` `∑` `−` `∕` `∙` `√` `∞` `∫` `≈` `≠` `≤` `≥` `◊` `ﬁ` `ﬂ`

### M16: 1 missing

`­`

### M17: 14 missing

`­` `Ʒ` `Ǟ` `ǟ` `Ǥ` `Ǧ` `Ǩ` `Ǭ` `ǭ` `Ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ`

### M18: 26 missing

`­` `Ʒ` `Ǟ` `ǟ` `Ǥ` `Ǧ` `Ǩ` `Ǭ` `ǭ` `Ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `π` `ℓ` `Ω` `℮` `∂` `∆` `∏` `∑` `√` `∞` `∫` `◊`

### M19: 29 missing

`­` `Ə` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `₺` `₽` `ℓ` `Ω` `℮` `∆` `∙`

### M20: 59 missing

`­` `Ċ` `ċ` `Ġ` `ġ` `Ħ` `ħ` `Ŋ` `ŋ` `Ŧ` `ŧ` `Ŵ` `ŵ` `Ŷ` `ŷ` `Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `π` `Ẁ` `ẁ` `Ẃ` `ẃ` `Ẅ` `ẅ` `Ỳ` `ỳ` `₺` `ℓ` `Ω` `∂` `∆` `∏` `∑` `√` `∞` `∫` `◊` `ﬁ` `ﬂ`

### M21: 41 missing

`­` `Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ș` `ș` `Ț` `ț` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `₺` `₽` `ℓ` `Ω` `℮` `∆` `∑` `∕` `∙` `√` `∫` `≤` `≥` `◊`

### M22: 27 missing

`­` `µ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ʒ` `ˉ` `ℓ` `Ω` `℮` `∆` `∕` `∙` `ﬁ` `ﬂ`

### M23: 29 missing

`­` `Ţ` `ţ` `Ə` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `π` `₺` `ℓ` `Ω` `∙`

### M24: 43 missing

`­` `Ə` `ƒ` `Ʒ` `Ǟ` `ǟ` `Ǥ` `ǥ` `Ǧ` `ǧ` `Ǩ` `ǩ` `Ǭ` `ǭ` `Ǯ` `ǯ` `Ș` `ș` `Ț` `ț` `Ȯ` `ȯ` `Ȱ` `ȱ` `ə` `ʒ` `ˉ` `π` `₺` `₽` `ℓ` `Ω` `℮` `∆` `∑` `∕` `∙` `√` `∞` `∫` `≤` `≥` `◊`

## Additional work beyond the prompt

None. Missing source glyphs have not been newly drawn. The inspection script reproduces the coverage tables.
