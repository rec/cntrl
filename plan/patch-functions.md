# Patch functions

Status: implemented on `codex/patch-functions`; built and tested, not flashed at
the user's request. Physical qualification is pending. Extend patches to perform functions on the pedal as well as
send MIDI. The first function is `stock-firmware`, with one argument identifying
the button combination that returns to patch mode.

## Patch representation

Keep the existing MIDI patch representation. Add a function patch as a second
typed alternative, sharing the existing name, display name, categories and style
properties. A function patch has no instrument, program, bank, volume or pan.
Reject definitions that mix those MIDI properties with a function.

Proposed TOML entry, within the existing patch collection:

```toml
{ function = 'stock-firmware', exit_buttons = 'A+F' }
```

`function` selects the operation; `exit_buttons` is its sole argument. Require
the argument explicitly. Optional presentation properties are not function
arguments. Validate the combination against the target's buttons: exactly two
distinct supported buttons, in either order. Normalize equivalent combinations
when compiling. Unknown functions and unsupported target capabilities are errors.

Use explicit typed function definitions and a small dispatch switch. Do not add
scripts, arbitrary command strings, a plugin loader or a generic argument bag.
This first version allows one MIDI selection or one function per patch. Ordered
mixtures belong in the song's patch list, not in a second action-list mechanism.

## Activation

Browsing, displaying and editing a function patch never execute it. The normal
patch activation action dispatches either MIDI or the pedal function. Use the
same display and category/style resolution as other patches; fields requiring
a MIDI program are empty for function patches.

A failed function leaves the current performance position unchanged and reports
an error. Navigation and Undo must not implicitly re-enter stock firmware.
Explicitly activating the function again starts a new visit.

Put dispatch and song continuation in portable firmware. Implement firmware
switching through the target hardware boundary, reusing the operation currently
invoked by the Original firmware module. That module remains available and does
not acquire song-advance behavior merely because a song is selected.

## Songs containing several patches

The current song model supports one inline selection or one live reference.
Extend it to one ordered `patches` list, whose entries are inline MIDI patches,
function patches or existing live references. A one-entry list remains a simple
song. Replace the single-selection representation rather than maintaining two
authoring paths. Update song import/export and the on-pedal editor together.

Assign persistent identities to patch occurrences, just as setlist occurrences
already have identities. Two uses of the same patch are separate positions.
Preserve the setlist occurrence, song and patch occurrence across a firmware visit.

Example performance sequence:

1. Intro MIDI patch.
2. `stock-firmware`, with `exit_buttons = 'A+F'`.
3. Solo MIDI patch.

Activating step 2 starts stock firmware. Pressing the configured combination in
stock returns to patch mode and **activates step 3 automatically**, including its
MIDI transmission. No additional press is required. Merely highlighting step 3
would not satisfy this behavior.

If the returning function was the last patch, show End of song and send nothing.
Do not wrap or start the next song automatically. Outside a song, return to the
patch browser without automatic activation. If the next patch is another
function, activate it through the same dispatch path.

## Persisting a visit and returning

Firmware switching resets the processor, so an in-memory cursor is insufficient.
Before leaving patch mode:

1. Finish pending MIDI and cooperative storage work. Require saved source data.
2. Persist a small continuation record using the existing storage conventions:
   a visit identifier, setup/library identity and revision, setlist occurrence
   when present, song and current/next patch occurrences.
3. Persist the exit combination and visit identifier in storage readable by the
   stock return hook. Verify the records before committing the stock boot choice.
4. Reset into stock only after these operations succeed.

Keep storage operations cooperative so MIDI and the UI remain responsive. Reuse
existing persistence machinery; no database or general session snapshot is needed.

The stock hook reads the configured pair for this visit, retains the current
50 ms window with no hold, and waits for the pair to be released before arming.
When triggered, it records an explicit return for that visit, selects patch mode
and resets. This requires runtime pair configuration, replacing the current
build-time-only choice for function-driven visits. No new gesture is added to
patch mode's button reader.

On return, validate the visit marker and continuation against the saved data,
restore the performance context, consume the continuation durably, then activate
the next patch once. Suppress held return buttons until release. Reuse normal
activation so MIDI ordering, display transitions and subsequent navigation agree.

A normal patch-mode boot without the matching return marker sends no automatic
MIDI. Power loss while in stock preserves the visit and selected firmware; a later
explicit exit can still resume it. Missing or changed source data, an invalid
marker or unavailable storage reports an error without recalling a guessed patch.

Consuming before activation prevents repeated MIDI or repeated firmware entry
after another reset. A power failure between consumption and activation may skip
the automatic recall; require manual activation in that case. Do not claim
exactly-once MIDI delivery across arbitrary power failures.

Target-specific record layouts, flash capacity checks and stock-hook changes
belong beside the existing
[firmware switch implementation](../cntrl/maker/morningstar/mc/firmware/boot_switch/README.md).
Reserve space for the entire round trip before entering stock. Do not add update
handling or journal compaction to this feature.

## Implementation order and verification

1. Add typed function patches to validation, compilation, firmware decoding and
   resolved export. Version affected formats and regenerate the development
   fixtures; do not introduce parallel legacy parsers.
2. Extend songs to ordered patch occurrences, preserving the simple one-patch
   creation flow and existing live-reference behavior.
3. Share the firmware-switch operation between the module and function dispatch.
   Add per-visit exit configuration and persistent continuation.
4. Test validation, round-trip encoding, unsupported targets, browsing without
   execution, failed entry, repeated occurrences, next-patch activation and end
   of song. Exercise interrupted persistence and stale/consumed return markers.
5. Build and flash through upload_helper. Physically verify a song containing
   MIDI, stock firmware and MIDI, including a non-default exit pair, a power cycle
   in stock, return-button release, and a final stock-firmware patch. Verify MIDI
   reception as well as the displayed position.

## Additional work beyond the prompt

None.
