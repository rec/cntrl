# Per-line display formatting

Already-sent patch names preserve their foreground color and use a checkerboard
with alternating transparent pixels. This appearance is verified on the pedal.

Status: implemented, 2026-10-27, compiled document format 12. Pages use an ordered
list of unique field names. The inheritance, missing-value and background rules
below are implemented. The default page represents the existing UI. The page UI
and stippled appearance are deployed and visually verified. The format-12 color
cleanup is implemented but has not been flashed.

## Goal

Allow every text line on a display to have its own foreground color,
background color and font. Formatting can come from the program change,
patch, patch category, page containing the line, or window displaying that
page. Authors should specify only the properties they want to override.

Keep content reusable: the same program or patch can appear on different pages
and in different windows without copying its definition just to change its
appearance.

## Terms and current foundation

- **Program change:** the instrument program definition associated with a MIDI
  program and optional bank. Its display style is configuration metadata, not
  part of the MIDI message.
- **Patch:** a show-local use of a program, including local names and overrides.
- **Patch category:** a named group of patches, with a displayed category label.
- **Page:** an ordered list of fields to display together.
- **Window:** the display region presenting a page, with dimensions, padding
  and default appearance. It supplies the page's current data item. A window
  may occupy an entire physical display.
- **Field:** a uniquely named value sourced from an attribute of the current
  data item or a global application parameter.
- **Line:** one displayed field value. A wrapped value produces multiple
  rendered rows.

The undo, sent and next windows use the same default `patch` page while supplying
their respective patches as its current data item. The default fields are
`['category.name', 'display_name']`. Omitting page/window configuration preserves
the existing category/name UI, typography, spacing, colors and send indication.
The management, empty-state and error screens use the same generic line renderer
with their existing fixed text, wrapping and readable defaults.

## Pages and field lookup

Specify a page with an ordered list of field names:

```toml
[pages.patch]
fields = ['category.name', 'display_name']

[windows.undo]
page = 'patch'

[windows.sent]
page = 'patch'

[windows.next]
page = 'patch'
```

Here, `category.name` and `display_name` read attributes of the patch supplied
by the window. The order in `fields` determines display order. A page can also
include a shared parameter such as `tempo` after defining its value:

```toml
[parameters.tempo]
value = 120
```

This provides a shared display value; it does not implement a tempo engine.
Parameters accept string, numeric or Boolean values, or an absent value.

All field names share one namespace. Built-in field definitions identify their
sources, so the page does not need an `attribute` or `parameter` tag. Patch
attributes are `category.name`, `display_name`, `name`, `program`, `bank`,
`volume`, `pan` and `instrument` (the routing alias). Global configuration fields
are `brightness`, `splash`, `usb_midi_interface`, `screensaver_timeout`,
`chord_ms` and `transition_ms`. Global fields and editable numeric patch fields
read current document values after edits. Additional parameter names must not
collide with these built-ins. Duplicate page fields and unknown references are
rejected during validation. Pages are bounded to 16 fields and must fit the
target's drawable height.

The responsibilities are:

- The window selects the page, supplies the current data item and defines the
  available display space.
- The page selects fields and their order.
- The field definition identifies how to obtain the value and may specify
  presentation details or style overrides.

Reusing a page must evaluate its fields against each window's own data item.
Global parameters read shared application state. Changes to either source must
update the affected displayed values.

Missing-value behavior: omit a field whose value is absent or whose
formatted text is empty, then distribute the remaining lines using the normal
spacing rule. Zero and false are values, not absence. An unknown field name is
a configuration error, not an empty value.

## Style properties

Use one partial style structure wherever styling is permitted:

| Property | Meaning |
| --- | --- |
| `foreground_color` | Text color |
| `background_color` | Background behind the line |
| `font_name` | Font family supported by the target |
| `font_size` | Requested size in pixels |
| `autoscale_fonts` | Whether to choose the largest fitting size for this line |

Retain the existing property names and color syntax. Read sizes through
`reccy.configuration.units`, including values such as `"24 px"`.

Every property is optional. Omission means inherit; a supported input format's
null value has the same meaning. Preserve absence until resolution. Apply
application defaults in one place after all applicable sources have been
considered. Explicit false values must override inherited true values.

Window geometry and padding belong to the window. Style changes must not alter
MIDI routing, program identity, patch selection or category membership.

## Inheritance

Resolve each property independently. The order below runs from lowest
to highest priority; a later explicit value replaces an earlier one:

1. Application defaults, with the current show-wide settings as show defaults.
2. Window style.
3. Page style.
4. Program definition style, when the line represents a program or patch.
5. Patch category style, when the line belongs to a category.
6. Show-local patch style, when the line represents that patch.
7. An explicit style on the displayed line.

This order lets reusable program definitions supply an appearance, categories
give their patches a common appearance, and individual patches make exceptions.
Page and window styles supply defaults for content that does not specify its
own appearance.

Merge properties, not entire styles. A patch that specifies only a foreground
color still inherits its font and background from the applicable sources.
The same line can consequently use a program's font, a category's foreground,
and a window's background.

Only consult sources that belong to the line. A category heading inherits the
category style, but not the current patch's or program's style. A patch label
can inherit all three. Unrelated page text uses the page and window defaults.

### Category labels and category-wide defaults

A category's style supplies defaults for both its heading and its patch labels.
An explicit style on the heading line can distinguish the heading without
changing the category's patches. Likewise, a patch or its displayed line can
override a category property for one entry.

For example, a brass category can supply gold text; its heading line can use a
larger font; and one muted-trumpet patch can override the text color. Other
patches in the category continue to inherit gold.

### Illustrative style fragment

The same partial `style` block attaches to program-table entries, show-local
patch entries, pages, windows and shared parameter definitions:

```toml
[style]
foreground_color = '0xFFD700'
background_color = 'black'
font_name = 'Arial'
font_size = '24 px'
```

Category styles use `[category_styles.CATEGORY]`; this leaves category patch
lists in `patches.CATEGORY`. Per-line overrides use
`[pages.PAGE.line_styles.FIELD]`, quoting field names containing dots:

```toml
[category_styles.brass]
foreground_color = '0xFFD700'

[pages.patch.line_styles.'category.name']
font_name = 'Arial'
font_size = '24 px'
```

Window padding is `windows.NAME.pad`; dimensions come from the associated
target display. Top-level `font_name`, `font_size`, `autoscale_fonts` and `pad`
remain show defaults. The old `screens.NAME` color tables are replaced by
`windows.NAME.style`, without a parallel configuration path.

## Layout and rendering

Resolve fonts before measuring text. Each line uses its resolved font family,
size, glyph bearings and line height. Horizontal centering must use that line's
metrics. Preserve letter proportions when autoscaling. Retain the existing
rule that enabled autoscaling overrides an explicit size. Autoscaling gives
each field an equal share of the padded height, reserving two pixels per
inter-line gap during font selection. Explicit font sizes may have unequal
heights if their combined height fits. Shared automatic sizing groups lines
with the same font, padding and field count, retaining the existing default
shared font choice.

After sizing and wrapping, retain the agreed vertical distribution. For N
rendered, nonempty rows, subtract their combined heights from the drawable
height and divide the remainder by N to obtain H. Leave H/2 at the top and
bottom and H between rows. Handle fractional pixels consistently and exclude
empty labels. Initial performance-page overflow is rejected by the compiler.
Management text retains wrapping and clipping. Numeric edits that outgrow their
compiled font are clipped; their horizontal centering is recalculated at draw
time. Other performance labels retain their compiled centered positions.

Background behavior: each line paints its horizontal band across the
drawable window width. The boundary between neighboring bands is halfway
through their gap. The first and last bands extend to the drawable area's
edges; the outer padding uses the window background. This gives lines with
different backgrounds a defined result even when their fonts have different
heights.

Apply the already-sent indication after resolving the line style. Retain its
foreground color and expose its background through alternating checkerboard
pixels after composing the glyphs. Evaluate contrast against the final color
pair, rather than against partially inherited colors.

The same page can resolve differently in different windows. Resolve the
destination appearance when content moves between undo, sent and next. Ensure
the completed transition displays the destination fonts and colors, and that
style-only changes invalidate the affected text and background.

## Implementation

The host display compiler resolves fields and styles for each patch/window
combination, deduplicating styles, pages and literal text in the compiled
document. The pedal receives bounded generic line records, not an inheritance
engine. Mutable attribute/global field codes read live document values.

Compiled format 12 uses resolved pages in place of category cursor offsets and `CFNT` metadata
with resolved pages. It stores authored display settings and partial overrides
as UTF-8 TOML metadata, so downloads preserve inheritance rather than flattening
it. Existing MIDI records remain authoritative for numeric edits. Older
documents require source recompilation; no compatibility decoder or automatic
migration is included. See the [binary protocol](../firmware/README.md).

## Verification

- Field names resolve unambiguously to attributes or global parameters.
  Duplicate definitions and unknown names are rejected during validation.
- Pages render fields in their specified order. The same page evaluates against
  each window's current data item, while global parameters use shared state.
- Missing or empty values follow the agreed policy without hiding zero or false.
- Partial overrides combine correctly at every level, including explicit false
  and absent values. The chosen precedence has direct examples in tests.
- Category headings do not accidentally acquire the selected patch's style.
- A category style reaches its patches; a patch override affects only that patch.
- One program displayed through different patches, pages or windows resolves
  independently without modifying its shared definition.
- Mixed font families and sizes fit, center and distribute vertically using
  their own metrics. Empty and wrapped lines are handled consistently.
- Background bands meet without gaps, and the already-sent pattern retains
  the final foreground color with alternating transparent pixels.
- A style-only edit repaints correctly. Send and undo finish with the correct
  destination appearance and preserve MIDI behavior.
- Export and recompile preserve both effective appearance and inheritance.

## Additional work beyond the prompt

None. Automatic configuration migration is not provided.
