# MIDI Clock pages

Use **MIDI Clock** to control transport. A sends Start, B sends Continue,
C sends Stop, and E switches between internal and external clock. The internal
clock starts stopped, and runs across page changes. F browses pages; hold D
and press F to browse backward from a clock page. An asterisk marks an unavailable output. The transport controls on
this page take precedence over patch actions.

**Clock Tempo** shows the working BPM and the selected stored slot. A and B
adjust the working tempo by one BPM; C recalls the next slot, wrapping to the
first. Two taps on **Tap Tempo** set the working tempo to the measured period.
After a pause the first tap preserves the tempo until a second valid tap.
Changing tempo during playback preserves fractional tick phase. Setup slots
are authored values; live adjustments last until another slot or setup is
selected.

Configure these preferences under the existing page settings:

```toml
[page_settings]
clock_outputs = ['din', 'usb-device']
clock_bpms = [90, 120, 135]
clock_source = 'internal'
```

Set one to four integer tempos (20–600 BPM). The first is selected on setup
activation. The default is one 120 BPM slot and no outputs. Choose available
target output names: DIN, TRS, USB device or USB host. Disabled USB ports and
duplicate destinations are rejected. The configurable TRS jacks
(`configurable-1` through `configurable-4`) must have a MIDI output role. Clock settings use an optional section in format 29. Existing format 29 setups
and embedded song sources continue to load with the default clock settings.

Internal mode reserves the configured outputs for local Clock, Start, Continue
and Stop, even while stopped. Other forwarded messages and unselected routes
retain their existing behavior. At Start or Continue only currently available
outputs join. Disconnect removes an output from that run. Reconnection requires
a fresh transport command. External mode forwards through the existing bridge
when enabled and shows the measured incoming tempo. Loss of incoming clock
shows “waiting” after the setup's clock timeout. Selecting external mode,
activating a setup, entering stock firmware or starting a port-owning
diagnostic stops local generation.

The scheduler generates 24 ticks per quarter note. Each poll sends at most one
current tick per active destination; time spent late never creates a burst.
A blocked destination misses its tick independently. Transport precedes ticks
on that destination. Stop is retried while the output is available.

## Timing qualification

Clock Tempo reports the worst scheduler lateness (microseconds) and the sum of
skipped deadlines and failed tick submissions since Start/Continue. At 120 BPM
the expected wire spacing is 20,833⅓ microseconds. To measure physical jitter,
capture the selected output with an independently timestamped MIDI receiver,
first idle, then during display scrolling and a setup download. Record observed
tick count, mean and maximum interval error, transport order and page counters.
Scheduler counters measure submission, not receipt at an instrument.

The native scheduler test models alternating 307 µs and 1,999 µs poll delays
across timer rollover. At 120 BPM it submits 24 ticks in 500 ms; intervals
from Start through the last tick range from 20,352 to 22,753 µs with a mean
of 20,833⅓ µs. A separate 250 ms stall at 240 BPM yields one current tick
and 23 skipped deadlines. These are simulated poll delays, not measurements
on a physical MIDI port.
