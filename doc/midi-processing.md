# MIDI routes

A route merges its inputs, applies optional processing in written order, and
sends the result to its outputs. Inputs and outputs use saved endpoint aliases
from the target's endpoint registry. Examples below assume those aliases have
been declared. A disconnected endpoint keeps its alias.

```toml
[midi]
thru = {from = 'din', to = 'usb-host/synth'}
```

Use a list for multiple inputs or outputs. All message types and channels pass
unless filtered. A route name refers to another route's processed output in the
same scope. Route names cannot contain `/` or collide with endpoint aliases.

```toml
[midi]
keys = {from = ['din', 'usb-host/keys'], to = 'usb-host/synth'}
echo = {from = 'keys', via = [{delay = '250 ms'}], to = 'trs'}
```

An explicit system `midi` table defines system forwarding. An empty table
turns system forwarding off. Omitting it keeps the setup's existing forwarding
policy. Patch routes run in addition to the system routes.

```toml
[patches.solo.midi]
harmony = {
    from = 'system/keys',
    via = [{transpose = 7}, {delay = '250 ms'}],
    to = 'usb-host/synth',
}
```

Selecting the patch starts its routes. Leaving it cancels pending copies and
releases its owned notes and hold controls. Selecting the same patch preserves
its running state. Momentary operations cannot select a new routing context.
Derived patches inherit routes by name. A child route replaces the parent's
whole route with that name.
Song patches resolve `system/name` against the active setup's system routes.
A missing dependency prevents the new patch routing from replacing the current
one. Routes that share an output channel and pitch share note ownership.
Use separate channels for layers that need independent articulation.

The `via` list supports `keep`, `drop`, `transpose`, `invert`, `scale`, `map`,
`delay` and `reverse`. Channels are 1 through 16 and program numbers are 1
through 128. Numeric field predicates require that field to exist. Conditions
on different fields combine with AND, and alternatives within one field combine
with OR.

```toml
[midi.controls]
from = 'din'
to = 'trs'
via = [
    {keep = {type = 'control-change', channel = [1, 2]}},
    {map = {match = {controller = 7}, set = {controller = 11}}},
    {scale = {match = {controller = 11}, field = 'value', output = [127, 0]}},
]
```

Delay has no dry signal. A duration delays once. A table can specify finite
repeats and feedback. Both ends of each note receive the same delay.
Live reverse requires a window and adds one window of latency. Held notes are
split at window boundaries. Finite file playback preserves original onset
velocities during reversal.

A generator replaces `from` with `generate`.

```toml
[midi]
clock = {generate = {clock = {bpm = 120}}, to = 'trs'}
backing = {generate = {file = {filename = 'intro.mid'}}, to = 'usb-host/synth'}
```

File generators use the existing fixed MIDI recording directory and playback
settings. An empty filename selects the latest completed recording.

Use a patch operation to mute or enable a route. A qualified name addresses a
system route. A bare name addresses the selected patch's route.

```toml
[patches.mute_echo]
operation = 'midi-stream'
stream = 'system/echo'
enabled = false
```

Queues, voices and capture buffers are bounded. Overload stops and cleans up
the affected branch or destination. Reconnection discards stale performance
traffic. A confirmed returning device receives pending cleanup before new
traffic, while a replacement starts with fresh local state. Stable identity
requires support from the endpoint
provider. Passive DIN and TRS cables have no reliable disconnect notification.
Use the existing panic operation when the physical receiver needs a reset.
The initial limits are 64 routes per compiled document, 16 processing steps per
route, 256 input notes, 256 output voices and 512 queued events. Setup memory
planning uses a conservative pool estimate while runtime state grows sparsely.

The existing serial command channel accepts `CNTRL MIDI STREAMS`. Its reply
reports the selected scope, queued events, voices, pending cleanup records,
successful writes, drops, overloads and processor failures, in that order.

Compiled extensions register a configuration model and encoder on the host and
a matching processor factory in the firmware. The same registration can declare
a generator. Setups requiring a missing extension or mismatched parameter
version fail validation before activation.
