# Implementing a target

The Python compiler and the C++ runtime operate on declared capabilities and
logical IDs. An implementation supplies physical I/O, clocks, file handles,
font export, memory placement, the compiler/linker invocation and the uploader.
Page behavior, layout, rasterization, input routing, MIDI scheduling, setup
residency, recording, animation and persistence policy remain in the shared
packages and headers.

This contract can be implemented against the existing test services. A second
physical device or an additional production backend is not required to change
or test the shared code.

## Build boundary

Select the implementation once:

```sh
cntrl build --target-dir path/to/implementation --output build/device
```

Targets that support managed firmware installation also supply `flash_entry.py`.
`cntrl flash --target-dir path/to/implementation --output build/device --port PORT`
first completes the normal build and then executes that entry with the selected
interpreter, `--directory` pointing to the build export and `--port` identifying
the device. The shared source root and temporary-directory environment are the
same as for the build. The target owns backup, bootloader entry, upload and
post-upload verification. `--legacy-usb` is forwarded explicitly for upgrading a
device still using the earlier text-framed cntrl protocol; binary USB remains
the default. A target without a flash entry is rejected before building.

`cntrl.build.run` executes the implementation's `build_entry.py` using the current Python interpreter, with
`--source-root` and `--output` arguments. `--source-root` identifies the shared
source checkout and can be supplied to the public build command when building
against a different checkout. The implementation must use that source root for
shared includes and sources, rather than a sibling-path convention.

`cntrl build` creates `tmp/` inside the output directory and sets `TMPDIR`, `TMP`,
and `TEMP` to that directory for the implementation and its child tools. Build
scratch files stay with the generated output; the calling process's environment
is unchanged.

The implementation performs its own compilation and exports:

| Artifact | Purpose |
| --- | --- |
| `build.toml` | Relative `image` and `target` paths |
| Image named by `image` | Complete image for the implementation's uploader |
| `target.toml` | Valid `cntrl.patch_switcher.models.Target` description |
| `fonts/` | Portable font sources referenced by that description |
| `target_capabilities.h` | C++ capability data generated from the same target model |

The common build wrapper requires the manifest's image and target to be files
inside the output directory. Font paths in the description are resolved relative
to that description, so an export remains usable after relocation. The generated
header is for the implementation's composition layer; shared runtime code takes
capability views through its service interfaces.

Generic setup compilation and copying consume these exports. They do not discover
an implementation package, select a processor, invoke a device uploader or flash
a controller. Keep the toolchain, startup image construction, linker script,
image checks and physical uploader with the implementation.

## Capability description

The `Target` model is authoritative for both host compilation and generated C++
data. Its principal fields are:

- `memory.capacity` and `memory.runtime`, in bytes, plus each app/page's declared
  `ram2_bytes` use. The runtime allowance includes platform and shared-runtime
  overhead, including allocations outside `MemoryBuffer`.
- `displays`: logical `id`, human `label`, `width`, `height`, `gap` and
  `corner_radius`. IDs are unique printable ASCII strings of at most 31 bytes.
- `buttons`: ordered logical input IDs, with `controls` assigning navigation,
  confirmation, back, page, tap and application actions to those IDs. Input IDs
  are unique printable ASCII strings of at most 31 bytes; `+` is reserved for
  compound-input authoring. `scan_interval` is a duration. `usb_block_size` is the USB transfer payload
  size in bytes (default 4096); rebuilding regenerates the firmware buffers.
- `midi_endpoints`: `name`, `label`, `route`, `transport`, input/output capability
  flags and an optional configurable-port slot. `default_midi_output` names the
  output used when a play-file operation omits its output list.
- `trs_ports`: configurable-role capabilities keyed by the declared connection
  name. An endpoint's `port` binds that name to a serialized role slot; names and
  route positions have no implied connector or role meaning.
- `fonts`, `font_sources`, `ui_fonts`, optional `animation` geometry, supported functions,
  text characters and compiler resource limits.

For example, this is an endpoint fragment, not a complete target description:

```toml
default_midi_output = 'synth'

[[midi_endpoints]]
name = 'synth'
label = 'Synthesizer'
route = 0
transport = 'physical'
input = true
output = true
```

Routes form a dense permutation from zero through endpoint count minus one.
Transport is `physical`, `usb-device` or `usb-host`; these roles can appear at
any route index. Recording inputs default to all entries whose `input` flag is
set. `Target.outputs` is computed from output flags, rather than configured
separately. An internal forwarding connection can have both selectable flags
clear while retaining a queue and transport identity.

## Font provider

UI font IDs are a portable product contract, independent of any display library.
`Target.ui_fonts` is a required list of available names; it is not a second wire font table.
The classic face is required for management/error text. Default page settings also
require CourierPrime and AtkinsonNextSemiBold unless alternatives are authored.
The canonical IDs are:

| Name | ID |
| --- | --- |
| classic | 0 |
| Arial | 1 |
| ComicSansMS | 2 |
| StillTime | 5 |
| CourierPrime | 8 |
| CourierPrimeBold | 9 |
| AtkinsonNextSemiBold | 16 |

The default outline sources for AtkinsonNextSemiBold and the CourierPrime faces
are bundled with the shared font tooling. Optional classic, Arial and ComicSansMS
faces can be supplied as exported neutral CFN2 sources or rendered from outline
sources. `cntrl.fonts.ui.prepare(name, sizes, path=None)` prepares hinted outline text as
ASCII CFN2. The bundled defaults cover AtkinsonNextSemiBold and the CourierPrime
faces; preparing classic requires an explicit outline source.
`ui.embedded_header({name: data})` generates an `EmbeddedFont` array. The optional
`CNTRL_UI_FONT_STORAGE` macro lets the implementation choose its storage placement.
`Target.font_sources` accepts CFN2 sources or authoritative TTF/OTF outlines for
all requested scales, with paths relative to the target export.
An implementation may bind `ui::Fonts` directly or use `ui::AssetFonts` with
`ui::EmbeddedFont` records. Advertise only faces and sizes the runtime provider
can actually serve; an implementation's private font identifiers must not leak
into the canonical UI IDs.

## Runtime services

The main implementation bindings are declared in these headers:

| Header | Contract |
| --- | --- |
| `cntrl/hardware.h` | Application I/O and capabilities; setup/revision storage interfaces |
| `cntrl/ui/display.h` | Display geometry, readiness, submission and optional scrolling |
| `cntrl/ui/fonts.h` | Portable font lookup and glyph geometry |
| `cntrl/ui/controls.h` | Input roles, labels and logical event routing |
| `cntrl/ui/application.h` | Application coordination and `ApplicationServices` |
| `cntrl/ui/page_host.h` | Generic page lifecycle and its compile-time platform service contract |
| `cntrl/filesystem.h` | File, directory and filesystem primitives |
| `cntrl/midi_outputs.h` | Borrowed endpoint descriptor views |
| `cntrl/midi_bridge.h` | MIDI delivery queues and the central state-tracking sink |
| `cntrl/midi_runtime.h` | Fair ingress service, clock/file/utility scheduling and diagnostics |
| `cntrl/usb_midi_forward.h` | Hosted-device publication lifecycle and USB MIDI descriptor extension |
| `cntrl/runtime.h` | Cooperative scheduling order |
| `cntrl/animation/animation.h` | Animation source and logical output contracts |

`Hardware::midi_outputs()` returns `midi::Outputs`, a borrowed descriptor view.
Construct `midi::Bridge` with that same view. The implementation decodes external
packet framing and calls `forward_packet(route, packet, now)` for an explicit
logical destination. It implements actual writes behind the MIDI sink and
returns success only when the complete packet has been accepted. Successful
`Sink::submit` calls update MIDI state centrally; callers do not maintain a
second note/controller state table.

`Hardware::input_count()`, `input_label(index)` and `control_input(role)` expose
the declared input order and control roles. The serialized `CINP` IDs must match
those labels exactly. The declared back control is reserved from switch-patch
assignments; its label or numeric position is not inferred.

`midi::Runtime` takes nonblocking event services and owns the source cursor,
per-source item limits, aggregate input time budget, clock ownership, utility
handoff, delivery-before-file-work order and recording-failure diagnostics.
The default input allowance is 64 events per source and 180 microseconds in
aggregate; the constructor can choose a different allowance. Source count comes
from the service, rather than a fixed USB/serial split. `InputEvent` contains a
recording-input ID, optional forwarding route, clock-source ID, optional cable-test
input and either one byte or a decoded `Packet`. The implementation owns native
packet/cable decoding and slices retained native SysEx buffers into bounded events.
Core code does not retain a pointer to that native buffer.

`InputEvent::relay` requests explicit hosted-device forwarding independently of
the physical-interface switch. Its destination uses the existing bridge queues
and central sink/state path. `Bridge::discard_forwarded` clears forwarding work
without cancelling explicitly owned pedal operations. Device replacement clears
the affected destinations entirely.

`UsbMidiForward` accepts a connection generation, current product name, setup
enablement and a busy flag. Its services clear replaced devices, stop forwarding,
detach, publish and attach. The implementation supplies native descriptors and
controller operations. A short idle grace allows the final configuration reply
to drain; a 250 ms detached interval publishes the latest connected device.
Polling never blocks. Product strings are consumed synchronously by `publish`.

`ApplicationServices` supplies application time, blanking, output-idle state,
song-transfer state and switch-editor entry/shift operations. `PageHost<Platform>`
uses generic values such as `Pane`, `KeyboardReport`, `midi::Outputs`,
`PageSettings`, `animation::Session` and `ExpressionPedals`. The platform supplies
its clock methods, runtime state, MIDI bridge, availability queries, brightness,
optional keyboard delivery and physical configurable-port/expression operations.
The complete method list is the compile-time contract in `page_host.h`; page
selection, rendering, prompts and transitions are implemented there and in
`pages.h`, not duplicated in a platform class.

A composition layer passes generic service objects to the shared classes:

```cpp
cntrl::midi::Bridge bridge(endpoints);
cntrl::ui::Renderer renderer(displays, fonts, work, maximum_area,
                            cache, previous, total_area);
cntrl::ui::PageHost<PlatformServices> pages(platform, application, documents,
                                          displays, fonts, renderer, controls);
cntrl::CooperativeRuntime runtime;
```

Here `endpoints` is a `midi::Outputs` view, `displays` implements `ui::Display`,
`fonts` implements `ui::Fonts` and `controls` is `ui::Controls`. The platform's
keyboard boundary accepts `KeyboardReport` through `keyboard_report`, rather
than exposing a platform-specific report structure to page logic.

The implementation's loop calls `CooperativeRuntime::poll(services)`. Its order
completes display work, services keyboard state, performs a due scan, advances
applications, sends MIDI, processes bounded communication work, advances storage
and transfers, samples expression inputs, and finally services navigation/display.
Use wrapping unsigned timestamps and the units required by each interface;
page services expose both milliseconds and microseconds.

## Display ownership and scheduling

`ui::Display` exposes `count()`, `viewport(screen)`, `poll()`, `ready()`,
`failed()`, `priority_due()` and region-based `submit(...)`. The implementation
maps logical coordinates to its physical surfaces and owns orientation, wire
byte order and transfer mechanics. RGB565 display submissions use big-endian
pixel bytes in column order.

A successful submission borrows its pixel storage until `ready()` becomes true.
The implementation must not report readiness before the consumer has finished
reading those bytes. Rejection retains neither bytes nor ownership and must
leave submission state unchanged. The renderer waits before reusing its transfer
buffer. Optional ring-column and scrolling methods can keep their default
unsupported behavior; ordinary region submission remains the rendering path.

`priority_due()` gives a due input scan priority between display transfers.
Finishing an in-flight transfer must not immediately start another one ahead of
that scan. The renderer also observes this priority before preparing more display
work. Backpressure is represented by readiness or rejection, not a busy wait in
shared rendering code.

The renderer receives caller-owned work, current-cache and previous-cache pixel
buffers. Work capacity must cover the largest display area, and each cache must
cover the sum of display areas. Its retained transfer buffer is sized from the
largest display height and its bounded column batch. Keep these buffers alive
for the renderer's lifetime and place transfer-visible storage according to the
implementation's bus requirements.

## Files and persistence

`files::File` is an owned handle object supplied by the implementation. Its
`open`, `close` and `opened` operations accompany offset-based `size`, `read`,
`write` and `sync` operations. Reads and writes return a Boolean for exact
completion, not a partial-byte count. Opening modes are:

| Mode | Required behavior |
| --- | --- |
| `read` | Open an existing file for reading |
| `create` | Open/create read-write without truncating existing contents |
| `exclusive` | Create read-write, failing if the name already exists |
| `truncate` | Open/create read-write with empty contents |

The caller supplies bounds and owns buffers. A failed or short read/write must
return false. `sync()` provides the durable completion point required by commit
policy; closing a handle does not replace that contract.

`files::Directory` provides incremental `open`, `next`, `close`, the last entry's
size and failure state. `next` fills the caller's name buffer without overflow
and reports whether the entry is a directory. `files::FileSystem` supplies
existence, directory creation, rename, removal, availability and directory-handle
creation. File and directory objects must outlive policies retaining references
to them. Do not share a handle between independently active policies unless the
policy explicitly coordinates that use.

Shared classes own naming, two-slot commits, history selection, checksum and
completion-marker validation, immutable asset publication, latest-recording
selection and calibration records. Bind them to file primitives rather than
reimplementing those policies. Calibration records encode their fields explicitly
and retain their generation/checksum scheme across processor byte orders.

Poll-sized work has explicit bounds: animation playback reads/submits at most
320 bytes per call; animation commit verification processes at most 512 bytes;
revision work processes one directory entry or at most 512 file bytes per tick;
MIDI bridge service attempts at most one packet per destination; MIDI-file
indexing processes at most 32 work items per poll and recorder draining at most
16 queued events. `CooperativeRuntime` admits at most 512 raw serial bytes and
128 command bytes per call. These limits do not make a platform's synchronous
storage primitive nonblocking. Cold bundle validation and initial residency
planning can perform multiple reads and must be included in startup qualification.

The startup counter also uses `File::read_some` and `write_some`. Implement these
with actual completed byte counts, `-1` for an I/O error and `position_error`
(`-2`) when seeking to the requested offset fails. The counter distinguishes
short journal operations from positioning failures. Its reset command requires
`resize(0)` to truncate the open journal before rewriting and syncing it; return
false if truncation fails. The default counted methods support exact-only file
implementations, but cannot preserve a device's more detailed short-I/O result.

Expression samples use a portable integer range of 0 through 1023. Convert the
native ADC range in the implementation. `ExpressionPedals::poll` requests three
phases: establish low bias, read low and establish high bias, then read high and
release bias. The declared rest and bias intervals allow settling without a
blocking delay. A negative sample reports an unavailable input. The core owns
connection detection, calibration, smoothing, curves and MIDI value mapping.

## Memory and placement

Initialize `memory_budget` from the available linked heap before constructing
or loading budgeted resources. `MemoryBuffer` tracks documents, font assets and
transfer allocations that use it. Include descriptor-sized queues, renderer
state, vectors, platform services, stack requirements and other allocations in
the declared runtime allowance and in measured image qualification.

`total_ram2_needs()` retains its established API name for the non-setup memory
estimate. Free setup capacity is target capacity minus runtime/app/page/metadata
needs and the configured memory margin. Full animation and MIDI-file payloads
remain on storage. Animation owns one retained 320-byte packet; MIDI-file readers
allocate their 512-byte caches lazily. MIDI note/controller state allocates only
used outputs, channels and values.

If all compiled setups fit, the bundle prepares a packed resident table. Otherwise
it uses a byte-limited LRU cache and a host-computed prefill set. Setups are indexed
by the sorted bundle name table; human-readable names remain available separately.
Page estimates default to zero, and the budget uses the largest active setup's
combined app/page demand.

Buffer placement, alignment, executable sections and linker reservations belong
to the implementation. It may define the optional `CNTRL_FLASH_ROUTINE(name)`
placement macro before including shared headers. Shared code must also compile
when that macro is absent.

## Formats and conversion

Storage reporting uses `CNTRL STORAGE CAPACITY_BYTES USABLE_BYTES FREE_BYTES
ALLOCATION_UNIT_BYTES`; unknown free space is `-`. An acknowledged scan may emit
optional `CNTRL DETAIL KEY_HEX VALUE_HEX` rows before its directory entries.
Details are implementation-owned UTF-8 strings; card identity and filesystem
classification are outside the shared parser. `StorageListing::begin` borrows
the detail array and strings until completion or cancellation. Entry IDs and
the final `CNTRL STORAGE END COUNT` count only directory entries. Storage can
use any filesystem and need not provide device identity or free-space counts.

The current setup format is 35. Important portable sections are:

- `CLAY` version 2: explicit logical window IDs and a priority permutation per
  app, followed by the ordered page cycle. There is no implied display count.
- `CEND`: endpoint names, labels, routes, transport/capability flags and explicit
  configurable-port slot bindings.
- `CINP`: ordered logical input IDs. Switch gesture, hook and shifted-action
  records are indexed by switch-record slot; the switch record retains its
  physical input index.
- `CNSA` version 2: animation dimensions, logical stream count, pixel format,
  frame count, total size and per-frame durations. Stream transforms are supplied
  explicitly when preparing assets; output grouping belongs to the implementation.
- `CFN2`: portable font sources. Prepared glyph assets retain their checksummed
  storage and portable glyph metrics.

Existing protocol limits are sixteen MIDI routes, eight physical input IDs,
five configured switch slots and four configurable-port/expression slots. Layout
supports at most four patch-switchers, 36 named pages and 255 declared windows; setup/resource
limits are declared separately by `Target`. These are format/product limits,
not processor pins or implied transport positions.

Host tools and firmware accept only the current setup and animation formats.
There is no format migration. Rebuild outdated setups and animation assets from
their source files when a format changes. Backups retain the saved bytes;
restoration requires data in the current format.

Qualify the result with the shared Python tests, sanitized native C++ tests,
an isolated implementation build, image/memory inspection and an explicit backup,
upload and read-back check on the existing device. Native tests use
service fakes; they do not establish physical timing, visual quality or wiring.

## Logical input pipeline

Use `cntrl::ButtonState` from `cntrl/buttons.h` and
`ui::InputCoordinator` from `cntrl/ui/input.h`. Supply one retained button state
for each declared input, the generated `ui::Controls` descriptor, the active
`Document` object, `Application`, and `ApplicationServices`. These references and
the state storage must outlive the coordinator. The configuration reference is
to the stable document object, so loading another setup changes the configuration
seen by subsequent scans.

The implementation converts its electrical sample into an active-high `uint8_t`
mask: bit `i` means that declared logical input `i` is pressed. Register positions,
pin polarity and bus acquisition stay in the implementation. Call
`InputCoordinator::scan(mask, now_ms, services)` after acquiring that sample.
Core code selects configured debounce or its 15 ms default, applies
`fast_switches`, retains settled state, computes held inputs and routes events.
Do not debounce the electrical sample a second time.

The input services are:

| Method | Required result or operation |
| --- | --- |
| `debounce(input)` | `std::optional<unsigned>` override in milliseconds; no override returns `std::nullopt` |
| `keyboard_release(input)` | Release the shortcut owned by this input |
| `panic_open()` | Whether the optional panic utility owns input |
| `panic_release(now_ms)` | Release its confirm gesture |
| `animation_controls(held, now_ms, replay)` | Call the shared animation session's controls method; return whether it consumed input |
| `diagnostic_controls(events, held, now_ms)` | Optional physical diagnostic handler; return false when inactive |
| `visit()` | Pointer to the shared `FirmwareVisit`, or null when unsupported |
| `actions()` | The action service consumed by `ControlRouter` |

The coordinator always performs confirm-release cleanup before an animation or
other focus owner can consume the frame. It also owns menu/setup/song polling,
release-to-unlock behavior, animation replay eligibility, firmware-visit focus
and the choice to dispatch through `ControlRouter`. The diagnostic callback can
inspect the `controls.count` event entries; event bits are `press_event`,
`release_event`, `hold_event` and `double_press_event` from `cntrl/buttons.h`.

The router's action service binds `controls_locked`, `menu_open`, `setup_open`,
`songs_open`, `switches_open`, `switch_button`, `invoke(Operation)`, `browse`,
`midi_reserved`, `keyboard`, `expression`, `port`, `clock`, `has_controller`, `controller` and
`tap` to the shared application and page objects. Its completion path additionally
uses `leave_switches`, `abandoned`, `patch_pending` and `error`. Call
`InputCoordinator::finish(actions)` during the runtime's navigation phase;
that path owns deferred editor exits and pending menu/performance operations.

Construct `CooperativeRuntime(scan_interval_ms)` with the generated interval.
It owns the wrapping scan deadline and last-scan timestamp. The runtime calls
`services.scan_available()` before a due acquisition, then calls
`services.scan(now_ms, gap_ms)`; the first gap is zero. The implementation may
record the gap for diagnostics, acquire and map its raw sample, and invoke the
coordinator. `scan_due(now_ms)` is also the display adapter's priority query.
Completion of a pending display transfer must release the shared bus before a
new transfer is considered. A due scan then precedes application ticking, MIDI
delivery, command/storage work and the next display phase.

Physical indices and application actions are separate. `Controls.bindings[i]`
maps a declared input to its label and logical application action. The six
patch-switcher actions are a software protocol, not six required switches or a
physical label convention. Menu and page roles also come from `Controls`;
unused roles have the `Controls::absent` index. Do not infer navigation, back,
confirmation or application actions from a label or numeric input position.

## Application hardware facade

An implementation can derive from `ui::ApplicationHardware<Platform>` instead of
writing its own page-facing `Hardware` forwarding methods. Its public
`page_host()` method returns the retained `ui::PageHost<Platform>&` that was
constructed with the same platform, application, documents, display, fonts,
renderer and controls. Construct physical resources and caller-owned pixel
buffers before constructing the host; keep all of them alive for its lifetime.

The facade connects `Hardware::show`, app/page configuration, refresh, page
browsing, tap tempo, transitions, brightness, pulse, animation configuration and
error presentation to the shared host. It also supplies common keyboard,
clock-control, configurable-port and expression-page entry points. Implement
the physical `Hardware` services and the platform methods consumed by
`PageHost`; do not copy the facade or page behavior into the implementation.

Management presentations describe logical context, selection and detail. The
shared presentation helper preserves their three-window arrangement, combines
their text when fewer windows exist, and spreads their roles when additional
windows exist. Individual page geometry comes from the destination viewport.
Configured pixel values, including tap-flasher diameter and annulus, retain
their physical pixel units rather than being stretched with the layout.
