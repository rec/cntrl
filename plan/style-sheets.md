# CSS style sheets for displays

Status: proposed, not implemented. This replaces the style-resolution proposal
in [display-format.md](display-format.md), which records the current system.

## Scope

Replace the fixed style merge order and category priorities with a small CSS
subset. Supported syntax and semantics must agree with CSS. Keep the current
pages, fields, category membership, bitmap renderer and host-side compilation.

Authors choose text color, background color and font family. Text always fits
automatically. There are no authored font sizes, sizing modes or fitting switches.

## Authoring

Use one show-level `stylesheet` string. Keep the CSS source in downloaded setups,
including comments and rule order. No external sheets, imports or resources.

```toml
stylesheet = '''
show {
    color: black;
    background-color: white;
    font-family: "Arial";
}

.sax { color: #d08000; }
.whole-system { font-family: "CourierPrimeBold"; }

window[name="next"] field[name="display_name"].sax {
    color: #008080;
}
'''

[pages.patch]
fields = ['group.name', 'display_name']

[windows.next]
page = 'patch'
```

The example illustrates syntax; it does not prescribe a new pedal color scheme.

## Display tree

Each window contains its own occurrence of a page and that page's fields:

```text
show
  window [name="next"]
    page [name="patch"]
      field [name="group.name"]
      field [name="display_name"]
```

The undo and sent windows have equivalent branches. These are application
elements, not HTML. The existing layout supplies their boxes. A reused page
inherits from the window displaying that occurrence.

Programs and patches are musical data, not CSS ancestors. Their effective
categories become classes on patch-content fields and the `categories` field.
Do not attach these classes to pages, windows, navigation headings or global
parameter fields. Explicit program and patch category assignments remain
separate; the effective union is derived for matching.

Use categories to style programs and patches, including a category assigned to
just one object when necessary. Do not add program/bank selectors, positional
patch selectors, unique IDs or a second system of styling labels. Reordering
patches must not change which rules match them. Global parameters are selected
by field name.

## Selectors

Support only:

- Element names: `show`, `window`, `page`, `field`.
- Category classes: `.sax`.
- Exact `name` attributes: `[name="display_name"]`.
- Compounds: `field.sax.whole-system`.
- Descendants: `window[name="next"] field.sax`.
- Comma-separated lists for sharing declarations.

Use CSS identifier escaping for category names that need it; do not normalize
or rename categories. The `name` attribute identifies windows, pages and fields
using their existing configuration names.

No universal or ID selectors, other attribute operators, child or sibling
combinators, pseudo-classes, pseudo-elements or functional selectors.

## Properties

| Property | Supported values | Inherits |
| --- | --- | --- |
| `color` | CSS named colors, `#rgb`, `#rrggbb` | Yes |
| `background-color` | The same colors, plus `transparent` | No |
| `font-family` | One quoted installed family name | Yes |

Also accept `inherit` to request the parent's computed value explicitly.
Other authored values and all other properties are outside this subset.

`"CourierPrimeBold"` names an installed family; Bold is simply part of that name.
No weight, italic, synthesis, family fallback lists or font shorthand. Reject
unknown families against the target's font catalogue. No CSS lengths or units
are needed. Convert resolved colors to RGB565 only when compiling target data.

## Cascade

Follow [CSS cascade and inheritance](https://www.w3.org/TR/css-cascade/):
application defaults form a user-agent sheet, and the configured sheet is the
author sheet. Support ordinary declarations only, without `!important`, layers,
variables or at-rules.

Resolve each property by origin, specificity, then source order. Class and
attribute selectors have equal specificity; element selectors have lower
specificity. Later declarations win ties. Category assignment order never
contributes precedence. Remove category `priority` and tied-priority errors.

Inheritance follows the display tree after the cascade. A declaration directly
on a field wins over an inherited value regardless of the ancestor selector's
specificity. Missing inherited properties take their parent's computed value;
other missing properties take their CSS initial value. Define application font
and color defaults centrally.

Backgrounds are initially transparent and do not inherit. A transparent field
reveals its page, window and show backgrounds. `background-color: inherit`
copies the parent's computed value instead. Resolve these layers to the painted
background before emitting firmware records.

## Text fitting and painting

Always fit each nonempty field's text independently to the available window
width after padding, preserving glyph proportions. Choose the largest available
bitmap face that fits that width and the field's allocated vertical space.
Height remains a necessary bound so short labels cannot overlap other lines;
do not stretch letters to fill unused width. Retain the existing equal height
allocation for fitting and distribute remaining vertical space between rows,
with half a gap at the top and bottom.

Fitting is application layout behavior, not CSS. Remove authored `font_size`
and `autoscale_fonts` throughout the replaced configuration and metadata. Do
not introduce replacement settings or a fixed-size mode. Internal bitmap metrics
and the selected raster face remain implementation details.

Retain field ordering, missing-value omission, centering, management-text
wrapping, window geometry and padding. Line backgrounds still paint their
existing bands. Preserve transitions and resolve the destination appearance.
The new always-fit rule deliberately replaces any previous fixed-size text;
other layout and behavior should remain unchanged.

Already-sent stippling remains an application effect after style resolution:
alternating transparent pixels expose the composed background while retaining
the foreground color. No CSS setting or extra color is needed for it.

Borders, rounded corners, images, opacity, animations and CSS layout are outside
scope. Do not add them merely because the graphics library can draw them.

## Implementation and verification

1. Implement host-side parsing, matching and resolution for this subset. Use
   proper CSS tokenization and escaping, not string splitting. Check available
   parser dependencies before deciding whether another is needed.
2. Replace the old style blocks, category priority resolver and authored sizing
   controls completely. Convert repository configurations and documentation;
   retain one authoring path, with no legacy decoder or automatic migration.
3. Compile resolved styles into the existing bounded display records. Preserve
   authored CSS and category assignments in source metadata; revise the compiled
   format contract as needed and require source recompilation.
4. Test specificity, source-order ties, inheritance, transparent backgrounds,
   category matching and export/recompile. Verify headings and global parameters
   do not accidentally acquire patch categories.
5. Test independent fitting with short and long labels, mixed families and
   multiple lines. Verify background bands, stippling and destination rendering.
   Preserve MIDI behavior. Check physical appearance when deployment is requested.

Report unsupported or invalid CSS with source positions and fail compilation
rather than silently omitting a requested effect. This is strict authoring
validation; do not reinterpret unsupported syntax or alter CSS matching and
cascade semantics to make it work.

The pedal receives resolved appearance, not CSS or a selector engine. This is a
replacement for style resolution and sizing configuration, not a browser engine
or a rewrite of the MIDI system.

## Additional work beyond the prompt

None. This document does not implement the proposal or change installed firmware.
