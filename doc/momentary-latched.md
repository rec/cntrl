# Switch assignments

Switches is an optional module for press, momentary and latched Patch assignments.
Open it from the module menu. Opening the page sends no MIDI. Indicators use
the shared known MIDI state, including writes made before opening the page.
Held entry switches must be released before they activate an assignment.

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
`'clean'`. Program selections use instrument
aliases; other MIDI variants name their output directly. Definitions are keyed
by `patches.clean`, independently of their display labels.

## Shared parameter state

The output scheduler records submitted CC values, Pitch Bend, program selections
and NRPN parameter values by destination and MIDI channel. Assignments referring
to the same controls share that information, including named references,
derived patches and sequences. Names and button labels do not define identity.
Program bank selectors are captured when the Program Change is submitted.

For example, if A and B both use CC 7 value 127 for `on` and value 0 for `off`,
turning it on through A makes both indicators show On. Pressing a latched B
then requests value 0. A separate toggle group is unnecessary. A program
selection updates every matching program indicator on that output and channel.

An indicator becoming On through a shared value does not acquire an additional
Off cleanup obligation. Explicit Off cleanup belongs to assignments that
actually ran their On action during this visit.

A compound patch matches when all its specified final parameter values match.
Repeated writes to a parameter use the last value in the sequence. The page
shows On for an `on` match, Off for an `off` match, Partial match when some
specified values match, Other values when known values differ, and Unknown
remote state when values are unavailable. Pending work has its own indication.
Latched presses request Off when their On values match and otherwise request On.

These values record local submissions. Instrument controls and traffic from
another controller can change an instrument without cntrl observing it. Incoming
messages forwarded through cntrl update the destination state when submitted.
There is no assumed connection between an input message and an instrument's
feedback protocol. Disconnecting a detectable output invalidates its state.
Physical MIDI plugs without connection detection require explicit user care.

Program Changes invalidate previously known channel parameters because loading
a program may change them. Subsequent CCs in the same patch establish the
specified values again. SysEx and System Reset invalidate destination state.
Reset Controllers invalidates affected controller state, and RPN/NRPN
increment/decrement invalidates inferred parameter values.
Cancelled pending output loses its known state. Reconnection establishes no
values until fresh messages are submitted.

Notes, transport, application operations and arbitrary SysEx have no inferred
active-value state. Assignments containing those operations retain local
execution status. Persistent assignments using opaque MIDI operations continue
to reserve the destination. Known parameter controls may be shared. Runtime
state is retained across page and setup changes while the same outputs remain
available, and is lost on restart.

## Enter and exit hooks

Hooks follow assignment selection. Selecting a different assignment runs the
previous assignment's `exit`, then the new assignment's `enter.action`, then
the ordinary selecting action. Repeated actions on the same assignment use
the ordinary action. The first selection after opening the page runs enter.
Selecting an alias is a new selection even when its parameter values already
match another assignment.

```toml
[switches.lead]
button = 'A'
on = 'lead'
enter = { action = 'prepare-lead', override = true }
exit = 'finish-lead'
```

`enter` and `exit` are optional. `enter.override` is a boolean, defaulting to
false. When true, the selecting action is suppressed on that first selection.
Further actions on the same assignment run normally. Each hook accepts the
common Patch representation, including sequences and references. Exit has no
override option.

Presses and recognized gesture actions select assignments. A momentary release
requests its Off cleanup without changing selection. Hook execution is
serialized with accepted assignment work. A blocked exit delays the next enter
and action. A failed exit or enter cancels the pending selection queue and
reports the existing Failed / unknown or Unknown remote state indication.
Hooks cannot leave the switch context or trigger selection recursively.

The selection queue holds at most 16 actions and reports saturation. Held
repeats coalesce while selection work is pending. F cancels future selection
actions, finishes accepted work and explicit Off cleanup, then runs the selected
assignment's exit once. F again abandons blocked work. Closing during an enter
that has begun also runs that assignment's exit after accepted work completes.
Selection is reset each time the page is opened.

## Shared shift layer

`{ operation = 'shift' }` enables one shared layer for the current Switches
visit; `{ operation = 'unshift' }` disables it. Both are idempotent: repeated
shifts do not nest or require matching unshifts. They are switch-context
operations and can appear in switch actions, gestures, hooks, sequences and
references. They do not invoke actions or change assignment selection by
themselves, so indirect shifts do not recursively trigger hooks.

An assignment can supply a complete alternate action set under `shifted`:

```toml
[switches.shift]
button = 'A'
mode = 'momentary'
on = { operation = 'shift' }
off = { operation = 'unshift' }

[switches.sustain]
button = 'B'
mode = 'momentary'
on = { controller = 64, value = 127, output = 'din', channel = 1 }
off = { controller = 64, value = 0, output = 'din', channel = 1 }

[switches.sustain.shifted]
on = { controller = 66, value = 127, output = 'din', channel = 1 }
off = { controller = 66, value = 0, output = 'din', channel = 1 }
```

Assignments without `shifted` retain their normal actions. The alternate set
uses the assignment's existing mode and accepts `on`, `off` and `gestures`.
The same mode rules apply: press forbids Off, persistent modes require Off,
and gestures require press mode. Alternate actions cannot leave Switches.
Names, styles and selection hooks belong to the assignment and are shared by
both layers.

The layer is captured on press. Momentary and gesture releases stay paired
with that layer even if Shift is released first. Accepted actions retain their
chosen layer while queued. A repeated activation uses the currently requested
layer. When changing the layer of a latched assignment that owns an active On,
its old Off finishes before the new layer is activated; shared MIDI matches
alone do not acquire cleanup obligations. Backpressure delays this transition
without dropping its queued action. Changing shift cancels unrecognized
gestures on released assignments with alternate actions; held gestures retain
their original recognition and release actions.

The footer displays `Shifted` while the shared layer is enabled. Assignment
indicators continue to describe their last executed layer and shared known
MIDI state, not acknowledgement from an instrument. Shifting does not itself
send MIDI, rerun enter/exit hooks or restore previous parameter values.
Disconnects retain the existing unknown-state and rearm rules. Closing cancels
queued actions, resets shift, and finishes accepted work and cleanup using
each assignment's executing layer. Abandonment and reopening also reset shift;
it is never persisted across navigation, setup changes or restart.

## Programmable gestures

A `press` assignment can include a `gestures` table. `on` is its single-press
patch. The eight optional gesture patches accept the same Patch representation.

```toml
[switches.lead]
button = 'A'
on = 'clean'

[switches.lead.gestures]
release = 'clean-release'
long_press = 'solo'
long_release = 'solo-release'
double_tap = 'boost'
double_release = 'boost-release'
long_double_tap = 'sustain-boost'
long_double_release = 'sustain-boost-release'
repeat = 'step'
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

With `long_double_tap`, the second press waits for the same `long_delay` used
by a first-press hold. Releasing it before that threshold runs `double_tap`
followed by `double_release`. Holding through the threshold runs
`long_double_tap`, and releasing afterward runs `long_double_release`.
The held second press excludes ordinary double actions, first-press long
actions and repeats. An ordinary double tap therefore runs on its second
release when this hold gesture is configured. `long_double_release` requires
`long_double_tap`, which can also be used without an ordinary `double_tap`.
Cancelling an undecided second press emits nothing. Cancelling a recognized
hold runs its matching release action.

These gesture slots use compiled setup format 40. Rebuild older compiled
setups from their TOML sources before using them with this firmware.

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

Persistent assignments identify the controls their reachable patches affect.
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

The page also displays Pending, Completed locally and Failed / unknown.
These describe local execution and scheduling;
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
