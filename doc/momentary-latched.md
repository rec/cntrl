# Switch assignments

Switches is an optional module for press, momentary and latched Patch assignments.
Open it from the module menu. Every visit starts locally Off without transmitting
anything. Held entry switches must be released before they activate an assignment.

Configure assignments in the performance setup:

```toml
[switches.sustain]
button = 'A'
mode = 'momentary'
on = { type = 'control-change', output = 'din', channel = 1, controller = 64, value = 127 }
off = { type = 'control-change', output = 'din', channel = 1, controller = 64, value = 0 }

[switches.solo]
button = 'B'
mode = 'latched'
on = { type = 'control-change', output = 'trs', channel = 2, controller = 7, value = 110 }
off = { type = 'control-change', output = 'trs', channel = 2, controller = 7, value = 85 }

[switches.start]
button = 'C'
on = { type = 'start', output = 'din' }
```

`press` is the default mode and requires only `on`; it forbids `off`. Each new
press invokes the Patch once. A momentary switch requests On when pressed and
Off when released. A latched switch alternates states on successive presses.
Both persistent modes require an explicit `off` Patch. On and Off may have
different Patch types, outputs and channels.

Each branch accepts the common [Patch representation](patches.md), including
inline program selections, sequences and stable references such as
`{ patch = 'clean' }`. Program selections use instrument
aliases; other MIDI variants name their output directly. Definitions are keyed
by `patches.clean`, independently of their display labels.

## Programmable gestures

A `press` assignment can include a `gestures` table. `on` is its single-press
patch. The six optional gesture patches accept the same Patch representation.

```toml
[switches.lead]
button = 'A'
on = { patch = 'clean' }

[switches.lead.gestures]
release = { patch = 'clean-release' }
long_press = { patch = 'solo' }
long_release = { patch = 'solo-release' }
double_tap = { patch = 'boost' }
double_release = { patch = 'boost-release' }
repeat = { patch = 'step' }
debounce = '20 ms'
long_delay = '500 ms'
double_delay = '300 ms'
repeat_interval = '100 ms'
```

Those timing values are the defaults. Times have millisecond resolution and
must be at least the debounce interval. The debounce interval is 1 to 1000 ms,
and the other intervals are at most 65.535 seconds.

Without long press, double tap or repeat, `on` runs as soon as the press has
debounced. An assignment without `gestures` keeps its existing immediate input
handling. With long press or repeat, a short press runs `on` followed by
`release` when released. Holding through `long_delay` runs `long_press`, then
`repeat` if configured, and repeats at `repeat_interval` until release, when
`long_release` runs. The long gesture excludes `on` and `release`.

With `double_tap`, a short first release starts the double-tap window. A second
debounced press strictly before `double_delay` runs `double_tap`, and its release
runs `double_release`. That pair excludes the single and long gesture patches.
Otherwise, when the window expires, `on` and `release` run in order. This adds
up to `double_delay` after the first release to single-tap latency, plus input
debouncing and the normal service interval. A press exactly at the deadline
starts a new tap. `long_release` requires `long_press` or `repeat`, and
`double_release` requires `double_tap`. Omit any unneeded patch.

The gestures table is available only in `press` mode. Gesture patches stay in
the switch context, so stock-firmware visits and menu/back navigation are
rejected, including through references and sequences. F cancels pending
recognition and queued gesture actions while already accepted work completes.
Held entry switches require release before rearming. Disconnect or execution
failure cancels pending gestures, and reconnection waits for a fresh press.

Each assignment has a 16-action queue. Held repeats coalesce while an action is
pending and resume without catching up missed intervals. If physical input
fills the queue, the assignment reports an error and cancels its queued work.
It rearms after release. MIDI delivery continues through the shared executor.

Persistent assignments reserve the controls their reachable patches affect.
Notes, pressure, direct Program Change, system messages and SysEx reserve their entire destination output until Off cleanup or
explicit abandonment. Other assignments cannot share those reserved resources.
Two press assignments can share controls because each invocation completes its
accepted work. Firmware visits and operations that leave this context are
allowed only in press assignments. Other assignments finish cleanup before the
context is left.

An optional assignment `name` defaults to its dictionary key; `style` uses the
normal formatting properties. The optional `pages.switches` page accepts up to
three switch, parameter or global-setting fields. Its default fields are
`switch.name`, `switch.mode` and `switch.state`. `switch.name` includes the button
in its label; `switch.button` exposes just that button. Status text is fitted
against all possible status strings at compile time. Icons remain references
for future use.

The page displays On, Off, Pending, Completed locally, Unknown remote state or
Failed / unknown. These describe local execution and scheduling;
completion does not acknowledge receipt by an instrument. Off executes its
authored Patch and does not reconstruct the previous sound or roll back effects.

F begins cleanup and waits for accepted On/Off work to finish. F again abandons
blocked cleanup. Disconnecting an active output marks its state unknown.
Reconnecting never replays old work; an explicit new activation establishes the
Off baseline before On. Configuration and asset replacement stay locked while
the module or an invocation holds its source data. Status queries remain
available. Program-selection Undo is separate from switch state.

Each assignment uses the shared Patch executor and its reserved scheduler owner.
Backpressure preserves accepted work and required Off cleanup. Referenced source
documents remain pinned for the assignment context, so pending work cannot read
replaced configuration or staging bytes. Storage and memory limits are actual
resource limits; there is no separate short-message command buffer or fixed
sequence-depth restriction.

Live On/Off state is never persisted. This configuration uses the current
compiled setup format and the tagged authoring schema. Older setups need conversion and recompilation;
there is no legacy firmware decoder. See [Patch authoring](patches.md) for the
complete schema and required authoring changes. Exports after pedal program edits
canonicalize synthesizer-name metadata while preserving MIDI selections and the
visible labels; recompiling that canonical export is stable.

Native sanitizer tests cover edge handling, latching, queue pressure, cleanup,
abandonment, disconnect/rearm and malformed records. Python tests cover schema
errors, composed ownership, target capacity and exact export/recompile equality.
Physical latency, switches and external MIDI reception require qualification on
the pedal.
