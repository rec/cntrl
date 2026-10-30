# Display formatting in TOML

Status: implemented on main in compiled document format 14. Included in the
combined firmware deployment recorded in the hardware documentation. This
replaces the CSS proposal and updates
[display-format.md](display-format.md), which records the current implementation.

## Goal

Keep formatting in the existing TOML configuration. Simplify the current style
model and make its precedence explicit. Reuse object-attached partial styles,
page fields, tag membership and host-side compilation.

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
| `icon` | Image-file reference; empty string suppresses an inherited icon |

All four properties are optional. Omission leaves the lower-precedence value
unchanged. Merge individual properties, not whole style objects. Apply centrally
defined application defaults only where no source supplies a value.

Retain the existing color syntax and target font validation. A name such as
`CourierPrimeBold` identifies a family; Bold is documentation within the name.
There are no weight, italic, synthesis or font fallback controls.

Use show-level `style` for authored appearance defaults. Remove duplicate
show-level font controls as part of converting configurations, so each setting
has one authoring path.

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
property wins. Registry declaration order has no effect. Remove integer
`priority` and the errors for conflicting values at equal priority.

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

Remove authored `font_size` and `autoscale_fonts` throughout configuration and
source metadata. There is one fitting behavior, with no replacement switch or
fixed-size mode. Bitmap metrics and selected raster sizes remain internal.

Retain equal height allocation for fitting. After sizing and wrapping, subtract
the rendered rows' total height from the available height. For N rows, divide
the remainder by N to obtain H. Leave H/2 above and below the rows and H between
them, with consistent pixel rounding.

Retain window padding, horizontal centering, field order, missing-value omission
and management-text wrapping. Line backgrounds paint their existing bands.
Transitions finish with the destination window's resolved appearance. Existing
fixed-size configurations deliberately change to automatic fitting.

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

## Implementation and verification

1. Reduce the existing style model to the four properties above. Remove
   tag priorities and authored sizing controls, including obsolete export
   metadata and configuration paths.
2. Reuse the existing property merge logic with the documented tag order.
   Convert repository examples and update the current display documentation.
3. Make independent automatic fitting unconditional. Continue compiling resolved
   pages and styles on the host into bounded firmware records. Preserve partial
   styles, explicit tag lists and icon references in source metadata.
4. Update the compiled format contract as required and require source
   recompilation. Do not add a legacy decoder or automatic configuration migration.
5. Test partial overrides at each applicable layer, tag ordering and repeated
   inherited/local tags. Verify unrelated headings and global fields are
   unaffected. Test export/recompile without flattening authored information.
6. Test short and long labels, mixed families, multiple lines, background bands,
   stippling and transition destinations. Preserve MIDI/send/undo behavior.
   Verify physical appearance when deployment is requested.

No borders, rounded corners, image rendering, general style selectors or new
layout features are part of this work.

## Additional work beyond the prompt

None. Physical verification requires a separately requested deployment.
