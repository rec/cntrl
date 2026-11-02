# Additional font sources

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
