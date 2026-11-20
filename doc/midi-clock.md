# Shared MIDI clock

The shared clock supplies timing to the arpeggiator, looper and MIDI file
services. The two-window **Time** page combines source selection, clock output
and Tap Tempo. Two taps set the working tempo to the measured period. After a pause the
first tap preserves the tempo until a second valid tap. Changing tempo during
playback preserves fractional tick phase.

Configure clock preferences under page settings:

```toml
[page_settings]
clock_outputs = ['din', 'usb-device']
clock_bpms = [120]
clock_timeout = 0.250
autoselect_clock = true
```

The first configured tempo (20–600 BPM) is selected on setup activation.
The default is 120 BPM and no outputs. Choose available
target output names: DIN, TRS, USB device or USB host. Disabled USB ports and
duplicate destinations are rejected. The configurable TRS jacks
(`configurable-1` through `configurable-4`) must have a MIDI output role.

The left Time window shows separate Tap, DIN and TRS tempos. A green outline
marks the selected source. The next-section control (E on MC Pro) cycles down
through them. The next control (B) toggles Send, initially off. When enabled,
Tap generates MIDI Clock at the measured tempo; DIN and TRS relay the selected
input's ticks directly to `clock_outputs`. These destinations reserve clock
traffic for Time, preventing duplicate ticks from ordinary forwarding.

DIN and TRS become blank independently after `clock_timeout` seconds without
ticks. Tap remains measured until cleared by holding the confirm control
(C on MC Pro). A successful tap measurement selects Tap. With
`autoselect_clock = true`, the selected source stays selected while valid;
if it becomes blank, another measured source is selected, or Tap if none is
available. With it disabled, selection changes only through E or a successful
tap measurement. Sending clock does not send transport Start or Stop messages.
Selecting a blank source with E waits for that source instead of immediately
falling back. Once it has received a measured clock, automatic fallback resumes
if that clock later expires. The Tap Tempo circle sits at the bottom right of
its window, using the configured `flasher_diameter` (40 pixels by default).
Activating a setup, entering stock firmware or starting a port-owning
diagnostic stops local generation.

The scheduler generates 24 ticks per quarter note. Each poll sends at most one
current tick per active destination; time spent late never creates a burst.
A blocked destination misses its tick independently. Transport precedes ticks
on that destination. Stop is retried while the output is available.

## Timing qualification

The scheduler tracks its worst lateness (microseconds), skipped deadlines and
failed tick submissions since Start/Continue. At 120 BPM
the expected wire spacing is 20,833⅓ microseconds. To measure physical jitter,
capture the selected output with an independently timestamped MIDI receiver,
first idle, then during display scrolling and a setup download. Record observed
tick count, mean and maximum interval error, and transport order.
Scheduler counters measure submission, not receipt at an instrument.

The native scheduler test models alternating 307 µs and 1,999 µs poll delays
across timer rollover. At 120 BPM it submits 24 ticks in 500 ms; intervals
from Start through the last tick range from 20,352 to 22,753 µs with a mean
of 20,833⅓ µs. A separate 250 ms stall at 240 BPM yields one current tick
and 23 skipped deadlines. These are simulated poll delays, not measurements
on a physical MIDI port.
