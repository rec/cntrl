# Remaining song and setlist work

Songs, ordered Patch positions, live references, on-pedal creation and setlist
editing are implemented. Their current behavior and storage limits are in the
[usage guide](../cntrl/maker/morningstar/mc/songs-and-setlists.md) and
[Patch authoring](../doc/patches.md).

## Optional named sections

Named sections such as intro, verse, chorus and solo remain deferred. Add them
only when a performer chooses to use them. A simple song must remain usable
without section fields, a section editor or an artificial main section. Reuse
existing song pickers and setlist editing rather than creating parallel flows.

Different section orders for separate occurrences of one song are also deferred.
Define their identity, persistence and advancement semantics before implementing
them. Repeated occurrences must stay independent, and merely browsing or editing
must never recall a sound.

## Physical qualification still required

Verify on the pedal that a performer can create a song without typing, build a
five-song setlist in one picker visit, distinguish duplicate names and navigate a
large library. Exercise Add next ordering, removal and movement of the current
occurrence, edit Undo, End of set, restart and restart-after-save behavior.

Capture MIDI for ordered Patch positions and a stock-firmware visit between two
MIDI positions. Check nondefault return buttons, power cycling in stock, held
return buttons, a final stock position and changed or unavailable source data.
Automated continuation tests and a successful flash do not qualify these physical
flows. Measure service latency under sustained MIDI and storage load.
