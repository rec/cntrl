# Desktop controller emulator

The emulator runs cntrl's shared C++ application in a native host process. A
Python/PySide6 window supplies the controls and shows the production renderer's
pixels. It does not run an ARM image or a manufacturer's original firmware.

Install from the source checkout, including the optional desktop/MIDI packages:

```sh
uv sync --frozen --extra emulator --no-editable
```

The GUI uses the official PySide6 Essentials package; web-engine and other Qt
add-ons are not required. Reccy and middy come from the Git revisions in the
lockfile, without editable or sibling-directory dependencies. A native C++17
compiler is required: `c++` on macOS/Linux or `cl` in a Windows developer shell.

## Three separate inputs

- The existing controller catalogue describes controls, displays and ports.
- An emulator layout TOML places catalogue components on a dimensionless canvas.
  It references controls and displays by their existing IDs. It does not assign
  MIDI actions or redefine native pixel resolution.
  An optional `faceplate` image path, relative to the layout TOML, supplies the
  enclosure artwork. It scales with the controls and live displays. With artwork,
  buttons use a translucent pressed-state highlight over the pictured switches.
- A cntrl execution target supplies supported application capabilities, input
  bindings, native viewports and exported fonts. Compile setups against that
  target. A catalogue entry alone does not supply an execution implementation.

Run commands from the source checkout with the environment activated, or prefix
them with `uv run --no-sync`. Start a built-in controller directly from a setup
source file using `cntrl emulate SETUP --controller PROFILE`; the command exports
the controller target, compiles the setup and starts the native runtime.
See the manufacturer profile guide below for the supported profile names.

Generated files and saved pedal state live in `.cntrl-emulator/<profile>/` by
default. This directory is ignored by Git. Repeated launches preserve the saved
state. Use `--storage DIRECTORY` to select a different session, including a fresh
one for an independent run. This changes only the emulated filesystem.

For a custom controller, supply its layout and execution target explicitly.
The positional input can be a setup source file or an already compiled CSTB
bundle:

```sh
cntrl emulate setup.toml --layout controller-layout.toml --target target.toml
```

The native executable and generated target/font headers are built under the
session's `.emulator-build` directory. Generated outputs can be recreated from the checkout
and execution target. The session directory supplies the emulated filesystem;
no SD card, formatting operation or connected pedal is involved. Reopen the same
directory to retain its saved data; choose a new directory for a fresh session.
Fonts and movies exported beside the compiled bundle are imported into the
session through the existing asset formats. Put referenced MIDI files under
the session's `midi/` directory before playing them.

See the [Morningstar profiles](../cntrl/maker/morningstar/mc/emulator/README.md)
for the initial six-switch/three-display and eight-switch/four-display layouts.

## Controls and inspectors

Press a switch with the mouse or its labelled keyboard key. Auto-repeat does
not generate repeated presses. Shift-click or right-click latches a switch so
chords and holds can be operated with one mouse. Leaving the window releases
held controls through the normal input path.

The inspector provides virtual MIDI injection, output blocking, connection
state, manual time stepping, expression/auxiliary inputs and display snapshots.
The enclosure drawing retains
its aspect ratio when resized, and each display retains its native aspect ratio
and pixels within its placement rectangle. The inspectors do not take firmware
display windows.

Host MIDI connections use middy and are opened only after explicit selection.
Loading a profile does not connect or send to a real MIDI destination. MIDI
captured from the runtime records acceptance by the emulated output transport;
it is not acknowledgement from a synthesizer. The emulator uses only the setup's
explicit `[midi]` routes for incoming MIDI. It does not infer the physical
adapter's legacy USB/DIN forwarding connections or echo input back to itself.

Built-in controller features that the selected execution backend cannot service
are rejected explicitly. The generic layout model has no six/eight-switch or
three/four-display limit. The initial cntrl backend retains its existing eight
physical-input, five assignable switch-slot and sixteen-route limits. Numeric
and character-display backends and built-in relative/absolute control bindings
require additional application support; they are not silently mapped to buttons.

## Deterministic replay

The same native process and input path serve interactive and headless operation.
A TOML scenario selects a random seed, ordered input events and an end time.
Save replay exports the inputs from the current session. Replaying requires the
same target, bundle and initial session state; use fresh session directories
for independent reproducible runs, or preserve a copy of an existing session
before recording changes to it.
Times accept the project's duration notation. Events with the same timestamp
are applied in file order. Advancing time never rewinds the runtime.

```toml
seed = 17
until = '1 second'

[[events]]
at = '20 milliseconds'
kind = 'button'
target = 'a'
value = 1

[[events]]
at = '120 milliseconds'
kind = 'button'
target = 'a'
value = 0
```

Other event kinds are `midi` (`target` endpoint name and `data` byte list),
`block` (`target` output name and `value` zero/one), `connection` (`target`
endpoint and `value` zero/one), `expression` (`target` port slot as text and
`value` raw 0..1023), `auxiliary` (`target` port slot and `value` contact mask
0..3), `cleanup`, and `text` (`text` ASCII cue and optional `value = 1` for
warning). Input follows production debounce and scheduling;
a press/release at one instant is not a debounced physical press.

```sh
cntrl emulate setup.bin --layout controller-layout.toml --target target.toml --storage replay-session --scenario scenario.toml --output replay-results
```

Replay exports a PNG for each display and an accepted-output/error trace in
`trace.toml`. Use a fresh session directory when comparing independent scenarios.

Add `--interactive` to run the scenario as startup input and then open the
window in that resulting state. Without a scenario, the window opens normally.

## Evidence limits

Native operation can reproduce application behavior, rendering, messages,
storage policies and MIDI scheduling under the injected conditions. It does not
qualify physical USB/DIN timing, wiring, display quality, MCU memory layout,
interrupts, DMA, power loss or firmware bootability on an unowned controller.
