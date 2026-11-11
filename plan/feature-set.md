# Remaining features

Unfinished features from the [README](../README.md). This backlog is not
authorization to implement every item immediately.

The graphical configuration editor remains tracked separately in
[GitHub issue #4](https://github.com/rec/cntrl/issues/4).

## MIDI looper

Implemented the [live MIDI looper](../doc/midi-looper.md), including overdub,
Undo, page controls, patch actions and Save/Load. Independently timestamped
physical timing and receiver qualification remain outstanding in
[the looper plan](looper.md).

## Arpeggiator

Implement the arpeggiator page and MIDI processing for both polyphonic and
monophonic instruments, using arpeg as a reference. See the
[arpeggiator plan](arpeggiator).

## Drone controller

Implement the dedicated drone controller page and controls for starting,
holding and releasing drone notes. Existing note patches and
[momentary/latched controls](../doc/momentary-latched.md) provide basic note
handling.

## Incoming text messages

Implemented [text messages](../doc/text-messages.md) over MIDI SysEx and USB
serial, including cues and warnings from a computer. Physical display and
transport qualification remain outstanding.

## Additional feature ideas

Ideas for discussion, grouped by topic. These are not implementation commitments
or a priority order. Implemented parts are omitted; partially completed ideas
describe the remaining work.

### Performance organization

#### Song sections

Add named song sections such as intro, verse, chorus and solo. Each section
recalls its associated selections, while the displays show the current section
and what comes next. Allow a song to appear more than once in a setlist with
different section orders.

#### Multi-instrument scenes

Add a scene view that previews the children of a Composite Patch together and
restores the previous scene's known selections. Composite Patches already execute
several instrument and controller changes with one press; current Undo restores
known program selections without reversing arbitrary effects.

#### Favorites and recent selections

Keep a small favorites list and a list of recently used patches across setups.
Allow quick access without traversing the full collection of patch cycles. Store
the patch's instrument identity alongside its name so similarly named sounds
remain distinct.

### Controls and expression

- Pedal speed calibration.

#### MIDI learn

Move a control or send a message from another device to assign it without
entering channel and controller numbers manually. Show the captured message
and let the user confirm its role, such as controlling a parameter or selecting
a scene.

#### Expression travel zones and multiple assignments

Allow one pedal to control several parameters over different portions of its
travel, such as raising an effect level while reducing another parameter.
Add configurable dead zones. Per-assignment minimum, maximum, direction and
linear/slow/fast response curves are already supported.

#### Parameter pages

Provide named pages for commonly adjusted instrument parameters, such as filter
cutoff, reverb level or modulation depth. Display musical labels and values
instead of bare controller numbers, and let the footswitches choose which
parameter an expression pedal controls.

#### Pickup direction feedback

Extend the existing [controller pickup](../doc/pickup.md) with a display indication
of whether the pedal needs to move up or down to take control. Waiting until the
controller reaches or crosses the destination's last accepted value is already
implemented for expression pedals and live MIDI controllers.

#### Scene morphing

Use an expression pedal to move between two sets of continuous parameter values.
For example, move from a quiet, dry sound to a brighter sound with more delay
and modulation. Treat discrete patch changes separately from interpolated
controller values.

### Clock, synchronization and scheduling

- Extend the existing incoming-clock tempo and beat tracking to song position
  and time.
- Defer patch changes until the next beat, bar or four-bar boundary.
- Sync metronomes.

### Note generation and modulation

- enveLFO (from engeLFO): identify the remaining envelope functionality beyond
  the existing [MIDI LFO generators](../doc/lfos.md).

Basic note and chord triggers already use MIDI patches with
[momentary or latched controls](../doc/momentary-latched.md). The dedicated
Drone controller and Arpeggiator work is tracked above.

### Feedback and performance safety

#### Feedback from connected devices

Use incoming state reports to update displayed patch names, parameter values
and effect states when another controller changes them. Distinguish confirmed
device state from the pedal's last transmitted value. This would depend on what
each connected device can report.

#### Performance mute

Suppress new musical messages while allowing navigation and configuration,
with an unmistakable display indication.

### Input, remote UI and display extensions

- Accept a USB typing keyboard for text entry, search and navigation, keeping
  keyboard drivers in the target adapter and verifying socket power on hardware.
- Provide an optional browser UI over the existing USB serial transport for setup
  transfer, browsing and explicit actions. Detect browser support and keep the
  pedal independent when the browser disconnects. Coordinate this with
  [the graphical configuration editor issue](https://github.com/rec/cntrl/issues/4).
- Extend the existing [text messages](../doc/text-messages.md) with bounded
  graphics popups over SysEx or serial and explicit popup dismissal. Expiry and
  dismissal must restore the current underlying UI without changing patches.
- Render the optional image references already retained in styles. Conversion,
  embedding, layout and image loading remain future work.

## Additional work beyond the prompt

None.
