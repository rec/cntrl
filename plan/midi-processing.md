# MIDI routing and processing

This is the proposed design for routing and filtering in feature-set items 6
and 7. A route reads MIDI, optionally processes it, and sends the result. A
complete system-wide DIN thru fits in two lines.

```toml
[midi]
thru = {from = 'din', to = 'trs'}
```

Put the same route on a named patch to hear it only while that patch is active.
For example, this patch layers a USB synth and an onboard output an octave
above either keyboard.

```toml
[patches.chorus.midi]
octave = {from = ['din', 'usb-host:keys'], via = [{transpose = 12}], to = ['trs', 'usb-host:synth']}
```

## Writing routes

Each key names one route. The compiler expands the following conventions.

| Field | Meaning |
| --- | --- |
| `from` | One input, or a list to merge. An endpoint alias reads that port. `@name` reads the processed result of another route in this scope. |
| `generate` | A generator with no input. Supply exactly one of `from` and `generate`. |
| `via` | Steps in playing order. Omit for thru. Each step has one processor name and its argument. |
| `to` | One output or a list for fan-out. Omit on a route consumed by another route. Diagnose an unused route with no output. |

Single endpoints need no list brackets. All channels and message types pass by
default. Ports are explicit so adding a cable cannot silently add a destination.
A shared route runs once regardless of its number of readers. References can
point forward. The compiler rejects empty endpoint lists, missing names,
duplicate list members, cycles and overlapping direct destinations within one
route. Distinct branches can intentionally deliver different versions to the
same port.

A split can share both the source and its filter. This setup sends a dry signal
to the synth and returns one delayed copy to the computer.

```toml
[midi]
keys = {from = 'din', via = [{keep = {channel = 1}}], to = 'usb-host:synth'}
echo = {from = '@keys', via = [{delay = '250 ms'}], to = 'usb-device:performance'}
clock = {generate = {clock = {bpm = 120}}, to = 'trs'}
```

Endpoint aliases come from the port registry and include DIN, TRS, MIDI-capable
omniports, USB host and device ports. Internal producers such as expression
assignments and patch messages can expose named sources. The compiler checks
direction and configured port role. A known alias may be physically absent.
Keep existing setups' forwarding defaults when no routing is authored. When
system routes are explicit they define the system matrix, including an empty
matrix. Compile existing forwarding policies and integrated internal producers
into the same delivery path so a direct writer cannot duplicate a routed event.

## System and patch lifetime

Every route, processor, generator and output uses the same grammar and behavior
in both scopes. System routes run across patch changes. Selecting a different
patch retires the previous patch's routes and starts the selected patch's routes
with fresh state. Selecting the already active patch leaves its streams running.
A momentary action invoking another patch's MIDI messages does not by itself
change the selected patch. Replacing the system configuration retires both
scopes before activating the replacement.

Derived patches inherit the parent's routes by name. A child definition with
the same name replaces that entire route. Resolve references after inheritance.
System and patch names are independent. To consume a system route from a patch
use `@system/name`. System routes cannot depend on an active patch. Reserve
the `system/` prefix in route names. A shared system clock or keyboard stream
therefore keeps running when its patch subscriber leaves.

For example, given the system routes above, a solo patch can add a harmony.
A derived solo patch can change the interval with one route replacement.

```toml
[patches.solo.midi]
harmony = {from = '@system/keys', via = [{transpose = 7}], to = 'usb-host:synth'}

[patches.high_solo]
parent = 'solo'

[patches.high_solo.midi]
harmony = {from = '@system/keys', via = [{transpose = 12}], to = 'usb-host:synth'}
```

For interactive muting, retain the proposed `midi-stream` operation with a
route reference and an explicit enabled flag. Routes start enabled. Repeating
the current gate setting has no effect. Disabling a route cancels work owned by
it and its contributions downstream. Other inputs to a merge continue.
Re-enabling admits future events. Patch-local gate settings end with the patch.

## Processor vocabulary

| Step | Behavior |
| --- | --- |
| `keep`, `drop` | Match by origin, type, channel or typed message field. Fields combine with AND. A list of alternatives within one field means OR. A note match governs the whole voice. |
| `transpose = 7`, `invert = 60` | Add semitones or reflect pitch around the given center using `2 * center - pitch`. Carry releases and polyphonic pressure with the mapped note. Suppress out-of-range voices. |
| `scale` | Specify a numeric field and an output range. The input defaults to that field's legal range. Clamp input to the selected interval, interpolate, round halves upwards and clip to the legal output range. Descending output ranges invert the response. A mapped onset velocity of zero suppresses its voice. |
| `map` | Replace matching typed fields or convert a message. Unmatched events pass. Each step sees the previous result. |
| `delay` | A duration delays the entire stream once. A table adds finite echoes, e.g. `{time = '250 ms', repeats = 3, feedback = 0.6}`. Defaults are one repeat and unity gain. Copy k is due at k times the interval and scales onset velocity by feedback to the power k. Both ends of each note move together. Mix a dry branch explicitly. |
| `reverse` | Use `{reverse = {}}` for a finite clip or supply `window = '2 s'` inside the argument for live input. |
| `generate` (route input) | Adapt the existing clock and MIDI-file players. A generator declares its clock, finite-clip capability and bounded overdue behavior. |

For example, remap volume to expression, reverse its response, and send channel
1 program selections as controller values. Program numbers are authored as
1 through 128. The offset produces the corresponding 0 through 127 CC value.

```toml
[midi.controls]
from = 'din'
to = 'trs'
via = [
    {map = {match = {type = 'control-change', controller = 7}, set = {controller = 11}}},
    {scale = {match = {controller = 11}, field = 'value', output = [127, 0]}},
    {map = {match = {type = 'program-change', channel = 1}, set = {type = 'control-change', controller = 11, value = {from = 'program', offset = -1}}}},
]
```

A field predicate requires the field to exist. An omitted match applies to all
eligible events. Preserve applicable fields such as channel on conversion.
Reject unknown fields, incomplete destination messages and mappings that can
create a Note On without a paired release. Use existing typed MIDI messages and
duration units. Pass unrelated messages unchanged through numeric transforms.

Reverse delegates to the existing sequence reversal once its ongoing fixes
are verified. Mirror note intervals about clip duration, using original onset
velocities. Live reverse captures one bounded window while playing the previous
one, adding one window of latency. Split held notes at boundaries and reopen
with the original velocity. Treat SysEx as a complete byte-ordered frame in
the window where it finishes. Order simultaneous releases and retriggers so
each onset precedes its own release, including zero-duration notes. Reject
unbounded live reverse. Apply timing steps to all selected events, including
clock. Keep a single clock owner per output.

## Disconnect, reconnect and overload

Use the settled USB provider's stable alias, capability, presence and connection
generation. A reconnect can change enumeration order. An ambiguous or unknown
physical device cannot claim a saved alias until the provider resolves it. Each
observable disconnect invalidates that connection generation. Tag pending work
with its source, destination generation and route ownership.

| Event | Required behavior |
| --- | --- |
| Input disappears | Discard its partial parser state, running status, unfinished SysEx, capture and scheduled contributions. Release its delivered notes and hold-pedal claims on reachable outputs. Other inputs on the same route continue. |
| Output disappears | Invalidate its queued writes, partial SysEx and cached delivered state. A physical release cannot be confirmed over an absent cable. Healthy outputs keep playing. |
| Same device returns | Rebind the saved alias to the new generation. Discard old performance traffic. Where endpoint identity is confirmed, finish recorded cleanup of potentially sounding notes and sustain before admitting new traffic on the affected channels. Resume from new events without replaying old notes, CCs, clock ticks or file backlogs. |
| Replacement device appears | Bind according to the provider's identity policy. Start a replacement with clean local state. Never send the previous device's pending cleanup to it. |
| Cable flaps during recovery | Keep a bounded set of unresolved releases per stable destination, channel and pitch plus hold state. Retry only outstanding cleanup after identity is confirmed. Never append a queue per disconnect. Repeated notifications for the same generation are harmless. |
| Device never reports cable state | DIN and passive TRS cannot promise USB-style disconnect detection. Use observable resets and explicit panic for recovery. |

Cleanup gets reserved scheduler capacity. If a reachable output blocks, keep
affected channels gated until cleanup succeeds. Ordinary cleanup targets the
notes and hold states owned by the departing branch. Channel-wide panic is an
explicit operation. Expose absent, recovering, overloaded and ready states plus
drop counts through existing diagnostics. A permanently missing output consumes
bounded bookkeeping and cannot stall another output or the switch scan.

## Runtime and extension contract

Decode once per input connection. Carry a typed message, monotonic timestamp,
stable arrival order and source identity through the graph. Merge ready events
by due time, then declared input order and arrival order, without waiting for
silent inputs. Preserve ordered transactions and the scheduler's real-time
interleaving rules. Bound SysEx storage and abort incomplete transmissions.

Normalize zero-velocity Note On to release. Pair repeated input keys FIFO per
source, channel and pitch. Track sparse ownership for admitted voices, freezing
pitch, destinations and delay copies at onset. For transparent thru, preserve
repeated onsets and velocities. Where distinct owners collide on the same
destination key, use a shared gate from the first owner's onset through the last
owner's release. MIDI 1.0 cannot give those owners independent articulation on
one channel. Track hold ownership similarly. Explicit resets invalidate affected
ownership. Keep bookkeeping for suppressed voices so their releases cannot end
a later note.

Physical writes use the existing scheduler and `midi::Sink::submit`. Update
delivered `MidiState` only on successful delivery. Reserve release capacity
before admitting onsets. Service graph work, generators, SysEx and scheduled
events with byte, event and time budgets. Account for buffers, voices, delayed
copies and capture storage in target memory estimates. On exhaustion refuse new
work on the affected branch. If pairing can no longer be tracked, suspend and
clean up that branch, reporting the reason. Preserve other branches' progress
and due switch scans.

Register built-ins and future statically linked plug-ins by name. Each declares
a host config model, compiled parameter version, input or generator capability
and bounded storage needs. The target implements `push(event, emit)`,
`service(now, budget, emit)` and `stop(reason, cleanup)`. Emit through the
engine so extensions share timing, backpressure and lifecycle rules. A rejected
event has no committed effects. A processor that partially emitted retains
bounded progress and consumes the input exactly once. Reject unknown names or
unsupported versions at compile/load, before replacing the active configuration.
A runtime processor failure retires its affected contributions and reports the
scope and route.

## Implementation and acceptance

1. Compile both scopes to indexed routes using one schema. Round-trip the
   examples through compile, export, setup bundles and per-setup history. Check
   inheritance, cross-scope references, cycles, invalid port roles and budgets.
2. Replace fixed forwarding with the graph and existing delivery scheduler.
   Test byte-for-byte thru including repeated notes, stable merges, independent
   fan-out, SysEx and clock ownership. Verify setup defaults and explicit routes
   cannot accidentally double-send.
3. Implement scoped cleanup before shipping transforms. Test switching patches
   with sustained notes, two sources holding one pitch, a shared generator and
   a busy output. Exercise each transform, remapped releases and overload.
4. Add delay, generators and sequence reversal. Use a virtual clock to check
   latency, durations, onset velocities, timing ties, cancellation and bounded
   work after a service stall. Register a test processor without editing dispatch.
5. Exercise hundreds of unplug/replug cycles during held notes, sustain, SysEx,
   delay and queued cleanup. Alternate port order and device identity. Assert
   healthy branches keep time, stale generations never write, pending cleanup
   stays bounded and resources return to baseline after recovery. Verify scan
   gaps, clock jitter and actual reconnection separately on hardware.

## Additional work beyond the prompt

None.
