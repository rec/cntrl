# Separate the portable application from the pedal implementation

## Objective and scope

Move all reusable Python and C++ behavior out of the MC directory. The portable
application owns musical behavior, UI policy, layout, rendering, asset formats,
storage protocols and scheduling. The pedal implementation supplies hardware
capabilities, low-level I/O, CPU startup, memory placement and its toolchain.

A developer porting cntrl to another CPU and pedal should implement the published
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

This is a proposed command, not an existing CLI contract. It is the sole place in
the executable workflow that names that directory. Generic orchestration receives
an opaque target directory, reads its capability description and invokes its
local build entry point using the same relative entry-point contract for every
target. The entry point supplies local source paths, toolchain flags and generated
capability data. Subsequent compile/copy operations use the produced generic target
description, not a second reference to the source adapter directory.

Do not add a maker registry, hard-coded default target, target-name switch,
conditional import, source-path search or alternate legacy entry point. References
within the adapter to its own local files are implementation details. This plan's
inventory names existing files to explain their migration; those documentation
references are not executable dependencies.

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

The portable application owns controller/document instances, pages/apps, menu
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

Use the existing Pydantic model and CLI patterns. Define only the capability and
build request/result records needed for this boundary, using TOML for persisted
descriptions. Keep the host and firmware view of capabilities consistent through
generated generic data. Report unsupported requirements during compilation.

Use a portable glyph/bitmap representation for shared generated font data.
For movies, put dimensions, frame durations and logical image data in the generic
representation. Resolve screen grouping through layout; handle physical rotation,
mirroring and wire byte order in the output adapter. A core asset must not mean
"outer pair plus mirrored center". Preserve current visible results with fixtures.

Separate generic build orchestration from the CPU compiler/linker/uploader.
The adapter supplies its build entry point and CPU-specific image preparation.
Generic compilation, backup, setup copy and asset transfer work without importing
that entry point. Keep the existing device uploader and recovery tools local;
extract their reusable parsing or transport only where it has a clear current use.

## Implementation sequence

1. **Record the boundary and baseline.** Inventory reverse imports, includes,
   build paths, generated files and hidden hardware constants. Record current
   protocol versions, golden bytes, rendering fixtures, switch timing requirements
   and firmware memory results. Classify product behavior versus physical facts.
2. **Establish the capability and service contracts.** Adapt the current hardware
   implementation and native test fakes to the same contracts. Introduce the single
   target-selection build entry point. Document memory and display-buffer ownership
   before moving runtime code. Keep construction in the adapter; no generic code
   looks up a target implementation.
3. **Extract Python assets.** Move shaping, font packing, movie preparation and
   inspection first. Update compiler/display/transport imports and tests together.
   Separate native font converters from generic rasterization. Generic Python tests
   must no longer depend on the adapter's cached toolchain or generated headers.
4. **Extract storage policy and protocols.** Move counters, recording selection,
   calibration persistence, font/file policy and splash transfer logic. Bind them
   to the existing storage interfaces extended only for demonstrated operations.
   Preserve bundle save durability and history behavior.
5. **Extract pages, application composition and scheduling.** Move UI state,
   commands, app lifecycle, song capture/resolution and poll order out of
   `target.cpp`. Leave physical input, transport and device-control bindings.
   Preserve immediate MIDI dispatch and scan priority between display transfers.
6. **Extract layout and rendering.** Replace display-library inheritance with
   portable raster operations and the display submission contract. Move text,
   wrapping, effects, scrolling, transitions and movie playback. Keep optimized
   hardware scrolling/DMA behind optional services with a portable rendering path
   that every conforming target can use.
7. **Remove embedded board assumptions.** Make button lookup/masks, logical windows,
   endpoint ordering and memory accounting derive from capabilities. Update host
   packing and firmware decoding together wherever current fixed records prevent
   this. Put RAM2 totals/placement into the current adapter's memory description;
   keep residency planning generic. Do not confuse estimates with measurements.
8. **Finish the boundary audit.** Remove replaced implementations and old imports,
   migrate generic tests/docs/build recipes and verify the acceptance criteria.
   Reduce the local entry point to hardware construction and calls into the core.

Keep each commit buildable. Pure moves should preserve bytes and behavior.
Capability-driven record changes and neutral movie/font formats may require a
format-version bump; isolate these changes and regenerate fixtures together.
Recompile affected assets/setups rather than adding a legacy-format compatibility
layer. State deployment implications before implementing format changes.

## Verification and acceptance

- Portable Python imports and tests run with the adapter directory and its
  toolchain unavailable. Portable C++ tests compile with a standard host compiler,
  without Arduino, display SDK or SD-library headers.
- Dependency checks reject maker/board paths and names in portable imports,
  includes, build recipes and generated source. Scan for disguised coupling too:
  CPU registers, linker symbols, platform memory attributes and fixed geometry.
  No allowlist of MC references in generic code is permitted.
- Use native fake hardware with a different input count, screen count/size and
  memory budget. Run the same application, page navigation, layout and asset
  pipeline. This is a contract test, not a new production pedal backend.
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
