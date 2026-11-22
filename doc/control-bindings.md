# Patch bindings

A patch can install its own controls across the whole pedal. Put a `bindings`
table on the patch, using the switch labels as keys and patch names as values:

```toml
[instruments.guitar]
instrument = 'guitar'
channel = 1
output = 'din'

[patches.clean]
program = 1
volume = 90

[patches.boost]
controller = 7
value = 110

[patches.verse]
parent = 'clean'
bindings = {A = 'boost', B = 'chorus', E = {operation = 'next-song-step'}}

[patches.chorus]
parent = 'verse'
program = 2
bindings = {A = 'verse', B = 'boost', E = {operation = 'next-song-step'}}

[patches.verse_louder]
parent = 'verse'
volume = 110

[[songs]]
name = 'Our song'
patches = ['verse', 'chorus', 'verse_louder', 'chorus', 'verse']
```

This example is a song library. The same patch definitions work in a setup,
with `patch_cycles` selecting the patches instead of `songs`.

Selecting `verse` sends its entry MIDI and installs its controls and display
when the entry operations complete.
Pressing B selects `chorus`, replacing the map. Pressing A on `chorus` selects
`verse`. Calling `boost`, which has no bindings, sends its MIDI while keeping
the current map. E advances through the current song's patches, then to the
next song in the selected setlist. During set performance, F always selects the
next step on release; C+F selects the previous step in either press order. C is
the modifier and has no standalone action. These controls take precedence over
authored bindings and work regardless of `inherits`.

## Binding scopes and inheritance

Bindings can also live at the top level of the existing `global.toml` defaults,
at the root of a setup or song library, and on each song. Global defaults are
resolved when compiling, so changing the global file requires recompiling its
consumers. Song bindings follow the active song at playback. Renaming a song
on the pedal preserves its bindings, as do saved edits and undo.

A song assignment replaces the shared assignment for that control. Other
controls remain inherited. The tables accept the same switch and expression
assignments used on patches. Setlists contain a name and a list of songs. For
older libraries with setlist bindings, remove the bindings from the source and
recompile before loading the library.

Every patch has an `inherits` setting:

| Value | Higher-scope controls included in a new patch map |
| --- | --- |
| `none` | None |
| `switch` | Switch assignments only |
| `expression` | Expression assignments only; the default |
| `both` | Switch and expression assignments |

The patch's own bindings override the inherited assignments. A full-pedal patch
therefore leaves unused switches inactive by default, while retaining the song's
expression assignments. Inheritance resolves enclosing scopes, not the previous
patch. Derivation with `parent` still resolves authored patch fields separately.

For example, in a song library:

```toml
[bindings.expression-jack]
output = 'din'
channel = 1
controller = 7

[patches.verse]
bindings = {A = 'boost', E = {operation = 'next-song-step'}}

[patches.boost]
output = 'din'
channel = 1
controller = 7
value = 110

[[songs]]
name = 'One'
patches = ['verse']
bindings = {expression-jack = {output = 'din', channel = 1, controller = 11}}

[[setlists]]
name = 'Tonight'
songs = ['One']
bindings = {expression-jack = {output = 'din', channel = 1, controller = 10}}
```

Verse inherits CC11 from One, overriding Tonight's CC10 and the global CC7.
Pressing A executes Boost without replacing Verse's map. A patch without
`bindings` remains an ordinary action, even if it specifies `inherits`. Use
`bindings = {}` to explicitly install an inherited-only map. Combining that
with `inherits = 'none'` installs an entirely empty map.

The resolved map supports up to eight switches. A three-display `binding_view`
shows up to six; larger maps use the ordinary switch presentation. Inherited
switch actions retain their source definitions and routing even when the
selected patch comes from another setup.

Playback always includes an **Empty position** before and after a song. A set
shares one empty position between consecutive songs: `. (A B C) . (D E) .`.
Previous walks exactly the same positions backwards. Empty positions are runtime
boundaries, not authored patches. Leaving a song releases its controls and runs
its optional `exit` action; stepping into a song runs its optional `enter` action
before the selected patch. The next song's enter action requires another advance.
Returning to the initial empty position and advancing resends song and patch
startup actions. Selecting a song or set starts at that initial position without
sending either action.

Explicit patch actions use `next-song-step` and `previous-song-step`. Song
authoring still uses an ordered `patches` list: each occurrence is a step, and
the mandatory empty positions are separate from that list.

Song records include binding references, styles and enter/exit actions and use `CSP9`.
Recompile song libraries and use matching host and firmware versions. Existing
offline backup conversion preserves older selections; firmware uses the current
format. The old optional last-step setting has been removed.

## Replacing maps

A derived patch inherits its parent's bindings. Supplying another `bindings`
table replaces that entire authored map. Omitting the table leaves the current
map unchanged; an empty table owns the surface with only the categories selected
by `inherits`. Unassigned switches do nothing while a map is active. Normal page
navigation does not intercept D or F, or any other switch.

## Momentary switches and gestures

A binding can also be an ordinary inline patch, or an expanded switch binding.
The expanded form uses the existing switch options, without repeating `button`:

```toml
[patches.verse.bindings.A]
name = 'Sustain'
mode = 'momentary'
on = {controller = 64, value = 127}
off = {controller = 64, value = 0}

[patches.verse.bindings.C]
on = 'boost'
gestures = {long_press = {controller = 7, value = 120}, long_delay = '500 ms'}
```

The modes are `press` (default), `momentary`, and `latched`. Momentary and
latched bindings require `off`. Press bindings support the existing release,
long-press, double-tap and repeat settings, plus entry/exit hooks and shifted
actions. `name` supplies a display label. MIDI routing uses the same explicit
outputs/channels or single-instrument inference as any other patch.

Actions that replace or leave the control map must be simple press bindings.
Put `verse_louder` directly on a switch to enter that map. Gesture actions can
use MIDI-only patches. This restriction lets release and momentary actions
finish in the context that owns them.

When replacing a map, active momentary/latched controls send their cleanup
actions, and recognized gestures finish their matching releases. Undecided
taps and queued gestures are cancelled. Held switches are disarmed in the new
map until released. Background MIDI, clocks, playback and recording continue.

## Performance display

A patch with bindings can also specify a `binding_view` for a three-display
performance view. It places the first three switches along the bottom and the
next three along the top, with a bank title, last pressed control's detail and
status in the middle bands. It supports up to six switches.

```toml
[patches.verse.binding_view]
font_name = 'Arimo'
maximum_size = 18
detail_size = 18
status_size = 12
align = 'switch'
show_labels = false
status = 'B001 PG1'

[patches.verse.binding_view.status_style]
foreground_color = 'blue'
```

`align = 'switch'` centres each short name in the display between its pair of
switches; `'left'` left-aligns all short names. Sizes are upper limits in the
target's native UI font sizes; the compiler fits each label's actual glyph bounds
within its band. Choose an embedded UI font with exported glyphs. A binding's
`name` supplies the short label, `display_name` its detail, and `style` its
foreground and background colors. The patch's style supplies the colors for
all three middle bands, including the long name. `binding_view.status_style` can override the
indicator's colors and font; the indicator is left-aligned at the bottom of its
band. Empty middle bands retain their background color.

## Song styles

During set performance, the current song's `style` supplies missing foreground,
background and font properties in these displays, including the ordinary switch
presentation when `binding_view` is omitted. Explicit patch, switch and status
styles take precedence per property. Song fonts use the controller's embedded
families. Fixed-size families use their smallest size when it exceeds a nominal
size limit; labels are fitted or shortened within their bands.

Recompile full-pedal setups to enable this inheritance. Older compiled binding
views retain their fixed display values, because they did not record which
properties were explicitly authored.

## Expression pedals

Expression assignments share the same table, keyed by the configured jack name:

```toml
[patches.verse.bindings.expression-jack]
output = 'din'
channel = 1
controller = 11
minimum = 0
maximum = 127
```

Use the actual jack ID from the target configuration. The jack must already
have the `expression` TRS role in the active setup. `reverse` and `curve` work
as in `page_settings.expression`. Calibration and input filtering are shared
with the existing expression subsystem. Set `mirror_output` to a second enabled
MIDI output to send each expression value there as well as to `output`.
A resolved map owns all expression assignments. Pedals supplied by enclosing
scopes remain active when `inherits` includes expression; all other unassigned
pedals are inactive until another map or the normal setup restores them.

Maps are stored in the compiled patch graph, including song sources and setup
bundles. Saving and history preserve them. The active map keeps an owned copy
of its source document, allowing a song to load its next source safely. Source
copies use the existing measured heap budget; allocation failure reports an
error before dispatching the new patch's entry MIDI.
