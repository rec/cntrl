# Feature ideas

Ideas for discussion, grouped by topic. These are not implementation commitments
or a priority order.

## Performance organization

### Song sections

Add named song sections such as intro, verse, chorus and solo. Each section
recalls its associated selections, while the displays show the current section
and what comes next. Allow a song to appear more than once in a setlist with
different section orders.

### Multi-instrument scenes

Recall a collection of instrument patches and controller values with one press.
A scene could select a synth sound, change an effects preset and set a mixer
level together. Preview the whole scene before sending, and make undo restore
the previous scene's known selections.

### Favorites and recent selections

Keep a small favorites list and a list of recently used patches across setups.
Allow quick access without traversing the full collection of patch lists. Store the patch's
instrument identity alongside its name so similarly named sounds remain distinct.

## Controls and expression

- Pedal speed calibration.

### MIDI learn

Move a control or send a message from another device to assign it without
entering channel and controller numbers manually. Show the captured message
and let the user confirm its role, such as controlling a parameter or selecting
a scene.

### Expression-pedal curves and ranges

Give each expression assignment its own minimum, maximum, direction and response
curve. One pedal could raise an effect level over part of its travel while
reducing another parameter. Include a dead zone where small movements should
have no effect.

### Parameter pages

Provide named pages for commonly adjusted instrument parameters, such as filter
cutoff, reverb level or modulation depth. Display musical labels and values
instead of bare controller numbers, and let the footswitches choose which
parameter an expression pedal controls.

### Parameter pickup

When an expression pedal takes control of a parameter, wait until its position
crosses the current known value before transmitting changes. Show whether the
pedal needs to move up or down to take control. This avoids sudden jumps when
changing assignments.

### Scene morphing

Use an expression pedal to move between two sets of continuous parameter values.
For example, move from a quiet, dry sound to a brighter sound with more delay
and modulation. Treat discrete patch changes separately from interpolated
controller values.

## MIDI routing and processing

- MIDI routing.
- Filtering.
- Remapping.
- Converting.
- Delay.

## Clock, synchronization and scheduling

- Receive beats, song position and time.
- Defer patch changes until the next beat, bar or four-bar boundary.
- Sync metronomes.
- Tap tempo.

## Note generation, looping and modulation

- Looper.
- Arpeggiator (from arpeg).
- enveLFO (from engeLFO).

### Note and chord triggers

Assign a note or chord to a footswitch for drones, bass notes or sample triggers.
Support momentary playing and an explicit latch mode, with selectable velocity
and octave. Releasing or cancelling the action should release the notes it owns.

## Diagnostics, feedback and performance safety

### Live MIDI diagnostics

Show incoming and outgoing activity by port and channel, with a readable recent
message list. Help diagnose a silent instrument, wrong channel or unexpected
controller traffic directly on the pedal. Let the user pause the view to inspect
a message.

### Feedback from connected devices

Use incoming state reports to update displayed patch names, parameter values
and effect states when another controller changes them. Distinguish confirmed
device state from the pedal's last transmitted value. This would depend on what
each connected device can report.

### Performance mute

Suppress new musical messages while allowing navigation and configuration,
with an unmistakable display indication.

## Additional work beyond the prompt

None.
