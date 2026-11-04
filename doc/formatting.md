# Display formatting in TOML

## Scope

Formatting uses TOML configuration, object-attached partial styles, page fields,
tag membership and host-side compilation.

Authors choose colors and font names. Text always fits automatically. No CSS
parser, selector language or additional dependency is required.

## Styles

Use the same optional `style` structure on shows, windows, pages, tags,
programs, patches and shared parameters. Keep page-specific field overrides in
`pages.PAGE.line_styles.FIELD`.

| Property | Meaning |
| --- | --- |
| `foreground_color` | Text color |
| `background_color` | Color behind the line |
| `font_name` | Exact installed bitmap font family |
| `horizontal_scale` | Positive width multiplier; overrides the font default |
| `icon` | Image-file reference; empty string suppresses an inherited icon |

All five properties are optional. Omission leaves the lower-precedence value
unchanged. Merge individual properties, not whole style objects. Apply centrally
defined application defaults only where no source supplies a value.

Retain the existing color syntax and target font validation. A name such as
`CourierPrimeBold` identifies a family; Bold is documentation within the name.
There are no weight, italic, synthesis or font fallback controls. Horizontal
scaling changes glyph widths and spacing before automatic fitting, without
changing the vertical scale. StillTime defaults to 1.5; all other fonts default
to 1.0. Explicit style values override these font defaults.

Use show-level `style` for authored appearance defaults. Font names belong in
styles; there is no separate show-level font setting.

Illustrative configuration:

```toml
[style]
foreground_color = 'black'
background_color = 'white'
font_name = 'Arial'

[tags.sax.style]
foreground_color = '0xD08000'

[tags.whole-system.style]
font_name = 'CourierPrimeBold'

[pages.patch]
fields = ['patch_cycle.name', 'display_name']

[pages.patch.line_styles.'patch_cycle.name']
foreground_color = 'blue'

[windows.next]
page = 'patch'

[windows.next.style]
background_color = 'white'
```

These colors illustrate configuration, not a new default pedal theme. Existing
program and patch definitions can continue to include their own `style` blocks.

## Resolution order

Resolve each property from lowest to highest precedence:

1. Application defaults.
2. Show style.
3. Window style.
4. Page style.
5. Applicable tag styles, in the order described below.
6. Program style, for patch-content fields.
7. Patch style, for patch-content fields.
8. Shared parameter style, for that parameter's field.
9. Page-specific field style.

Only consult sources belonging to the displayed field. The `tags` field
uses tag styles but not program or patch styles. Navigation headings such
as `patch_cycle.name` use show, window, page and field styles. Global fields do not
acquire the current patch's tags or appearance.

The same page resolves against each window's own data item. Background color
follows the same partial-override rules as foreground color and font name.
There is no separate transparency model or CSS inheritance behavior.

## Tag membership and style precedence

Programs and patches retain independently authored tag lists. Tag
membership does not change MIDI behavior or navigation patch cycles.

Apply the program's tags in listed order, followed by the patch's own
tags in listed order. Within these tag layers, a later explicit
property wins. Registry declaration order has no effect. Tags have no integer
`priority`.

For example, program tags `['sax', 'bright']` apply sax then bright.
Patch tags `['whole-system']` apply afterward. A tag that only changes
the font leaves the earlier foreground color intact. Explicit program and patch
styles still take precedence over all tag styles, as listed above.

A tag repeated in the patch's own list applies again at that later position;
otherwise deduplication would prevent the local ordering from taking effect.
The displayed tag names remain unique, in their existing first-occurrence
order. Distinguish that membership list from the ordered style application.

Preserve the explicit lists separately through download/recompile. Do not store
a flattened inherited list as the patch's own tags. Tags do not require style
declarations; an undeclared tag contributes no style properties.

## Automatic text fitting and painting

Each nonempty field independently chooses the largest available bitmap face
that fits the drawable window width and its allocated vertical space. Preserve
glyph proportions. Short labels remain bounded by height so they cannot overlap
other lines; unused horizontal space is acceptable.

There are no authored `font_size` or `autoscale_fonts` controls. Selected raster
sizes and metrics are compiler details. The patch-manager card divides the window
into three equal vertical bands. Its first band is reversed, using the heading
color as background and the page background as text color. The second and third
lines share the smaller fitting size, while retaining their font families.
Other pages, including switch pages, retain their own layout rules.

Window padding, horizontal centering, field order and missing-value omission
remain part of the page model. Transitions end with the destination appearance.

Already-sent text keeps its resolved foreground and uses alternating transparent
pixels to expose the line background. Stippling remains application behavior;
there is no extra style color or `half` setting.

## Optional style icons

Every style may include an optional `icon` image-file reference, for example
`icon = 'images/sax.png'`. It follows the same property override order as colors
and font names. Omission inherits; `icon = ''` explicitly suppresses an inherited
icon. Programs, patches and tags have no separate direct icon property.
Preserve partial styles, including empty icon strings, through download/recompile.

Image loading, path resolution, conversion, embedding, layout and rendering remain
future work. Resolving the reference does not display an icon yet.

## Pages and fields

Pages contain ordered field names, for example
`fields = ['patch_cycle.name', 'names.vl70m', 'names.patchman']`. Each window supplies
its own data item to the page. Missing and empty values are omitted; zero and
false remain visible. Built-ins and shared parameters use one unique namespace.
Unknown or duplicate field names are errors. Pages are bounded by record capacity
and target height. See the [application guide](../firmware/README.md) for the
complete field catalogue and compiled format, and [Patch authoring](patches.md)
for styles generated automatically for cycles.
