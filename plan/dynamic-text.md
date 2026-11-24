# Dynamic text

The first implementation covers `switch_label` and `scene_status` with
patch names and shared tempo. General templates in `display_name` remain
planned. See [performance display](../doc/control-bindings.md#performance-display).

Allow authored display text to include values that change during performance.
Start with `display_name` on patches, scenes, songs, and setlists, plus
user-authored switch labels. The same syntax applies in browser and setlist
modes wherever those fields appear. Keep reference names and IDs as authored
so a display change never changes which patch, song, or setlist is selected.

```toml
display_name = 'Evening Song {tempo} bpm'
```

With the pedal at 120 BPM, the display reads **Evening Song 120 bpm**. When
the tempo changes to 124 BPM, the same display reads **Evening Song 124 bpm**.

## Syntax

- `{var_name}` inserts the current value of a supported variable. Start with
  `tempo`, which uses the pedal's selected tempo source and displays rounded
  whole beats per minute. Authors supply any unit suffix in the label.
- `{{` and `}}` produce literal braces. For example, `{{tempo}}` displays
  `{tempo}`, and `{{{tempo}}}` displays `{120}` at 120 BPM. A single closing
  brace can remain ordinary text.
- Report unknown variable names and malformed opening braces when compiling
  the setup or song library, with the field name and the offending text.
  If a valid variable has no current value, display `--` in its place. A
  selected external clock that has yet to supply a usable tempo is one example.
- Keep existing counter, bank, and page substitutions valid in the fields
  that already support them. Use the same escaping rules when those fields
  adopt the template parser.

## Compilation and rendering

Parse templates on the computer and store literal fragments and variable IDs.
Resolve brace escapes at authoring time. When a supported field can be edited
on the pedal, validate and prepare its text when the edit is saved.
Keep the original authored text available for editing and export.

**Literal labels use the existing render path.** A string without an opening
brace, including one whose closing braces were unescaped while compiling,
must need no additional per-string storage, scanning, variable lookup, or
redraw tracking on the pedal. An escaped-only string such as `{{tempo}}` also
compiles to an ordinary literal label. Use compiled template metadata to
identify dynamic text and preserve the literal display of existing compiled
setups and songs.

At runtime, resolve values from the same clock state used by the pedal's
tempo display. Track the variables referenced by visible text. When a
referenced value changes, update the affected labels and redraw them only if
their rendered text changes. Deduplicate updates that render to the same
whole BPM. Resolve the current values when a label becomes visible again.
Use bounded text buffers and preserve complete UTF-8 characters when text
must be shortened. Prepare the required glyphs for each variable at compile
time so fitting and drawing use the normal display path.

## Verification

- Compile the example and change the selected tempo while the title is
  visible. Verify that only labels that reference tempo are updated.
- Cover multiple references in one label, both brace escapes, a template
  containing only escaped braces, invalid names and syntax, and an unavailable
  tempo becoming available.
- Compare ordinary labels with the current renderer and verify that they
  take the existing literal path and perform no variable work.
- Check the existing counter, bank, and page substitutions and verify that
  exporting a compiled setup or song preserves its authored template.

## Additional work beyond the prompt

None.
