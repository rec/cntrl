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
bindings = {A = 'boost', B = 'chorus', E = {operation = 'next-song-patch'}, F = {operation = 'back'}}

[patches.chorus]
parent = 'verse'
program = 2
bindings = {A = 'verse', B = 'boost', E = {operation = 'next-song-patch'}, F = {operation = 'back'}}

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
next song in the selected setlist. F returns to the preceding performance UI.

A derived patch inherits its parent's bindings. Supplying another `bindings`
table replaces that entire map. Omitting the table leaves the current map
unchanged; an empty table explicitly owns the surface without assigning any
controls. Unassigned switches do nothing while a map is active. Normal page
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
with the existing expression subsystem. A map owns all expression assignments;
unassigned pedals are inactive until another map or the normal setup restores
their assignments.

Maps are stored in the compiled patch graph, including song sources and setup
bundles. Saving and history preserve them. The active map keeps an owned copy
of its source document, allowing a song to load its next source safely. Source
copies use the existing measured heap budget; allocation failure reports an
error before dispatching the new patch's entry MIDI.
