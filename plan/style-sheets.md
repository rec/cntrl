# CSS style sheets for displays

Status: proposed, not implemented. This document describes the replacement for
style resolution in [display-format.md](display-format.md). That document records
the current implementation; this proposal supersedes its style properties,
precedence and category-priority rules when implemented.

## Goal

Use a useful, explicitly bounded subset of CSS to describe display appearance.
Supported CSS features must retain their standard meaning. Do not create a
CSS-looking language with different inheritance, specificity or property names.

Keep the current page/field model, bitmap renderer and host-side compilation.
The pedal does not need a CSS parser or a browser layout engine. The initial
migration must reproduce today's UI, including line spacing, transitions and
already-sent stippling.

## Current system to replace

`cntrl/patch_switcher/models.py` defines partial styles attached to windows,
pages, categories, programs, patches and parameters. Pages also have per-field
style overrides. `display.py` merges these in a fixed order, resolves category
styles using integer priorities, and compiles the result into display records.

Replace that merge order and category-priority mechanism with one CSS cascade.
Remove the old authored style blocks and their metadata representation once the
replacement is complete. Do not retain both configuration paths or translate
legacy styles silently at runtime.

Categories remain independently assignable to programs and patches. A patch's
effective categories remain the union of its child's categories and its own.
Their order still determines the category-label text, but never CSS precedence.
Category registration and membership are data, not style rules.

## Authoring

Use one show-level `stylesheet` string containing CSS. This keeps a setup
self-contained, works with the existing configuration composition machinery,
and allows downloads to preserve the authored rules without fetching files.
External sheets, imports and network resources are outside the initial scope.

Illustrative configuration:

```toml
stylesheet = '''
show {
    color: black;
    background-color: white;
    font-family: "Arial";
    font-size: 24px;
}

field.sax { color: #d08000; }
field.whole-system { font-family: "CourierPrimeBold"; }

window[name="next"] field[name="display_name"].sax {
    color: #008080;
}
'''

[pages.patch]
fields = ['group.name', 'display_name']

[windows.next]
page = 'patch'
```

The rules illustrate syntax, not a replacement color scheme for the current
pedal. The initial shipped sheet must be derived from the existing UI.

## Display tree and selectors

CSS needs a concrete element tree. Build a separate presentation tree for each
window's current content under one show root:

```text
show
  window [name="undo"]
    page [name="patch"]
      field [name="group.name"]
      field [name="display_name"]
  window [name="sent"]
    page [name="patch"]
      field ...
  window [name="next"]
    page [name="patch"]
      field ...
```

These are application element names, not HTML elements. Each page occurrence
has its own parent window, so the same page can have a different computed style
in each window. Each field is a line container; wrapped rows share its style.
The existing layout algorithm supplies their geometry.

Support initially:

- Type and universal selectors: `field`, `*`.
- Category class selectors: `.sax`, `.whole-system`.
- Exact attribute selectors: `[name="display_name"]`.
- Compound selectors: `field.sax.whole-system`.
- Descendant and child combinators: `window field`, `page > field`.
- Comma-separated selector lists.

No IDs, pseudo-elements, pseudo-classes, sibling combinators or functional
selectors are needed initially. Do not invent a custom `:sent` pseudo-class.
Ordinary attributes can expose application state if CSS styling of that state
becomes a requirement later.

### Connecting musical data to displayed fields

Programs and patches are referenced musical objects, not implicit CSS parents.
Do not put a program above a patch in the display tree merely to recreate the
old merge order.

Expose effective category membership as classes on patch-content fields and
on the `categories` field. Do not put those classes on the page or window:
that would also affect navigation headings and unrelated global parameters.
The `group.name` field and global fields have no patch category classes.

Expose source identity on patch-content fields using ordinary attributes:
`instrument`, `program`, optional `bank`, `group`, and `patch-index` (zero-based
within its authored navigation group). Values use the configuration's numbering,
including one-based MIDI program numbers. Program selectors use the routing
alias, program and bank; a particular patch can be selected by group and index.
Names displayed to the user are not assumed unique. Reordering a patch list
changes its positional selector, so category selectors are preferable for
reusable styling.

For example, a program-specific rule can select
`field[instrument="pman"][program="1"][name="display_name"]`.
A patch-specific rule can select
`field[group="sax"][patch-index="0"][name="display_name"]`.
Neither receives hidden precedence because it refers to a particular object.
A shared parameter is selected by its field name, just like other fields.

Class membership uses exact category names. CSS escaping is available for names
that are not simple CSS identifiers; do not silently normalize category names.
Keep explicit program and patch assignments separate in stored source metadata.
The union used for selector matching is derived, not written back into either.

## Properties and values

| Property | Initial supported authored values | Inherits |
| --- | --- | --- |
| `color` | CSS named colors, `#rgb`, `#rrggbb`, `currentColor` | Yes |
| `background-color` | Those colors, plus `transparent` | No |
| `font-family` | One quoted installed family name | Yes |
| `font-size` | Positive pixel lengths such as `24px` | Yes |

Support `inherit`, `initial` and `unset` with their CSS meanings on these
properties. `currentColor` has its CSS meaning, including on `color` itself.
An omitted declaration is not a special null value. CSS has no `None` or `null`
value for these properties.

Use standard CSS color names and syntax, not the old `0xRRGGBB` spelling or
application-specific color aliases. Convert computed colors to the target's
RGB565 representation only after resolution. Partial alpha, color functions,
gradients and images are outside this first subset.

A font's only selectable identity is its family name. `"CourierPrimeBold"` names
an installed bitmap family; the word Bold does not enable a weight operation.
Do not support `font-weight`, `font-style`, font synthesis or the `font` shorthand.
Size is the requested rendering size, separate from family identity. Family
lists, generic families, downloadable fonts and variable-font axes are deferred.
Validate the selected family against the target's installed fonts.

CSS lengths use CSS tokenization: `24px` is valid; `24 px` is not. Do not feed CSS
text through the more permissive configuration-unit parser. Internal normalized
sizes can still share the existing measurement machinery.

## Cascade and inheritance

Follow the [CSS cascade and inheritance specification](https://www.w3.org/TR/css-cascade/).
Use application defaults as a user-agent sheet and the configured stylesheet as
an author sheet. Initially accept ordinary declarations only; `!important`,
user-origin sheets, cascade layers and `revert` are outside the supported subset.

For each element and property, resolve origin, selector specificity and then
source order. Later declarations win ties, including duplicates within a rule.
Specificity follows CSS rules; category assignment order contributes nothing.
With equally specific `.sax` and `.whole-system` rules, the later rule wins a
conflicting property. Remove category `priority` and equal-priority errors.

Inheritance is a separate step after cascading. A declaration on a field wins
over an inherited value even if the ancestor's selector is more specific.
Properties absent on an element inherit or take their initial value according
to the property definition. Root inherited properties use their initial value
when otherwise unspecified. Define the application's initial font and text
color centrally; do not scatter repeated defaults through compiler and renderer.

The [CSS background rules](https://www.w3.org/TR/CSS1/#background-properties)
make backgrounds non-inherited and initially transparent. A transparent field
reveals its page, then window, then show background. Explicit
`background-color: inherit` instead copies its parent's computed background.
These can look the same but must remain distinct computed values.

## Layout and application effects

CSS supplies appearance; the existing application supplies the layout boxes.
Window dimensions and padding, field ordering, missing-value omission, wrapping,
centering and vertical distribution retain their current configuration and
behavior. Do not claim that this subset implements CSS block layout or flexbox.
Line backgrounds cover the existing bands; page and window backgrounds cover
their respective containers. Resolve transparent layers to the final painted
background before emitting opaque firmware records.

Keep `autoscale_fonts` as explicit application layout configuration, not a CSS
property or an invented `font-size: auto`. Move any per-style autoscale controls
into the layout model during migration. Preserve the existing fitting policy:
when enabled, it chooses the largest fitting bitmap size with unchanged glyph
proportions. The computed CSS size remains the requested size; the application
fit mode determines the actual raster size. Document that interaction clearly,
and test fixed-size and fitted modes separately.

Already-sent stippling remains an application effect applied after style
resolution. It uses the resolved foreground and exposes the composed background
through alternating pixels. It is not opacity, a second color, or a CSS font
property. Do not restore `half` or manufacture a CSS property for stippling.

Rounded rectangles are available in the graphics library, but borders and
`border-radius` are not part of this initial migration. Adding them would require
specifying the relevant CSS box and painting behavior, not just exposing a draw
function under a CSS name.

## Compilation, storage and errors

Parse and resolve CSS on the host for each displayed patch/window combination.
Reuse the bounded page, line, font and color records consumed by the firmware.
Do not run a general selector engine on the pedal. Continue resolving destination
appearance for transitions and repainting after style changes.

Store the authored stylesheet with the existing source metadata, alongside
pages, layout settings and explicit category assignments. Download/recompile
must preserve rule order and source intent rather than flattening computed
styles. Retain the original CSS text for round trips, including comments.

Use CSS parsing and recovery rules. Unsupported properties, values and selectors
must not acquire private meanings. Emit precise diagnostics with source position;
invalid declarations and selector lists follow CSS discard semantics. The build
may fail on these diagnostics as an explicit authoring-validation policy, rather
than silently producing a pedal setup that ignores a requested effect. Clearly
distinguish that validation policy from CSS parsing behavior.

Before implementation, evaluate existing parser dependencies against the subset.
Do not build a parser with regular-expression splitting or assume a tokenizer
also implements selectors and the cascade. Adding a dependency requires a
concrete need and the repository's dependency workflow.

## Implementation sequence

1. Fix the subset and presentation-tree contract in tests, including selector
   specificity, source order, inheritance and source-data attributes.
2. Implement host parsing, selector matching and computed styles. Keep this
   internal during development; expose only the replacement authoring path.
3. Replace partial style blocks and category priorities throughout models,
   compilation, exports and metadata. Separate fitting configuration from style.
4. Convert repository configurations and examples to CSS, preserving their
   appearance. Translate old precedence into explicit selectors only where the
   authored result needs it; do not bake it into the new engine.
5. Update documentation and bounded format fixtures. Change the format version
   if the authored metadata contract changes; require recompilation rather than
   adding a legacy decoder or automatic migration system.
6. Verify the current full pman UI on the pedal when deployment is requested.
   Automated equivalence tests do not establish physical appearance.

This is an architectural replacement of style resolution, not a rewrite of the
renderer, MIDI engine, page model or category membership model.

## Acceptance criteria

- Supported syntax and property semantics agree with CSS, including comments,
  escaping, selector lists, specificity and declaration order.
- A directly specified child value beats an inherited value; changing ancestor
  specificity does not change that result.
- Categories combine through class matching; neither membership order nor old
  category priority affects the result.
- Program and patch selectors work independently, while headings and global
  parameters remain unaffected by patch categories.
- Transparent backgrounds reveal the correct ancestor; `inherit`, `initial`,
  `unset` and `currentColor` behave correctly.
- Family names select exact installed fonts without weight or italic synthesis.
- Fixed-size and autoscaled text preserve current fitting, centering and spacing.
- Already-sent glyphs retain their foreground and correctly expose backgrounds.
- Export/recompile preserves CSS source order and explicit category assignments.
- Existing UI fixtures match after conversion, and MIDI/send/undo behavior is
  unchanged. The previous style resolver and obsolete configuration are removed.

## Additional work beyond the prompt

None. This document proposes the implementation; it does not authorize changing
code, dependencies, configuration files or the installed pedal firmware.
