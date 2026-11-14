# MIDI PC/CC scroll counters

Named counters let several switch actions share a MIDI program number or CC
value. Define the destination, initial value, limits, signed step and boundary
behavior once, then reference the counter from `scroll-counter` patches.

```toml
[counters.volume]
type = 'control-change'
output = 'din'
channel = 1
controller = 7
initial = 90
minimum = 0
maximum = 127
step = 5
boundary = 'bounce'

[switches.louder]
button = 'A'
on = {operation = 'scroll-counter', counter = 'volume'}

[switches.quieter]
button = 'B'
on = {operation = 'scroll-counter', counter = 'volume', direction = 'backward'}

[patch_views.switches]
fields = ['switch.name', 'counters.volume', 'switch.state']
```

CC counters use values 0–127 and require `controller`. Program counters use
1–128, matching other cntrl program changes, and omit `controller`. Every
counter requires `output`, `channel`, `initial`, `minimum` and `maximum`.
The initial value must be within the limits. `step` defaults to 1 and accepts
nonzero integers from -32767 through 32767. A negative step reverses the normal
travel direction. `direction` defaults to `'forward'`; `'backward'` reverses
the current travel direction for that action.

`boundary` has two behaviors:

- `'wrap'` (the default) jumps to the opposite limit when the next step would
  cross a boundary. With limits 0–127, 125 plus 5 becomes 0, and 2 minus 5
  becomes 127. Excess distance is discarded.
- `'bounce'` reaches the endpoint and reverses the shared travel direction.
  With limits 0–127 and step 5, 125 becomes 127, then 122 on the next forward
  action. Starting at an endpoint moves inward on the next step. A counter
  with identical minimum and maximum always sends that one value.

The first accepted action sends `initial`, regardless of direction. Later
actions step from the last accepted value. All assignments referencing a
counter share its value, first-send state and bounce direction. Loading or
changing the active setup resets all counter values and directions; opening
the switch page or changing performance pages does not. Initial values are
displayed immediately, but loading a setup sends no counter MIDI messages.

Add `counters.<name>` to a patch view to display the live value. The same field
can appear on several views, including switch views. Displayed values come
from the state used to construct the MIDI message. Program values are shown
as 1–128 even though their MIDI wire bytes are 0–127.

A value and bounce direction advance only after the existing output scheduler
accepts the complete message. While its queue is blocked, an action waits and
does not advance repeatedly. An unavailable output fails the action without
advancing. Cancellation before acceptance also leaves the counter unchanged.
Once accepted, the value remains advanced even if delivery is later cancelled
or the destination disconnects. Scheduler acceptance is local delivery state,
not acknowledgement from the instrument. No stale counter messages are
replayed automatically after reconnection.

Counters use the common patch executor, so named references, sequences,
gestures and patch-local bindings can invoke the same actions. References to
another setup keep that setup's counters separate; inlined references carry
their definitions into the compiled setup. They share values within the
current setup session and reset along with the active setup.

For independent stepping and destinations, use a `counter-message` patch:

```toml
type = 'counter-message'
counter = 'programs'
output = 'din'
channel = 1
change = 'increase'
wrap = true
```

The counter supplies its message kind, CC controller, initial value and bounds.
The action supplies the destination and channel. `change` is `'none'` (send
the current value), `'increase'` or `'decrease'` by one. Unlike `scroll-counter`,
these changes apply before the first send too. `wrap = false` clamps at a bound.
Only accepted messages commit the new value. Actions can therefore advance once
and send the shared value on other channels or outputs. A binding name can use
`{counters.programs}` to display the live authoring value.

Python tests cover authoring validation, compilation and export. Native tests
cover transmitted values, shared state and displays, endpoint behavior,
backpressure, cancellation and setup reset. Physical MIDI timing and receiver
behavior require hardware qualification.
