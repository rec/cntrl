# Desktop controller emulator

The emulator runs cntrl's shared C++ application in a native host process. A
Python/PySide6 window supplies the controls and shows the production renderer's
pixels. It does not run an ARM image or a manufacturer's original firmware.

Display images use integer scaling in physical screen pixels, including on
Retina and fractional-DPI screens. Every pedal pixel occupies an equal-sized
square block, with no smoothing. Small black margins inside the bezels are
normal. If the window is too small for native-size pixels, the display is
cropped; enlarge the window to see the full image.

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

To inspect the session actually stored on a connected pedal, use:

```sh
cntrl emulate --port /dev/cu.usbmodem12345
```

This reads the controller model, compiled setup journals, selected setup,
history, assets, language packs and device-global preferences from the pedal.
It does not compile setup sources or install additional language packs. The
pedal's persistent format must match the emulator. `--controller`, if supplied,
must match the connected model. Mirroring reads the pedal without changing it;
later emulator edits affect only the local snapshot.

The Status page also uses a captured pedal snapshot, including RAM and disk
usage, startup count, uptime, Git ID, cycle/patch counts and clock/tap values.
These values stay fixed for comparison; they are not live pedal telemetry or
measurements of the host computer. Setup previews continue to report their
emulated session. Capturing requires firmware with `CNTRL STATUS SNAPSHOT`;
unsupported firmware is rejected rather than replaced with placeholder values.

`status-snapshot.bin` preserves the command's 111-byte `CSS1` payload. Integers
are little-endian: two RAM used/free pairs (four uint32s), disk used/free and
startup count (three uint64s), uptime milliseconds (uint64), Git ID and
cycle/patch counts (three uint32s), disk-valid and startup-state bytes, a
32-byte NUL-terminated startup error, tap period milliseconds (uint32), clock
period microseconds (uint64) and a clock-active byte. Startup states are
unavailable (0), ready (1) and error (2). The shared page renders the decoded
values using its existing layout and text fitting.

Each capture creates a fresh `mirror-<identifier>` subdirectory beneath
`.cntrl-emulator/<profile>/`, or beneath `--storage DIRECTORY`. The snapshot's
`mirror.toml` records the source status, selection and captured file hashes.
Keep the pedal idle while capturing. A changing journal or selection aborts the
capture rather than opening an inconsistent session. Failed captures remain
available for diagnosis. Source-file launches print `Setup preview`, while
connected-pedal launches print `Pedal mirror` and the snapshot path.

Generated files and saved pedal state live in `.cntrl-emulator/<profile>/` by
default. This directory is ignored by Git. Repeated launches preserve saved edits
when the supplied compiled bundle is unchanged. A changed bundle replaces the
saved setups on launch while retaining unrelated session files. The first launch
after upgrading to runtime font loading also applies the supplied bundle to
establish this baseline. Use `--storage DIRECTORY` to select a different session,
including a fresh one for an independent run. This changes only the emulated
filesystem.

For a custom controller, supply its layout and execution target explicitly.
The positional input can be a setup source file or an already compiled CSTB
bundle:

```sh
cntrl emulate setup.toml --layout controller-layout.toml --target target.toml
```

The native executable and generated target header are built under the
session's `.emulator-build` directory. Generated outputs can be recreated from the checkout
and execution target. The session directory supplies the emulated filesystem;
no SD card or formatting operation is involved. Source previews do not access a
connected pedal. Reopen the same
directory to retain its saved data; choose a new directory for a fresh session.

The native executable is reused when its source and header contents, compiled
target capabilities, compiler identity, options and compiler environment are
unchanged. Changes to these inputs rebuild it automatically. Fonts and target
font metrics are runtime data and never trigger a native rebuild. Setup compilation
still runs when a source setup is supplied. A failed build is never recorded as
reusable.

All fonts load from `.cfont` files when the emulator starts. UI font files come
from the execution target's `font_sources`; their existing family identities and
utility-page choices are preserved. Custom patch fonts use the existing setup
asset files. Missing or malformed UI font files stop startup with an error naming
the file. Replacing a font takes effect at the next launch; live reload is not
performed. Font bytes are rendered by the same rasterizer as the pedal.

Patch fonts and movies exported beside the compiled bundle are imported into the
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

MIDI Cable Test remains in the page cycle with its original Atkinson font,
toggle and port indicators. Its probes run locally and are never sent to host
MIDI ports or included in the accepted-output trace. Virtual MIDI injection can
return probe messages to exercise the input indicators. Leaving the page stops
the test and restores normal setup routing.

Panic remains available and sends through the shared firmware's MIDI output
path. When the selected controller supports USB keyboard output, its Keyboard
page and patches emit inspectable eight-byte HID reports in the event log and
saved trace. They do not type into other applications. Stock-firmware selection
emits a visible simulated transition and suspends the CNTRL runtime; restart
the session to return. The emulator cannot execute the pedal's stock ARM image.

Captions and instructions use the physical control labels, independently of
the logical IDs used by replay files and the controller catalogue. The window
title distinguishes a setup preview from a pedal mirror.

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
