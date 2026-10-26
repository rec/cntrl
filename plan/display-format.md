# Per-line display formatting

Status: proposal, 2026-10-26. This document describes a possible extension;
the configuration syntax and precedence rules below are not implemented.

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
- **Page:** the content and arrangement of lines presented together.
- **Window:** the display region presenting a page, with dimensions, padding
  and default appearance. A window may occupy an entire physical display.
- **Line:** one displayed text item, with a source such as a category or patch.
  A wrapped item produces multiple rendered rows.

The current implementation has show-wide font settings, colors for the undo,
sent and next screens, and optional independent font sizes for category and
patch lines. Pages and windows are concepts for this proposal, not existing
general-purpose configuration objects. Define their relationship to the current
three screen roles before implementation.

## Style properties

Use one partial style structure wherever styling is permitted:

| Property | Meaning |
| --- | --- |
| `foreground_color` | Text color |
| `background_color` | Background behind the line |
| `font_name` | Font family supported by the target |
| `font_size` | Requested size in pixels |
| `autoscale_fonts` | Whether to choose the largest fitting size for this line |
| `half` | Color for the existing already-sent patch indication |

Retain the existing property names and color syntax. Read sizes through
`reccy.configuration.units`, including values such as `"24 px"`.

Every property is optional. Omission means inherit; a supported input format's
null value has the same meaning. Preserve absence until resolution. Apply
application defaults in one place after all applicable sources have been
considered. Explicit false values must override inherited true values.

Window geometry and padding belong to the window. Style changes must not alter
MIDI routing, program identity, patch selection or category membership.

## Proposed inheritance

Resolve each property independently. The proposed order below runs from lowest
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
own appearance. They are not forced overrides in this proposal.

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

The following is a proposed style block, not a complete or currently accepted
show document. The same block shape would attach to each supported source:

```toml
[style]
foreground_color = '0xFFD700'
background_color = 'black'
font_name = 'Arial'
font_size = '24 px'
```

The category container and page/line syntax still need to be defined; do not
introduce a second way to specify the same style property while doing so.

## Layout and rendering

Resolve fonts before measuring text. Each line uses its resolved font family,
size, glyph bearings and line height. Horizontal centering must use that line's
metrics. Preserve letter proportions when autoscaling. Retain the existing
rule that enabled autoscaling overrides an explicit size; define the available
height for each line as part of page layout.

After sizing and wrapping, retain the agreed vertical distribution. For N
rendered, nonempty rows, subtract their combined heights from the drawable
height and divide the remainder by N to obtain H. Leave H/2 at the top and
bottom and H between rows. Handle fractional pixels consistently and exclude
empty labels. Text that cannot fit must follow an explicit page overflow policy.

Proposed background behavior: each line paints its horizontal band across the
drawable window width. The boundary between neighboring bands is halfway
through their gap. The first and last bands extend to the drawable area's
edges; the outer padding uses the window background. This gives lines with
different backgrounds a defined result even when their fonts have different
heights. Confirm this choice before implementation.

Apply the existing already-sent indication after resolving the line style.
An inherited or explicit `half` value applies to that line; otherwise derive
the midpoint from its final foreground and background. Evaluate contrast against
the final color pair, rather than against partially inherited colors.

The same page can resolve differently in different windows. Resolve the
destination appearance when content moves between undo, sent and next. Ensure
the completed transition displays the destination fonts and colors, and that
style-only changes invalidate the affected text and background.

## Implementation outline

1. Agree on precedence, page/window ownership, category heading behavior,
   background bands and overflow behavior.
2. Add the partial style model to the relevant content and presentation models.
   Keep unspecified values absent through parsing and composition.
3. Resolve styles in the host compiler for each applicable content/window
   combination. Use the same resolver for any preview or diagnostics.
4. Extend compiled line records and the portable rendering interface to carry
   the resolved font and colors. Select the font and colors for each line when
   rendering. Assess document size and rendering cost before choosing a binary
   representation; avoid a separate general-purpose style engine on the pedal.
5. Preserve source-level overrides in editable downloads so a round trip does
   not flatten inherited properties into explicit values. Version any changed
   compiled format and document the required recompilation.
6. Verify mixed fonts, backgrounds, inheritance and transitions on the device.

## Verification

- Partial overrides combine correctly at every level, including explicit false
  and absent values. The chosen precedence has direct examples in tests.
- Category headings do not accidentally acquire the selected patch's style.
- A category style reaches its patches; a patch override affects only that patch.
- One program displayed through different patches, pages or windows resolves
  independently without modifying its shared definition.
- Mixed font families and sizes fit, center and distribute vertically using
  their own metrics. Empty and wrapped lines are handled consistently.
- Background bands meet without gaps, and the already-sent color is derived
  from the final style when no explicit `half` is supplied.
- A style-only edit repaints correctly. Send and undo finish with the correct
  destination appearance and preserve MIDI behavior.
- Export and recompile preserve both effective appearance and inheritance.

## Additional work beyond the prompt

None. Implementation, configuration migration and device deployment are future
work; this change only records the proposed display-format design.
