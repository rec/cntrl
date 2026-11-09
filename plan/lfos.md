# LFOs for MIDI values and program selection

## Objective and scope

Expand the former feature-set item 11 from CC-only waveforms to low-frequency
modulation of continuous MIDI values and stepped Program Change selection.
Support repeating and one-shot operation, with defined activation, running and
stopping behavior. This is a proposed design
and implementation plan, not an instruction to implement or flash firmware.

Use the existing [system/patch MIDI routes](../doc/midi-processing.md), generator
registration, stream gates, output scheduler and delivered parameter state. Keep
one generator path rather than adding an independent LFO engine to patch
execution or the target adapter. System LFOs can continue across patch changes;
patch LFOs follow patch lifetime.

## Output types and resolution

An LFO owns one typed parameter and can send it to the route's declared outputs.
Use the existing message names and channel numbering. A parameter descriptor
contains the message's addressing fields; the LFO supplies its changing value.

| Parameter | Addressing | Authored value range |
| --- | --- | --- |
| Ordinary CC | Channel and controller; resolution 7 | 0..127 |
| Fine CC pair | Channel and MSB controller 0..31; resolution 14; LSB controller is MSB + 32 | 0..16383 |
| Pitch bend | Channel | -8192..8191, with 0 neutral, as in existing patches |
| Channel pressure | Channel | 0..127 |
| Polyphonic key pressure | Channel and fixed note 0..127 | 0..127 |
| NRPN | Channel and parameter 0..16383; resolution 7 or 14 | 0..127 or 0..16383 |
| Registered channel fine tuning | Channel, RPN 1 and resolution 14 | 0..16383, with 8192 neutral |
| Program Change | Channel; numbered programs in the current bank | 1..128, encoded as 0..127 |

Program Change is a discrete output: quantize the waveform to a program number
and send only changes. Normal sampling/coalescing rules apply, so intermediate
program numbers may be skipped. Use the existing numbered `program-change`
encoding, including its one-based authored numbering. The LFO sends no bank
selection. Hold, explicit final program and restoration use the same lifecycle
policies as other outputs. Restoration requires a known previous program and
unchanged bank context; a bank change invalidates that restoration ownership.

Continuous outputs use numeric receiver parameters. A CC address does not prove
that its function is continuous. Reject bank selection, channel-mode messages,
parameter selectors, data entry/increment/decrement, portamento note selection
and the high-resolution velocity prefix as standalone LFO destinations.
Parameter-selection/data-entry bytes belong to complete typed transactions.
Sustain and other switch controls are outside this feature. An ordinary CC may
address its fine byte alone at 7-bit resolution, but that is distinct from
modulating the full pair.

NRPN ranges must match the intended instrument parameter; cntrl cannot infer
whether a manufacturer-defined parameter is continuous. Registered fine tuning
has defined continuous semantics. Do not treat every RPN as a linear 14-bit
number: pitch-bend sensitivity, for example, has separate semitone/cents fields.
Other registered parameters require their own typed semantics before inclusion.

Pressure modulation does not create notes. Polyphonic pressure addresses the
configured note; it neither allocates voices nor follows arbitrary held notes.
Pitch bend uses the receiver's configured bend range; the LFO does not silently
change that range. Note velocity, song position, transport, arbitrary SysEx
templates and MIDI 2.0 support are outside this item.

Wire-format references: the MIDI Association's
[CC and RPN table](https://midi.org/midi-1-0-control-change-messages) and
[message summary](https://midi.org/summary-of-midi-1-0-messages).
The message formats come from those references; lifecycle and waveform choices
below are proposed cntrl behavior.

## Configuration and control

Register `lfo` in the existing route `generate` table. Do not add a second
top-level LFO collection or a separate start/stop patch operation. Use the
existing `midi-stream` operation with an explicit `enabled` value for gates.
Existing scope, naming, inheritance and route-reference rules apply.

Proposed syntax, not currently accepted configuration:

```toml
[patches.vibrato.midi.bend]
to = 'din'
generate = {lfo = {
    target = {type = 'pitch-bend', channel = 1},
    shape = 'sine',
    range = [-512, 512],
    period = '200 ms',
    phase = 0,
    phase_mode = 'retrigger',
    run = 'repeat',
    update_interval = '10 ms',
    off = {action = 'value', value = 0},
}}

[midi.filter]
to = 'usb-host/synth'
generate = {lfo = {
    target = {type = 'control-change', channel = 1, controller = 1, resolution = 14},
    shape = 'trapezoid',
    range = [2048, 14335],
    period = {beats = '4'},
    off = {action = 'hold'},
}}

[patches.stop_filter]
operation = 'midi-stream'
stream = 'system/filter'
enabled = false
```

Require target, shape, range, period and an explicit `off` policy. The other
defaults shown above are phase 0, retrigger, repeat and a 10 ms update interval.
Range contains two endpoints in the parameter's native resolution. Descending
endpoints invert the shape; equal endpoints produce a constant value without
repeated duplicate sends. Off values may lie outside the waveform range but
must remain legal for the parameter.

Use one `period` field: a positive duration uses the existing units parser;
`{beats = '1/2'}` means half a quarter-note beat, parsed as an exact positive
rational. Beat timing follows the controller's selected internal/external clock,
not an unrelated route-local clock generator. `run` is `repeat` or `once`.
`phase` is a fraction of a cycle in [0, 1). `phase_mode` is `retrigger` or `free`.
Random shapes additionally accept a reproducible unsigned 32-bit seed, default 0.

Keep configuration validation in frozen Pydantic models and compiled validation
in the existing document/generator path. Reject unsupported targets, selectors,
resolution combinations, malformed ranges and nonpositive timing before use.
Choose and document practical timing/capacity bounds from measured scheduler
costs rather than treating every requested update rate as achievable.

## Shapes

Evaluate a normalized value in [0, 1], then map it to the authored range. Round
once to the nearest native integer, with ties toward positive infinity, and
clamp to its legal bounds. Never pass
14-bit destinations through a 7-bit intermediate. Equal shape values must
produce equal output values on every host and the device.

| Shape | One cycle |
| --- | --- |
| `sine` | Smooth oscillation; phase 0 is the midpoint rising toward the maximum. |
| `triangle` | Linear rise from minimum to maximum over half a cycle, then linear fall. |
| `saw-up` | Linear minimum-to-maximum ramp, then wrap to minimum. |
| `saw-down` | Linear maximum-to-minimum ramp, then wrap to maximum. |
| `square` | Minimum for the first half, maximum for the second half. |
| `random` | Sample and hold: choose one value per cycle and hold it until the next. |
| `trapezoid` | Rise, high plateau, fall, low plateau, each occupying one quarter-cycle. |
| `smooth-random` | Join successive random cycle values with smoothstep interpolation, with zero slope at each boundary. |

Trapezoid and smooth random are the two additions to item 11's original shapes.
Trapezoid gives sustained high/low sections without square-wave jumps. Smooth
random gives drifting modulation without sample-and-hold steps. Keep these
initial shapes fixed rather than adding arbitrary breakpoint/envelope editing.

Specify deterministic arithmetic or lookup tables and a fixed PRNG algorithm
before writing fixtures. Random values depend on seed and cycle index, so a
long service gap does not require iterating through every missed cycle. A
retrigger repeats the same seeded pattern; free mode follows the current cycle.

## Being turned on

Routes start enabled under the existing rules: a system LFO starts with its
setup and a patch LFO starts when that patch is selected. Selecting the same
patch or repeatedly setting an already enabled gate does not restart it.
Momentary/latched switches use their existing explicit On/Off patches to set
the stream gate; no separate toggle semantics are introduced.

In retrigger mode a fresh enable starts at the authored phase. In free mode the
phase clock is anchored at scope activation and keeps advancing while gated
off; enabling joins its current phase. Leaving a patch destroys its scope, so
later selecting that patch creates a fresh anchor. System scope survives an
ordinary patch change. One-shot mode uses retrigger; reject free one-shots.

Preflight ownership and any restoration snapshots before the first write. An
unknown requested restore value fails activation with a route diagnostic;
there is no guessed baseline. Capture each participating destination's last
accepted value, not an assumed instrument state. Fine pairs require a complete,
known value. Do not partially start a fan-out after failing this preflight.

Request the initial waveform value immediately through the scheduler. This is
an initial sample, not an implicit ramp or fade. If delivery is blocked, retain
the first sample until accepted; subsequent ordinary samples then use current
phase. Show pending activation separately from successful output delivery.
Unavailable outputs do not join the activation silently when they later appear.

## Continuous running

Compute phase from elapsed time or musical beat position, not from the number
of polls or successful writes. Tempo changes alter the rate without a phase
jump. Independent-duration LFOs ignore MIDI transport. For beat-timed LFOs,
Clock Stop pauses phase and holds the last value; Continue resumes it; Start
restarts at the authored phase. Loss of external clock pauses beat-timed LFOs
and reports waiting, with no invented fallback tempo or catch-up burst.

The update interval is the earliest ordinary sample spacing, not a promise of
wire throughput. Suppress unchanged quantized values per destination. After a
service stall, compute the current value and skip obsolete samples. A slow
output cannot hold back phase or healthy destinations. Coalesce pending
ordinary values before transmission; never accumulate an LFO event backlog.

One-shot operation runs one complete period from the selected phase, requests
its terminal sample and then applies the off policy. The terminal sample is
the left-hand limit at starting phase plus one cycle: a phase-0 rising saw ends
at its maximum, before the wrap to minimum. Do not start another cycle. Keep
initial, terminal and final-off writes ordered, even if a poll passes several
boundaries. Completion sets the gate off, so the next explicit enable starts a
new one-shot. A blocked output can delay delivery; it must not restart or extend
the oscillator. Clock Start resets enabled beat-timed runs, not completed or
explicitly gated-off runs.

## Being turned off

Off immediately cancels future generation and removes unsent ordinary samples,
including delayed/processed contributions owned by that route. Complete an
already started parameter transaction before its final action. Off is
idempotent and does not queue another final value on each request.

| Explicit off policy | Result |
| --- | --- |
| `{action = 'hold'}` | Leave the last fully delivered value; send no final value. |
| `{action = 'value', value = ...}` | Send the chosen legal value once, such as neutral pitch bend or zero pressure. |
| `{action = 'restore'}` | Restore the destination's captured pre-LFO value once. |

Off values and restore snapshots use the final destination parameter's native
units, after ordinary value processing. Final actions bypass waveform processors
and their delays through the existing cleanup path, so neutral pitch bend stays
neutral and restoration does not require inverting a scale/map. Resolve final
parameter identities during compilation; reject processing combinations whose
final addresses, types or cleanup values cannot be determined consistently.

The same policy applies to an explicit gate off, one-shot completion, leaving
the owning patch, setup replacement and orderly generator replacement. Stop
has no hidden zero/reset policy and does not send channel-wide panic messages.
An enable received during stopping waits for the old final transaction before
starting a fresh run; it cannot supersede required cleanup.

Keep bounded final-write records in the existing cleanup machinery if an
output blocks. Report stopping until the final write completes, without
blocking other outputs or input scans. A disconnected destination's ordinary
modulation ends; any final action is tied to its saved identity/generation
recovery rules. Never send that action to a replacement device. Reconnection
does not replay an LFO backlog or silently re-enable its participation; require
a fresh off/on activation after applicable cleanup.

## Ownership, transactions and scheduling

Use one active LFO writer per destination/channel/parameter. Treat the coarse
and fine bytes of a 14-bit CC as one owned parameter and detect overlapping
7-bit writers. Reject simultaneous authored LFO collisions, including system
and patch scopes. Explicit patch/manual replacement retires the old LFO writer
before taking ownership; old off/restoration work cannot overwrite the newer
writer. Apply existing teardown/panic/handoff ordering so no old sample or
cleanup can run after that ownership has been invalidated.

A fine CC sample contains the MSB then LSB of the same value. NRPN/RPN samples
contain their full parameter selection and required data-entry bytes; terminate
parameter selection with RPN Null. A 7-bit NRPN uses Data Entry MSB only.
Do not cache a selected parameter across arbitrary writers. Reserve a complete
sample before emitting its first byte, then retain bounded progress across
backpressure. Permit MIDI real-time interleaving but not unrelated writes inside
that parameter transaction. While the connection remains usable, finish an
in-flight transaction before cancellation cleanup. A disconnect can leave the
receiver with a partial value; software cannot undo bytes already received.
Invalidate partial logical state and recover with complete transactions for the
confirmed identity. Never continue a fine pair or parameter write across a
connection-generation change or redirect data entry to another parameter.

Preserve logical parameter/transaction identity through route fan-out and
supported processing. Scale/map full values before encoding; never transform
coarse/fine bytes independently. Validate processor/target combinations that
would break those semantics. This requires reviewing the existing packet-based
generator boundary and extending its shared encoding/scheduling contract where
needed, not building a private LFO byte writer or bypassing route cleanup.

At final delivery keep at most one coalesced ordinary value, one in-flight
transaction and one final action per active destination. Initial and one-shot
terminal samples use bounded lifecycle slots. Any upstream delay/reverse storage
remains within the existing processor budgets. Account for these records,
restoration state, shape tables and generator instances in target memory
planning. Reuse existing work budgets and input/clock priority; do not block or
allocate on each sample.

## Implementation sequence

1. Settle the proposed schema and lifecycle above. Trace current route teardown,
   `MidiState`, generator service and scheduler ownership. Specify the shared
   full-resolution transaction changes before implementation; retain existing
   patch encoders and lifecycle behavior for unrelated messages.
2. Add only the typed fine-CC/NRPN-resolution/RPN-fine-tuning support required
   here. Update host encoding, native decoding, known-value state and ownership
   together. Reuse the same encoding for ordinary patches where applicable.
3. Implement and test phase/value evaluation independently of hardware, then
   register the LFO generator in the existing engine. Add gate, one-shot,
   clock, restoration and bounded final-write state through that path.
4. Extend compilation, validation, export/recompile, bundles and per-setup
   history together. Give new compiled generator parameters an explicit
   supported version. Do not introduce a compatibility reader or format
   migration without a demonstrated requirement and separate approval.
5. Add existing-page/stream diagnostics for running, waiting, stopping, missing
   outputs and skipped/coalesced updates. Document usage, resolution, phase,
   off policies and actual measured limits. Do not add a separate LFO editor.
6. Run repository checks and the manually dispatched cross-platform matrix at
   a coherent checkpoint. Build the target and record memory/headroom changes.
   Measure physical output separately before claiming timing qualification.

## Acceptance

- Fixed fixtures cover every shape at boundaries and interior points, inverted
  ranges, native-resolution rounding, seeded restart and large time jumps.
- Compile/export/history round trips retain every destination and lifecycle
  setting; invalid fine pairs, special CCs and RPN semantics fail clearly.
- Capture exact bytes for 7/14-bit CC, signed pitch bend, both pressures,
  7/14-bit NRPN and registered fine tuning, including parameter nulling. Check
  Program Change quantization, authored 1/128 to wire 0/127 boundaries,
  duplicate suppression, skipped intermediate programs and final/restore
  behavior, including invalidation after a bank change.
- Exercise initial activation, repeated enable/off, re-enable while stopping,
  patch reselection/change, setup replacement, one-shot end, explicit final
  values, restore success and unknown restore rejection.
- Virtual-clock tests cover long runs, timer rollover, phase offsets,
  free/retrigger gating, tempo changes, Start/Continue/Stop and external-clock
  loss. Quantization cannot introduce phase drift or duplicate-send floods.
- Block and disconnect outputs at each transaction boundary and midway through
  fine/parameter writes. Check bounded memory, no mixed samples or stale writes,
  independent healthy outputs, cleanup ownership and replacement-device safety.
- Check competition with expression, counters, patch writes, other LFOs and
  routed traffic; no two owners fight over a parameter or restore stale state.
- Use an independently timestamped receiver to measure update spacing, jitter,
  actual values and start/stop latency during scrolling, transfers and other
  MIDI traffic. Verify supported receiver resolutions and record practical
  simultaneous-LFO limits. Host tests do not establish musical smoothness.

## Additional work beyond the prompt

None.
