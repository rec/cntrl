# Reconfigurable MIDI controller emulator

Status: implemented with the shared native runtime and optional PySide6/middy
frontend. Usage is documented in `doc/emulator.md`; the initial profile exports
and layouts are under `cntrl/maker/morningstar/mc/emulator/`. A macOS interactive
smoke check verified both profile layouts, virtual switch MIDI output, latched
chords, release-all and 10 ms manual time stepping. Physical host MIDI and the
Linux/Windows desktop paths remain to be qualified. The MC8 Pro profile has
explicitly provisional geometry.

Implement one emulator that can represent different MIDI controllers, starting
with the current MC6 Pro and an MC8 Pro profile. Adding a
controller with supported features should require data, rather than a separate
application. New kinds of controls, displays or behavior can require software.

The initial execution backend runs cntrl's shared C++ application on the host.
It must execute the production logic, rather than reproduce that logic in
Python. This is application and peripheral emulation, not ARM instruction
emulation or a reproduction of a manufacturer's original firmware.

## Existing foundations

- `cntrl/models.py` defines `Controller`, `Button`, `AbsoluteControl`,
  `RelativeControl`, `Display` and port records. Display kinds include numeric,
  alphanumeric and graphical. These are hardware descriptions independent of
  firmware and editors.
- `cntrl/catalogue.py:load_catalogue` loads TOML into those records. The current
  catalogue shorthand constructs buttons from `buttons`; it does not yet expose
  every control kind that the Python model can represent.
- `cntrl/patch_switcher/models.py:Target` describes a cntrl execution target:
  logical inputs, action bindings, graphical viewports, endpoints, fonts,
  resource limits and supported functions. It is not interchangeable with the
  hardware catalogue's `Controller`.
- The shared C++ runtime, application, pages, input coordinator, MIDI engines,
  storage policy and pixel renderer already compile for native host tests.
  `ui::Display` accepts RGB565 rectangles independently of a physical display.
- Native protocol peers and host filesystem tests demonstrate local process
  communication and ordinary files backing firmware storage interfaces. They
  are useful examples, not an existing complete interactive emulator.

Use these models and services. Do not create a second hardware catalogue, a
second page renderer or a Python implementation of the firmware state machines.

## Separate hardware, appearance and execution

| Record or component | Owns |
| --- | --- |
| Existing `Controller` record | What physical controls, displays and ports exist, their identities and known capabilities |
| New `EmulatorLayout` record | Where those components appear on screen and their presentation bounds |
| Existing cntrl `Target` and execution adapter | Which application runs, its supported features, logical bindings and resource constraints |
| Emulator session | Pressed controls, positions, clock, connections, loaded files and live output |

The layout does not define MIDI assignments, page allocation, display resolution,
switch gestures, firmware behavior or setup contents. Those belong to the
existing hardware/application records and running software. A switch's screen
position must never determine its action. The existing page layout continues to
allocate logical display windows independently of the drawing of the enclosure.

All generic emulator code and models belong under `cntrl/emulator/`, with shared
native adapter code under `firmware/` where appropriate. All Morningstar-specific
layouts, execution composition, data, tests and documentation remain under
`cntrl/maker/morningstar/mc/`. The generic core must not import a manufacturer
package or assume pedal-shaped hardware.

## New Python and TOML layout record

Define `EmulatorLayout` as a frozen Pydantic model, using the project's existing
TOML tooling. It contains:

- A catalogue path and controller key selecting an existing `Controller`.
  Resolve the path relative to the layout file, without a working-directory
  convention or a hardcoded manufacturer search.
- A canvas width and height in dimensionless design coordinates. These express
  relative physical arrangement, not millimeters or native display pixels.
- Control placements keyed by the existing `control.id`, and display placements
  keyed by `display.id`. Each placement has an `x`, `y`, `width` and `height`.
- Optional port placements for showing the enclosure's connections. Reference
  the existing `Controller.ports` list by zero-based index, since ordinary USB
  sockets are enum values without IDs. Do not change USB records just to draw
  them. Omitted ports remain accessible through the connection inspector.

Use dictionaries and lists of typed placement records. The control's existing
kind selects its widget; labels come from the catalogue. Do not repeat button
names, port specifications, value ranges, display kinds or resolutions in the
layout. Defer decorative skins, photographs, arbitrary artwork and a graphical
layout editor until there is a separate requirement.

Illustrative fragment of
`cntrl/maker/morningstar/mc/emulator/mc6-pro.toml`:

```toml
catalogue = '../mc.toml'
controller = 'mc6_pro'
width = 100
height = 60

[controls]
a = {x = 8, y = 40, width = 10, height = 10}
b = {x = 45, y = 40, width = 10, height = 10}
# A complete file also places c, d, e and f.

[displays]
lcd-1 = {x = 4, y = 6, width = 28, height = 24}
lcd-2 = {x = 36, y = 6, width = 28, height = 24}
lcd-3 = {x = 68, y = 6, width = 28, height = 24}
```

This is schema illustration, not measured enclosure geometry or a runnable
partial profile. The initial complete layouts represent six switches and three
graphical displays for MC6 Pro, eight switches and four graphical displays for
MC8 Pro. Obtain their actual arrangement before claiming physical fidelity.

Validate the selected controller, positive finite canvas/bounds, references,
duplicate placements and placement within the canvas. Require one placement for
each built-in control and display. Keep these checks in the model and binding
logic; do not silently drop unknown components or substitute another model.

Resizing the application scales the entire design uniformly and letterboxes
unused space. Convert pointer coordinates back to design coordinates for hit
testing. Keep the native display framebuffer and aspect ratio independent of
the containing screen rectangle. A higher-resolution host screen must not
change firmware layout calculations or bitmap content.

## Execution backend and target binding

Use a Python frontend with a native C++ process containing the shared runtime.
Communicate through bounded local pipes, following the existing native-peer
approach. Put reusable production communication code outside the test package.
The frontend sends logical input and session commands and receives display
updates, MIDI activity and errors. It does not schedule musical events or
reimplement application behavior. Keep the GUI toolkit out of native code.

The native adapter supplies display surfaces, clock, MIDI queues, filesystem
handles and the composition of the existing application services. Use
`InputCoordinator`, `CooperativeRuntime`, `PageHost`, the renderer and MIDI
runtime directly. Do not compile the physical target's GPIO, DMA, USB drivers,
bootloader or SD driver into the desktop process, or copy `target.cpp` and
replace hardware calls piecemeal.

For cntrl execution, supply an existing-format `Target` export and the required
fonts. Bind catalogue controls and displays to target logical IDs explicitly
in adapter composition, preserving the existing IDs where possible. The
layout's dictionary order and screen coordinates have no binding significance.
MIDI endpoint bindings likewise refer to declared ports/endpoints, not inferred
left/right positions or manufacturer-specific route numbers.

Validate compatibility before starting a session. A hardware catalogue entry
does not assert that cntrl supports that device, has the correct firmware port,
or can display all its pages. If an execution backend cannot service a declared
feature, explain the unsupported feature rather than simulating success.

The generic layout and input models must not inherit the current cntrl target's
limits. Today cntrl supports eight declared physical inputs, five configured
switch slots, sixteen MIDI routes and graphical target displays. More controls,
different display kinds or additional application actions require corresponding
runtime/compiler work. Eight visible MC8 Pro switches must not be advertised as
eight independently assignable patch slots under the current five-slot format.
Do not change that format or increase these limits as part of the first emulator.

There are unresolved source facts to check before binding the first profiles:
the catalogue lists MC6 Pro displays as 200 by 200, while the current physical
adapter uses 160 by 128 viewports; MC8 Pro display dimensions are unknown in the
catalogue. Determine whether the MC6 discrepancy is an error or a distinction
between physical and usable geometry. The appearance record must not become
an undocumented hardware override. A provisional MC8 profile may exercise four
explicitly specified virtual viewports, clearly labelled provisional; it must
not silently invent physical display specifications.

## Input and presentation behavior

- Buttons generate independent down/up state. Permit chords and holding one
  control while operating another. Mouse interaction must support this, rather
  than restricting the controller to one held switch. Keyboard repeat must not
  synthesize repeated presses. Clear held input on focus loss or disconnection
  through the ordinary release path.
- Absolute controls use their declared ranges; relative controls send signed
  movement. External expression and auxiliary switches attach to the selected
  supported port mode. Calibration and pickup remain in the runtime.
- Render graphical display updates through the production font/raster path.
  Numeric and character displays need their own presentation and backend
  support when introduced; a generic text widget alone does not make the cntrl
  graphical application support a character display.
- Show relay/output indicators and connection state where those capabilities
  exist. A capability requiring software support must be reported as unsupported
  until implemented, rather than reduced silently to a generic button.
- Keep debug controls, MIDI logs and connection selection outside the emulated
  enclosure, so they do not alter its layout or claim firmware display windows.

## Clock, MIDI and storage

Provide real-time and manual clock operation through the same clock service.
Manual operation supports pause, advance and deterministic event replay. Record
input events with virtual timestamps; define their ordering at equal times and
drive the ordinary runtime polling order. Persist replay scenarios as TOML.
Supply a random seed when exercising random arpeggiator modes. Do not bypass
the production debounce, tap-tempo, clock, pickup or scheduling code.

Model each MIDI endpoint with input/output queues and connection state. Start
with injectable input and captured output, including channel messages, SysEx,
clock and other real-time messages. Log timestamps and logical endpoint identity.
Permit delayed or blocked acceptance so pending delivery, counter advancement,
note cleanup and pickup targets retain their real semantics. The virtual
destination's current state comes from accepted traffic or injected feedback,
not from a GUI slider writing directly into the runtime's output state.

Connect to real host MIDI ports as a subsequent part of the emulator work.
Port selection is explicit; loading a layout must not open or transmit to a
physical destination automatically. Keep host MIDI access behind the same
endpoint interface used by captured virtual traffic.

Map firmware filesystem services to a chosen session directory. Use real
compiled setups, font assets, songs, history and MIDI files through the shared
storage interfaces and policies. A directory represents an already available
filesystem; there is no SD formatting operation. Keep catalogue/layout files
separate from mutable emulated storage. Expose reset and reopening of session
storage explicitly so automated scenarios are reproducible.

## Implementation sequence

1. Add the layout model, TOML loader and binding validation. Resolve the first
   profiles' geometry and identity questions. Add complete MC6 Pro and MC8 Pro
   layouts under the Morningstar package; mark any provisional geometry.
2. Compose a native desktop cntrl backend using the existing services. Support
   loading a target, compiled setup and fonts; connect real input handling to
   display framebuffers and captured MIDI. Establish a headless path using the
   same composition as interactive operation.
3. Add the desktop frontend: scaled enclosure, displays, independent buttons,
   absolute/relative widgets, external control attachments and inspectors.
   Expose a `cntrl emulate` command using the project's Pydantic/Tyro pattern.
   Select the layout, execution target and session storage explicitly.
4. Add manual time, replay, blocked endpoint scenarios, screenshots and output
   trace comparisons. Exercise menus, page allocation, tap tempo, gestures,
   MIDI processing, text messages, counters, looper and arpeggiator.
5. Add selectable host MIDI connections and verify switching between the two
   graphical profiles without recompiling manufacturer-specific geometry into
   the generic frontend. Add further controller records/layouts as capabilities
   become available; add software only for unsupported features.

## Verification and acceptance

Test layout-to-component binding, resizing/hit testing, simultaneous holds,
release on focus loss, range handling and unsupported-feature reporting.
Exercise real shared C++ behavior with deterministic input scenarios; do not
add parallel frontend tests that merely duplicate the C++ state machines.
Compare actual rendered pixels and MIDI traces, with time controlled by the
scenario. Reuse the existing golden/fixture and sanitized native-test patterns.

Acceptance requires the same emulator application to load both initial profiles,
show all their declared controls/displays, run the shared application, and retain
independent session storage. A controller composed entirely of supported
components can be added through catalogue/layout data and explicit application
target bindings, without changes to generic emulator code. Unsupported features
produce a specific explanation, and the generic frontend imposes no MC6/MC8
input or display count assumptions. Verify macOS first and retain the project's
Linux/Windows boundaries in the host process, filesystem and GUI implementation.

The emulator does not establish electrical behavior, physical display quality,
USB/DIN latency, interrupt/DMA/watchdog behavior, power-loss durability, ARM ABI
or real RAM capacity, or bootability on unowned hardware. Host allocation limits
can exercise admission failures but cannot reproduce the ARM allocator or memory
layout exactly. Existing physical qualification remains necessary for those
properties.

## Implementation decisions

- Use PySide6 Essentials for the GUI and middy for explicit host MIDI access,
  installed through the optional `emulator` extra. Pin reccy and middy to Git
  revisions; do not use editable dependencies or sibling-directory paths.
- Keep the MC6 Pro's current usable viewports and create a provisional MC8 Pro
  profile with eight switches and four 160×128 virtual viewports. G/H select
  previous/next page independently. The catalogue's unresolved physical display
  dimensions are unchanged.
- Frame local process messages with a little-endian payload length and opcode,
  bounded to 65,536 bytes, and acknowledge each command. Display rectangles use
  the existing column-major RGB565 big-endian format. Input requests, display
  updates, accepted MIDI, relay states and errors share this communication path
  in interactive and headless operation. Bound response waits and terminate an
  unresponsive native process rather than leaving the GUI blocked indefinitely.
- Use existing CSTB compiler output and the production bundle/storage policies,
  including cross-setup references, history, fonts and movies. No alternate
  emulator setup format or raw-CSW1 compatibility path is introduced.
- Apply equal-timestamp events in TOML order. Record inputs and the random seed
  for export through Save replay. Use the same initial target, bundle and session
  state when replaying a recording.
- Route incoming MIDI only through explicit setup `[midi]` routes. Do not infer
  the physical adapter's legacy USB/DIN wiring, and do not echo injected input.
- Original firmware execution and USB HID keyboard output remain unsupported
  platform features. Other unsupported controller component kinds are reported
  explicitly; the generic layout records retain their hardware-independent
  shape for later execution adapters.

## Additional work beyond the prompt

None.
