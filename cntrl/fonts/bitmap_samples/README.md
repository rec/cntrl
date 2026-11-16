# Status font comparison samples

These portable CFN2 assets preserve the upstream bitmap pixels and horizontal
advances at their native sizes. They contain printable ASCII only. No outlines
are rasterized, scaled, or automatically hinted for this comparison.

The asset collection contains 35 samples. The emulator comparison selects only
those whose visible capital `H` is seven pixels high, ignoring blank rows in
the bitmap. Nominal cell sizes below include padding and descender space and
can therefore be larger than seven pixels.

* UW ttyp0: 6x11, 6x12, 7x13, 7x14, 8x15 and 8x16, regular and bold.
* Cherry: 10, 11, 12 and 13 pixels high, regular and bold.
* Tamzen: 5x9, 6x12, 7x13, 7x14, 8x15 and 8x16, regular and bold.
* Spleen: 5x8, 6x12 and 8x16.

`R` and `B` in a sample label mean regular and bold. UW ttyp0 uses its upstream
default plain zero; the other families retain their own zero designs.

`TamzenBold7x13.cfont` is the selected utility font, generated from the same
pinned Tamzen Bold 7x13 BDF. It removes only empty bitmap rows, preserving each
glyph's baseline-relative pixels and character advance. The comparison samples
retain their original cell padding.

Regenerate from the repository root with:

```sh
uv run --no-sync python -m scripts.prepare_status_fonts
```

This maintainer command needs network access, Make and Perl. Normal emulator
use needs none of those font-generation tools and makes no font downloads.
The script downloads pinned upstream files and uses the UW release's own
Unicode/BDF generation rules, including its shallow-size variants.

Sources and original notices:

* [UW ttyp0 2.1](https://people.mpi-inf.mpg.de/~uwe/misc/uw-ttyp0/), archive
  SHA-256 `99506dd8794e1a5d5cd5811e6c1e55eeef989f8c3501fdb9265bda972aba1630`.
  See `UW-ttyp0-LICENSE.txt` for its permissive font licence.
* [Cherry](https://github.com/turquoise-hexagon/cherry), revision
  `34a94dc3db4ad1136bbcce9f8dff154ef5fa901a`, 0BSD. See `Cherry-LICENSE.txt`.
* [Tamzen](https://github.com/sunaku/tamzen-font), revision
  `3255e8259bc9b880c60ab8b737ec8aa574e00d75`, permissive font licence.
  See `Tamzen-LICENSE.txt` for both Tamzen and Tamsyn notices.
* [Spleen](https://github.com/fcambus/spleen), revision
  `c73273a07deb838a99ac473dee5f340ffa901d7d`, BSD-2-Clause.
  See `Spleen-LICENSE.txt`.
