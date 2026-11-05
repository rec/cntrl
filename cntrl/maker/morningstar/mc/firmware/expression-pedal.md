# Expression pedal

Configure the jack as an expression input in the setup, then give it a MIDI
assignment. For a pedal on configurable jack 2 controlling expression (CC 11)
on DIN channel 1:

```toml
[trs_ports]
configurable-2 = 'expression'

[utility_pages.expression.configurable-2]
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

Browse with F to **Expression**. A selects a jack. Press C to begin calibration,
move fully to heel and press C, then move fully to toe and press C again.
B cancels, as does leaving the page. A short or missing stroke cannot replace
a working calibration. The page shows the jack, channel/controller, calibrated
position, connection and calibration status. Heel and toe can have either
electrical polarity. Calibration is shared by setups and stored separately on
the SD card in two checksummed records per jack.

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
