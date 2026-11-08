# Separate the portable application from the pedal implementation

## Objective and scope

Move all reusable Python and C++ behavior out of the MC directory. The portable
application owns musical behavior, UI policy, layout, rendering, asset formats,
storage protocols and scheduling. The pedal implementation supplies hardware
capabilities, low-level I/O, CPU startup, memory placement and its toolchain.

The existing pedal is the only hardware target for this project.

A future developer porting cntrl to another CPU and pedal should implement the published
adapter API, describe the hardware and compile the existing application. They
should not copy pages, rewrite the event loop or edit portable source files.

This document is a plan only. Implement it in separately reviewable commits.
Preserve current behavior throughout extraction. Do not flash hardware as part
of the split without a separate instruction.

Additional work beyond the prompt: None.

## Dependency rule and the one target selection point

Dependencies point inward: a board implementation imports/includes the generic
API and supplies implementations. Generic code never imports, includes, discovers
or constructs a particular board implementation by name. This applies to Python,
C++, tests, generated code and build recipes outside the adapter directory.

Use one explicit target selection on the command line, conceptually:

```sh
uv run cntrl build --target-dir cntrl/maker/morningstar/mc --output build/pedal
```

The command above is proposed. Its argument is the only place the workflow names
the implementation. Give the build a single direction of control:

1. The generic CLI resolves the supplied directory and output directory. It calls
   the executable at `<target-dir>/build` with absolute paths to the portable
   firmware sources and the output directory. A failed build returns a nonzero
   status. The CLI never imports adapter Python.
2. That executable owns compiler selection, flags, linking, startup files and
   native asset conversion. It validates the target using the existing
   `models.Target` schema and produces the firmware image, a host-readable target
   description and any required font resources in the output directory. Generate
   firmware capability constants from the same validated values. Keep pins and
   linker details private to the executable.
3. Run the ordinary setup compiler and copy commands using the exported target
   description. Resolve its resource paths relative to the export. As a build
   acceptance check, relocate the output and compile a setup with the original
   adapter sources and SDK unavailable.

Use this same executable convention for a new CPU. The CLI needs no per-board
registration, package discovery or hard-coded default.

## What is coupled today

- `cntrl/patch_switcher/display.py` imports the adapter's font preparation code.
  `compiler.py` imports font validation and movie preparation; `transport.py`
  imports the adapter's splash asset inspection and encoding. Remove every one
  of these reverse dependencies.
- The adapter's `firmware/patch_switcher/target.cpp` contains roughly 1,500 lines
  mixing drivers with app construction, menus, command routing, song capture,
  state operations, polling order, status reporting and UI ownership.
- `pages.h`, `renderer.h` and `show_graphics.h` contain reusable page logic,
  drawing, text layout and animation, coupled to display-library classes and
  target functions. Renderer arrays and coordinates assume three 160 by 128
  displays.
- Font preparation mixes shaping and rasterization with library-specific C++
  font output and paths into the locally cached toolchain.
- Splash encoding/player code assumes an outer-screen group, a mirrored center
  screen and fixed dimensions. Those are physical arrangements, not generic
  animation semantics. Include the newly added splash/idle movie lists in this
  split, rather than moving only the older single-asset implementation.
- Already-generic code also embeds target assumptions: `ABCDEF` button masks,
  three-window layout records, fixed TRS jack order, and the 512 KiB RAM2 model.
  Removing maker names alone would leave these dependencies intact.

## Destination and ownership

Use the existing top-level `cntrl/` Python package and `firmware/` C++ tree. Avoid
creating a second application alongside `cntrl/patch_switcher`.

| Existing responsibility | Portable destination | What stays in the adapter |
| --- | --- | --- |
| `font_assets.py`, `sparse_fonts.py`, `bitmap_raster.py` | `cntrl/fonts/`: shaping, glyph runs, rasterization, packing, validation | Selection/import of any driver-native legacy font sources |
| `build_bitmap_fonts.py`, `build_font_metrics.py` | Portable metrics and generic bitmap generation in `cntrl/fonts/` | Parsing/emitting vendor-library font structures and locating toolchain files |
| `splash/asset.py`, `movies.py`, generic CLI transfer operations | `cntrl/animation/` plus the existing generic CLI/transport | Panel wiring, physical mirroring and controller byte order |
| Intel HEX parsing/encoding and image checksums | `cntrl/firmware/` | CPU vector tables, flash addresses, boot image composition and device validation |
| Storage report parsing and reusable MIDI qualification logic | `cntrl/patch_switcher/` and generic test helpers | Device discovery/identity checks and physical qualification scenarios |
| `pages.h` and app/menu coordination from `target.cpp` | `firmware/include/cntrl/` and portable implementation files where useful | Capability providers and device-only operations |
| `renderer.h`, `show_graphics.h`, portable parts of `graphics.h`, `panorama.h`, `pixel_throb.h` | Generic UI/rendering components under `firmware/` | Display controller commands, DMA, cache maintenance, SPI/pin ownership |
| `font_asset.h`, font cache, generated glyph tables | Generic font decoding/cache and board-neutral font assets | Linker placement and library-native font conversion, if still needed |
| `font_storage.h`, `storage_listing.h`, `midi_files.h`, `startup_counter.h`, expression calibration | Generic asset/file policy, listing, recording names, durable counter and calibration logic | Filesystem bindings, persistent-device access and ADC acquisition |
| `splash/animation.h`, `color_cycle.h`, `receiver.h`, `router.h`, `session.h` | Generic playback, color policy and transfer/routing components | Hardware output and byte transport bindings from `board.h` |
| `target.cpp` initialization and polling | Portable application composition and scheduler | Small construction/bootstrap entry point and hardware polling primitives |
| Hardware catalogue, pin maps, recovery analysis, stock-firmware binaries and boot-switch assembly | Remain in the adapter | Entirely device-specific material |

Treat these as responsibility splits, not blind whole-file moves. Do not move
Arduino, SD-library or display-library dependencies upward with the logic.
Move generic tests with their implementation; leave physical wiring, recovery
and driver tests with the adapter. Keep shared font licenses with their assets.

## C++ API

Evolve the existing `Hardware`, `Storage`, `SetupStorage`, MIDI sinks and file
interfaces. Replace their mixed responsibilities where necessary; do not add a
parallel hardware abstraction that leaves the old path running.

### Hardware description

Provide a board-neutral description shared with the host compiler: stable logical
input/output/display identifiers, labels, supported operations, screen bounds,
pixel format, expression ranges, and memory regions/budgets. A capability limit
must state whether it is a format limit or a hardware limit.

Physical pins, bus instances, polarity, panel orientation and ADC channels belong
to the adapter. Logical left/right roles and app window assignments belong to
generic layout/configuration and map to the described displays. Do not require
six buttons, three screens, four configurable jacks or a particular RAM bank name.

### Minimal hardware services

| Service | Contract |
| --- | --- |
| Clock and raw inputs | Monotonic timestamps with documented wrap handling; sampled switch states and expression samples using logical IDs. Core owns debounce, fast-switch behavior, gestures and calibration policy. |
| MIDI and keyboard I/O | Message acceptance/backpressure, incoming events, completion/idle state and logical endpoints. Core owns parsing, state tracking, routing, scheduling and patch operations. |
| Display output | Logical pixel regions, readiness/completion and a defined buffer lifetime. Core owns text, drawing, clipping, layout, transitions, dimming and animation policy. |
| Storage and persistence | Existing bounded read/write/seek/sync/commit needs, directory iteration and capacity reporting. Core owns paths, formats, bundle transactions, history and recording names. |
| Host byte stream | Nonblocking input/output and backpressure. Core owns framing, resumable sessions and command dispatch. |
| Device controls | Backlight/output levels, supported jack modes and optional alternate-firmware handoff. Advertise unsupported capabilities explicitly. |
| Resource information | Available memory regions and budgets, allocation constraints, boot counter storage and usage measurements where available. Unknown measurements remain identified estimates. |

Keep interfaces limited to operations actually needed by current code. Specify
which calls can block, their work bounds, ownership, failure behavior and whether
they may retain pointers. No driver object types, Arduino globals, linker symbols
or CPU SDK types appear in public portable interfaces.

The renderer submits a region only when accepted; its pixels remain valid until
completion. The adapter owns transfer alignment, cache flushing and pin release.
The generic scheduler gives due input scans priority before another display
submission. A slower target can declare a different feasible scan interval;
retain the current 1 ms behavior for the existing adapter. No GPIO/pin-mux policy
belongs in the core scheduler.

### Application versus hardware

The portable application owns controller/document instances, pages, menu
focus, switch dispatch, song sources, MIDI file operations and command routing.
Calls such as page browsing and app configuration become application operations,
rather than pretending to be hardware services. Status pages consume a portable
status snapshot instead of reading CPU heap/linker symbols themselves.

Keep alternate-firmware confirmation, pending-operation gating and continuation
policy generic. Actual boot addresses, record placement, firmware validation and
the handoff implementation stay local. Another target may omit this capability.

## Python and asset API

The compiler receives a generic target description explicitly. Font preparation,
layout and movie conversion operate on that description plus source assets;
they never reach into an adapter source tree or a cached compiler installation.
The board-local build entry point prepares any native font inputs before invoking
these functions.

Extend `models.Target` only when a current consumer needs another capability.
For each value, identify the host validator and firmware consumer. For example,
compile a button binding from its label to its declared input index. The adapter
reports that index when the switch changes. Page prompts use the label bound to
the action, so a confirmation screen and its dispatch cannot disagree.

Separate the logical movie frame from its transfer encoding. Dimensions, frame
durations and logical screen assignments belong to the portable representation.
An adapter can precompute physical mirroring and byte order during the host
build when needed to meet playback timing. Measure CPU work, bus bytes and peak
buffer use before replacing the current precomputed rows with runtime conversion.

## Implementation sequence and review gates

Budget roughly 15 to 25 focused commits across these eight stages. Each stage
should end with a working path through the existing application.

1. **Capture a baseline.** Record firmware size and memory headroom alongside
   existing document, bundle, font and movie fixtures. Trace one switch press to
   its outgoing MIDI message and one Status page through pixel submission. Mark
   where each path reads physical IDs, time, geometry and memory limits.
2. **Prove the build boundary.** Implement the build entry-point contract above
   with the existing adapter. Build its firmware, relocate the exported target
   description and assets, then compile a setup using only those exports.
3. **Remove Python reverse imports.** Extract font preparation and movie handling
   one asset family at a time. Exercise normal compile, copy and download paths
   using exported resources. Check current-target asset bytes before changing
   their encoding. Device-specific SDK font parsing stays in the exporter.
4. **Prove a complete UI path.** Bring Status through page construction, logical
   window assignment, text measurement, clipping and display submission. Use existing renderer
   tests to check each submitted region against the declared bounds. Validate requested window counts against
   the target at compile time. Introduce display services as this path needs them.
   Move the remaining pages, transitions and movie playback through the same path.
5. **Extract input dispatch and runtime coordination.** Drive a bound switch
   through sampling, debounce, gesture recognition, patch execution and MIDI output
   in the native tests. Advance a fake clock across a scan deadline while display
   I/O is pending. After transfer completion, assert the scan and due MIDI work
   precede the next display submission. Then move menu ownership, song resolution,
   app lifecycle and command routing out of the board poll loop.
6. **Extract persistence and budgeting.** Move counter, calibration, file naming,
   font and listing policies behind the storage primitives. Pass the target's
   capacity and reserved bytes into both bundle planner and firmware validator.
   Exercise the same setup collection under budgets selecting static residency
   and LRU. Keep tests for interrupted saves, per-setup history and backup/restore.
7. **Finish capability-dependent records.** Remove remaining physical endpoint
   orders, switch masks and fixed window counts from models, packers, firmware
   readers and prompts. Change each producer and all its consumers together.
   Preserve limits imposed by MIDI or by a documented file format. A change
   to either needs a reason independent of the hardware's current dimensions.
8. **Qualify the porting contract.** Build and run the portable suites from a
   source tree containing only the core, its assets and its native tests. Build
   the real image and compare with the baseline. Document the services a port
   implements and the build outputs it must supply. Audit remaining board files
   against the ownership table.

Keep format decisions separate from mechanical moves. Before changing a record,
try expressing the capability with its existing encoding. If that fails, identify
the affected reader, writer and persistent data, then specify how to recover an
edited setup, history entry or captured song from the old firmware. Rebuilding
authored TOML alone cannot recover pedal-only edits. Verify the agreed recovery
path with a saved fixture before publishing a format change.

## Verification and acceptance

- Portable Python imports and tests run with the adapter directory and its
  toolchain unavailable. Portable C++ tests compile with a standard host compiler,
  without Arduino, display SDK or SD-library headers.
- Dependency checks reject maker/board paths and names in portable imports,
  includes, build recipes and generated source. Scan for disguised coupling too:
  CPU registers, linker symbols, platform memory attributes and fixed geometry.
  No allowlist of MC references in generic code is permitted.
- In relevant existing tests, vary an individual capability such as viewport
  width or available memory and check that layout or residency follows it.
- Preserve current render fixtures and visible layout through the configured
  current-target geometry. Test region buffer lifetime/backpressure, clipping,
  input priority, gesture timing, expression mapping and MIDI ordering.
- Retain bundle static/LRU, power-loss save, history reindexing and recording/playback
  tests. Check representative fonts and splash/idle movie lists end to end with
  fake I/O, including orientation mapping at the hardware boundary.
- Build the actual CPU image after relevant steps, checking flash, stack allowance,
  heap and display buffers. Extraction must not silently add framebuffers, duplicate
  caches or unbounded work. Memory placement attributes remain adapter-owned.
- The final local directory contains drivers, capability data, startup/linker/build
  files, device-specific tools/assets/tests/docs and a small composition entry point.
  It contains no reusable layout, page behavior or application scheduler.
- A porting guide lists the required services, capability fields, build entry-point
  contract, optional features and validation procedure. A new port requires no
  changes to portable Python/C++ code and no registration by maker name.

Run the repository-required Python checks when implementation changes Python/data,
plus focused native tests and CPU builds for C++ changes. Hardware appearance,
timing and durability qualification remain separate from automated evidence.
