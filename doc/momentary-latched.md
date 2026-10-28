# Momentary and latched actions

Actions is an optional module for two-state MIDI assignments. Open it from the
module menu. Every visit starts locally Off without transmitting anything.
Held entry switches must be released before they can activate an assignment.

Configure assignments in the performance setup:

```toml
[actions.sustain]
button = 'A'
mode = 'momentary'
on = { instrument = 'piano', controller = 64, value = 127 }
off = { instrument = 'piano', controller = 64, value = 0 }

[actions.solo]
button = 'B'
mode = 'latched'
on = { instrument = 'sax', controller = 7, value = 110 }
off = { instrument = 'sax', controller = 7, value = 85 }
```

Instrument names refer to existing setup aliases. A momentary action requests On
when pressed and Off when released. A latched action alternates on successive
presses. Both states can instead reference named patches with
`on = { patch = 'Solo' }` and `off = { patch = 'Clean' }`. Each name must match
exactly one resolved patch name. Both states must have the same output/channel.
Other assignments cannot own overlapping MIDI controls on that output/channel.

An optional `name` defaults to the assignment key; `style` uses the existing
formatting properties. The optional `pages.actions` page accepts up to three
action, parameter or global-setting fields. Its default fields are
`action.name`, `action.mode` and `action.state`. `action.name` includes the bound
button in its display label; `action.button` exposes just that label. State text
is fitted using the longest error state at compile time. Fonts and colors use
the existing compiler and firmware page machinery. Icons remain references only.

The page displays On, Off, Pending or Unknown / Unavailable. These report the
pedal's local scheduling state, not acknowledgement from an instrument. Off sends
the explicit authored command and does not reconstruct the previous sound.

F begins cleanup and waits for accepted On/Off work to finish. F again explicitly
abandons blocked cleanup and reports unknown remote state. A disconnected output
also produces a warning. Reconnecting never replays old commands; an explicit
new activation establishes the Off baseline before On. Other modules, including
Panic and firmware switching, are reached only after leaving Actions. Thus normal
navigation performs cleanup first; after explicit abandonment it cancels unsent
action work before those modules can run. Configuration/asset transfer commands
are rejected while Actions is open; close it before replacing a setup. Status
queries remain available. Playback undo is kept separate from action state.

The scheduler reserves one fixed batch slot per assignment, outside the ordinary
pedal queue's capacity. An assignment waits for its accepted batch to complete
before reusing its slot. Requested transitions coalesce in bounded state, while
required Off commands retain their slot. No action allocates memory per press.
Resolved commands are copied on entry; configuration remains locked until exit,
so pending work never borrows replaced document or staging bytes. Patch references
resolve from the current decoded patch records, including saved pedal edits.
Edits that create action ownership conflicts are rejected transactionally.

Live state is never persisted. Configuration round-trips through compiled format
15, resolved TOML, setup transfers and revision history. Older compiled setups
must be exported with the matching host version and recompiled before use with
format 16; this change does not add a legacy firmware decoder.

Native sanitizer tests cover press/release, latching, coalescing, queue pressure,
cleanup, abandonment, disconnect/rearm and malformed commands. Python tests cover
configuration errors, ownership conflicts, target capacity and byte-for-byte
export/recompile. Physical latency, switches and external MIDI reception require
qualification on the pedal.
