# Live pedal remote control

Let a musician use the emulator window as a remote front panel for a
USB-connected pedal. The pedal runs its own menus, gestures, setup editing and
MIDI output. The desktop uses the existing controller layout and physical
button labels. The existing [Pedal mirror](../doc/emulator.md) continues to
open a local session from a capture of the pedal.

## Phase 1. Live buttons

Start with the MC6 Pro. Use the existing USB command connection and add one
small command family for arming, updating and ending a remote input session.
The host verifies the connected model and capability before enabling the
controls. Show when the session is live and when it loses contact. The physical
pedal displays provide feedback for this phase.

- Feed a bounded mask of held logical buttons into the
  [target switch scan](../cntrl/maker/morningstar/mc/firmware/patch_chooser/target.cpp)
  just before it calls the shared [input coordinator](../firmware/include/cntrl/ui/input.h).
  Use the selected controller's button count and preserve the physical
  switches. The existing coordinator supplies debounce, holds, chords and
  page routing. The desktop sends held state and leaves gesture timing to the
  pedal.
- Keep the USB handler to validation and updating session state. The normal
  scan consumes the input. Keep work and memory per command bounded and use
  the existing firmware gates for periods such as setup activation. Leave
  auxiliary switches, expression and keyboard input outside this first phase.
- Arm only when the user explicitly starts live control. Use a short expiry
  refreshed by the host. Send releases on normal focus loss, and disarm on
  disconnect, expiry or session close. Reject stale updates and require a fresh
  session to resume. Test that cancelling an interrupted remote gesture clears
  remote-held state without firing an unintended release action. If that
  requires invasive changes to shared gesture handling, narrow the prototype
  to bounded taps and revisit holds and chords after bench testing.
- Route desktop input to the connected pedal in this mode. Keep page execution,
  configuration writes and musical output on the pedal so a click has one
  effect and the existing Save and Discard flows apply.

Allow one to three days for a button prototype and initial bench testing.
Re-estimate after verifying interruption behavior and MIDI timing.

## Phase 2. Live display feedback

Use the pedal's rendered page and display values as the source for the remote
window. Physical presses, incoming MIDI and completed operations can change the
pedal even when the desktop sends no input. Start with a small, read-only
snapshot of the active page and its available display values. Bound its size
and polling rate. Pause polling during transfers and visibly mark stale
feedback on connection loss.

Review pixel-for-pixel display streaming separately if the snapshots fall short.
Measure USB bandwidth and any effect on display updates, switch scanning and
MIDI before deciding whether to add it.

## Verification

- Check capability detection, disabled and expired sessions, malformed masks,
  stale messages, and reconnect with buttons held. Exercise simultaneous
  physical and remote input, including a physical switch corresponding to a
  held remote button.
- Compare remote and physical presses, holds and chords on the pedal. Verify
  menu navigation, patch selection, MIDI output and the existing Save and
  Discard behavior. Cover focus loss, application exit, cable removal and
  disconnection while a gesture is pending or a momentary action is active.
- Compare input scan gaps, MIDI service gaps and clock timing with live control
  off and active under representative MIDI traffic. Verify MIDI delivery with
  an independent receiver. Repeat with polling enabled before releasing display
  feedback.

## Additional work beyond the prompt

None.
