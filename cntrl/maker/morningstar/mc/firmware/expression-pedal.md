# Expression pedal

Configure the jack as an expression input in the setup, then give it a MIDI
assignment. For a pedal on configurable jack 2 controlling expression (CC 11)
on DIN channel 1:

```toml
[trs_ports]
configurable-2 = 'expression'

[page_settings.expression.configurable-2]
output = 'din'
channel = 1
controller = 11
minimum = 0
maximum = 127
reverse = false
curve = 'linear'
```

Each jack has one assignment per setup. Channel is 1..16; controller and both
limits are 0..127. Set `reverse = true` for maximum at the heel. The `slow`
curve rises more gradually at the heel (quadratic); `fast` rises more quickly
there (inverse quadratic). Both reach the configured endpoints. The assignment
is included in compiled setup downloads and history. An assignment requires an
expression-role input and an available output in the target configuration.

**Calibration** uses one display in the default utility cycle. With an explicit
page catalogue, include it in the utility region:

```toml
[pages.calibration]
type = 'calibrate'
```

Press C to start calibration. Move each pedal toward heel first, then reverse
toward toe. The pedals can be moved in any order. Heel turns green after
reversal. Toe turns green after reversing again or settling for two seconds.
The widest range remains selected across repeated sweeps. Press C again to save
completed ranges. Leaving the page cancels the session. Incomplete ranges leave
the previous saved values in place. The display shows both endpoints as
percentages of the 12-bit ADC range.

The pedal saves calibration in `/cntrl/calibration.toml` on the SD card and
loads it at startup. All setups share it. The raw files in a pedal backup
include this file. To restore it from a backup, copy it back to the same location
on the SD card while the pedal is off.

Open **Settings → Pedal → Calibrate**, then press D for **Options**. Choose a
jack to edit its name, or choose **Settling time** to set how long the toe reading
must remain steady to complete capture. The default is 2,000 ms. A/B adjust
the delay and E changes the step. Review and save the change to apply it to
all setups. Saving either option preserves the measured endpoints.

```toml
bits = 12
settle_ms = 2000

[configurable-1]
name = "Boss EV-30, left"
heel = 472
toe = 3627
```

Each jack can have a single quoted `name`, which is optional and preserved
when recalibrating. The file accepts single-line entries and decimal endpoint
values from 0 to 4095. A file without `bits` uses the previous 10-bit scale.
Edit the file directly while the pedal is off. When there is no TOML
file, the firmware can still read older per-jack calibration records. The next
successful calibration save carries those measurements into the TOML file.

Assignments work during performance, including away from this page. On setup
selection, calibration, or reconnection, movement is required before sending.
No CC is sent during calibration on that jack. Filtered position and movement
hysteresis suppress repeated stationary values. When an output is busy, only
the most recent position is eligible. The cable tester and panic retain their
exclusive access.

The MC6 Pro role driver supplies the ring and samples the tip of a passive TRS
pedal. The reader alternates weak tip biases and measures them after settling.
A tip that follows both biases is treated as disconnected. This is a continuity
estimate, not a mechanical jack switch. Physical qualification remains pending:
with a known pedal, verify silence unplugged and at rest, the complete heel/toe
travel in both directions, and unplug/reconnect at heel, middle and toe. No
expression pedal was available for those checks.
