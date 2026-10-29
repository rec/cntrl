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
on = { type = 'midi', output = 'din', bytes = [0xFA] }
```

`press` is the default mode and requires only `on`; it forbids `off`. Each new
press invokes the Patch once. A momentary switch requests On when pressed and
Off when released. A latched switch alternates states on successive presses.
Both persistent modes require an explicit `off` Patch. On and Off may have
different Patch types, outputs and channels.

Each branch accepts the common [Patch representation](patches.md), including
inline program selections, sequences and stable references such as
`{ type = 'reference', patch = 'clean' }`. Program selections use instrument
aliases; other MIDI variants name their output directly. Definitions are keyed
by `patches.clean`, independently of their display labels.

Persistent assignments reserve the controls their reachable patches affect.
Raw MIDI and SysEx reserve their entire destination output until Off cleanup or
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

Live On/Off state is never persisted. This configuration uses setup format 18
and the tagged authoring schema. Older setups need conversion and recompilation;
there is no legacy firmware decoder. See [Patch authoring](patches.md) for the
complete schema and required authoring changes. Exports after pedal program edits
canonicalize synthesizer-name metadata while preserving MIDI selections and the
visible labels; recompiling that canonical export is stable.

Native sanitizer tests cover edge handling, latching, queue pressure, cleanup,
abandonment, disconnect/rearm and malformed records. Python tests cover schema
errors, composed ownership, target capacity and exact export/recompile equality.
Physical latency, switches and external MIDI reception require qualification on
the pedal.
