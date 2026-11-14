# Dynamic firmware code

Status: proposed, 2026-11-14. This is a plan, not an implemented module loader.
Existing firmware measurements and source inspection inform the estimates below;
no new firmware build or hardware experiment was performed for this document.

## Objective and limits

Ship a small resident kernel and individually precompiled feature modules. The
host installs the matching modules on an existing SD filesystem; the pedal loads
the features required by its setup. Users do not install a compiler or build
firmware. One kernel and one set of modules per execution target/release avoid
the exponential number of feature-combination firmware variants.

An SD card supplies bytes, not executable addresses. Native code must be copied
into executable memory before calling it. The first implementation should use
RAM, not rewrite internal flash during performance. Do not format cards, modify
partitions, or add an internal-flash module cache as part of this work.

Distinguish three results:

1. Removing optional code reduces the resident firmware's flash payload.
2. Removing enough resident instruction code can release instruction-memory
   banks for data, depending on the target's linker/startup layout.
3. Loading a module consumes executable memory, module data and alignment space.
   Moving code that currently executes from flash into RAM can increase RAM use.

Consequently, dynamic loading is not automatically a net RAM saving. Prove the
complete memory balance before committing to production modularization. Record
the kernel, loader, executable arena, module data, existing MIDI state, display
buffers and staging/transition peaks together. The
[reference target appendix](../cntrl/maker/morningstar/mc/dynamic-code.md)
contains the existing measured baseline and the target's specific constraints.

## Resident kernel

Keep the system usable when an optional module is absent or invalid:

- Startup, interrupts, physical drivers, USB enumeration, storage mounting and
  existing file/asset transfer machinery.
- Checked allocation, executable-memory admission, cooperative scheduling,
  clock/tap-tempo service, output queues, note ownership and emergency cleanup.
- Basic MIDI forwarding and channel mapping. Common parsing, ordinary live
  controller/pickup handling and output admission remain shared services.
- Basic patch execution, controller navigation, setup selection and validation,
  module requirement checking, minimal management/error UI and recovery.
- Static text/graphics rendering, brightness and independently configured throb.
  The existing frame-buffer count remains a separate setting.
- Module discovery, validation, loading, reference tracking and dispatch.

Do not make Panic, output cleanup, the installer or the recovery interface depend
on loading another module. Tiny features such as counters, text cues, expression
calibration and relay primitives should initially stay resident: their code is
small and they are common dependencies of larger features.

## Candidate modules and approximate sizes

These are provisional native code plus small read-only-data budgets, in KiB
(1024 bytes). They exclude mutable feature state, recordings, frame buffers,
font assets and movie files. They are not independently linked module sizes.
Inlining, retained helpers and the new boundary can change every range; do not
sum them to claim a kernel size or an exact saving. Target-specific symbol
attribution and its limitations are recorded in the appendix.

| Module | Approximate payload | Scope and loading condition |
| --- | ---: | --- |
| `midi-processing-advanced` | 18–32 KiB | Advanced routing nodes: echo, unison, reversal, richer transforms, generators and LFOs. Required by an advanced graph; pinned for pending events, generators, tails and cleanup. Basic forwarding/channel mapping stay in the kernel. |
| `midi-looper` | 12–20 KiB | Capture/overdub/undo/reverse and loop scheduling, with its page/actions. Pinned while armed, recording, playing or retaining a loop whose operations require its code. |
| `midi-files` | 16–28 KiB | SMF playback, recording and indexing plus their controls. Pinned for open players/recorders, pending file work and note cleanup. Basic filesystem and output services stay resident. |
| `arpeggiator` | 12–20 KiB | Mono/poly banks, selectors and expression replay, with page/actions. Required by configured arpeggiator use; pinned for enabled/captured state and generated voices. |
| `songs` | 20–32 KiB | Song/setlist sequencing, source capture and extended song UI. Pinned for song execution, held source documents and pending continuations. Setup loading and persistent-envelope validation remain resident. |
| `switch-assignments` | 12–18 KiB | Configurable switch action/gesture machinery and editor. Required by assignments, including hidden pages. Basic scanning/debounce, primary navigation and emergency controls stay resident. |
| `midi-monitor` | 4–8 KiB | Traffic decoding/history and monitor presentation. First loader proof. Pinned while subscribed, recording history or holding a view; closing the view can release it after unsubscribing. |
| `movie-playback` | 6–12 KiB | Streamed movie player, colour cycling and startup/idle transitions. Required by enabled configured movies; assets remain on SD. Not required merely because frame buffers exist. |
| `scrolling` | 4–10 KiB | Horizontal/vertical/panoramic transitions, including single-frame rendering coordination. Needed only when animations and a nonzero frame count allow scrolling. Static drawing and throb stay resident. |
| `storage-tools` | 8–16 KiB | Optional storage browser, extended history inspection/comparison and disk benchmark. Minimum setup management, backup/recovery and transfer services stay resident. |
| `cable-test` | 3–6 KiB | Interactive MIDI cable diagnostics and its page. Requires idle/admitted diagnostic output ownership. Panic is not included. |
| `keyboard-controls` | 2–4 KiB | Shortcut actions and keyboard page. The target's USB transport/descriptors remain resident; loading a module must not require USB re-enumeration. |

Use feature groups initially, not one binary per effect, page class or selector.
Common capture/timeline/format helpers may remain resident until independently
linked artifacts show a worthwhile shared-module boundary. Avoid duplicating
them in every module to make an attractive but misleading size table.

The existing routing split is a starting point, not a finished plugin boundary:
the application still instantiates `Routing<AdvancedProcessing>`, and advanced
templates refer to private core types. Replace that integration with a basic
resident engine plus an explicit processing-extension interface. Do not ship two
parallel routing engines, ownership systems or output queues.

## Build and package contract

Build modules in release CI with the same pinned toolchain/CPU/FPU ABI as the
kernel. Initially require an exact kernel build identifier and module ABI match;
do not promise compatibility between unrelated firmware releases.

Use a compact binary `.cmod` package containing:

- Target/CPU ABI, kernel build identifier, module ID/kind and format version.
- Code/read-only image size and alignment, writable initialized-data and zeroed
  storage requirements, entry-table offsets and relocation records.
- Lengths and integrity checks covering the complete package, including metadata.

A TOML release manifest describes the supplied module set to host tools. The
pedal reads the bounded binary package header; it does not need a TOML parser.
Install each build's files together under a versioned module directory using the
existing verified-transfer and publication patterns. Missing/mismatched modules
produce a specific setup-admission error, not an automatic online download or a
silent change to MIDI processing.

Native modules are trusted firmware. Integrity checks detect corruption; they
do not sandbox native code or authenticate an untrusted publisher. Third-party
native plugins are outside this initial design.

## Linking and the interface boundary

The publisher handles ELF complexity. Produce a compact image and supported
fixup records from each linked module; do not embed a general ELF/dynamic-linker
implementation in the pedal. Inventory actual emitted relocations first. Reject
unsupported relocation forms during packaging and again during loading.

Internal relative branches can remain valid when their complete code image is
moved together. Absolute references, separate writable data, function pointers
and target branch ranges still require deliberate relocation handling. A copied
piece of the current ELF is not a working module. Follow the target ABI rather
than assuming `-fPIC` solves every reference; for Arm, use
[AAELF32](https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst)
and [AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst).

Use a narrow versioned C interface with fixed-width records, byte lengths,
opaque state handles and function tables. Modules call resident allocation,
clock, routing/output, file and rendering services through the supplied table.
The kernel dispatches through a stable resident handle, not a direct module
pointer stored indefinitely in a queue.

Keep C++ private to each side. No `std::vector`, exceptions, RTTI, owning C++
objects, implicit global constructors, or cross-boundary virtual destructors.
Module creation/destruction is explicit; allocation and release use the resident
allocator. Module entry tables identify the operations appropriate to their
kind: preparation, bounded service/rendering, stopping/draining and destruction.
No module may install an interrupt handler in the first implementation.

## Executable memory and timing

Each execution target supplies the executable allocator, permission changes,
cache synchronization and ABI identity. Controller layout/catalogue records do
not establish that RAM is executable. The emulator needs host-native builds of
the same module interface; it cannot execute a microcontroller's instructions.

Keep basic MIDI and safety paths in fast resident memory. Start the loader proof
with a cold module. Move time-critical engine modules only after their hot code
can be admitted into a suitable fast-code region and measured under load.
Ordinary executable RAM is not automatically an acceptable replacement for
fast instruction memory.

Use dedicated executable arenas with the target's required alignment/protection
granularity, separated from writable data and DMA buffers. Charge the complete
arena reservation and padding to the memory budget, not just payload bytes.
Enable write access only during controlled installation, synchronize caches and
instruction state, then publish executable read-only code. For Cortex-M7,
[Arm's cache-maintenance guidance](https://documentation-service.arm.com/static/61efd6602dd99944d051417b?token=)
is relevant to publishing newly written instructions.

Loading and removal happen at an explicit dispatch barrier. If changing an arena's
permissions affects existing modules, pause their dispatch while writing, and
bound that interval against MIDI deadlines. Never read an SD file, relocate,
allocate or unload from a MIDI interrupt or an event's delivery callback.
If safe loading requires idle engines, report that requirement before changing
the setup; do not stall or stop an active engine implicitly.

## Requirements and module lifetime

Derive the required set from compiled setup semantics: graphs, patch/switch
actions, configured page cycles, background engines, movies and settings.
Keep module IDs separate from physical left/right placement. A hidden page or
zero-window region can still require its engine.

Prefetch all required modules before accepting a setup. Prepare candidate code,
data and state before replacing the active set. Account for both generations
and staging in the peak budget. Reject insufficient memory without discarding
recordings or evicting an active module to make the candidate appear to fit.
Preparation must not send MIDI, change relays or publish active subscriptions;
those effects begin only after commit.

Configured optional pages should be available without a first-press SD load.
Unconfigured management tools may request a load at an admitted control boundary;
if the boundary is unsafe, keep performance running and report/defer the request.
Do not add automatic code paging while notes or clocked effects are running.

Pin modules while any callback, queued work, voice, file operation, capture bank
or retained object needs their code. Module handles need generation identity so
reusing an arena cannot turn stale work into a call to another module. Unload
only after unsubscribing, stopping/draining, transferring any required note-off
cleanup to resident ownership, destroying state, and confirming no references
remain. Closing a page alone does not establish this condition.

Initially modules import only resident services, not one another's private
symbols. The loader owns the required set; a later shared feature module is
justified only by measured duplication and an explicit dependency/lifetime rule.

## Implementation sequence and acceptance

1. **Measure boundaries before changing production.** Build independently linked
   candidate artifacts using the pinned release toolchain. Record exclusive
   code/read-only bytes, current fast/flash placement, duplicated/shared helpers,
   writable state, relocation types and estimated ABI/loader cost. Compare a
   basic-forwarding setup with representative simultaneous-feature setups.
2. **Prove the target backend with MIDI Monitor.** Implement packaging, bounded
   validation/relocation, memory permissions, dispatch and unload. Prove load,
   render, unsubscribe, unload and reuse on hardware. This is a loader proof;
   a small module may save less flash than the resident loader adds.
3. **Establish the release/install path.** Ship prebuilt kernel/module sets,
   calculate setup requirements on the host and reject target/build mismatches.
   Keep recovery and original persistent setup contents available on failure.
4. **Extract cold feature groups.** Try monitor, movie playback, scrolling and
   optional tools. Measure actual RAM costs as well as resident flash reduction.
   Reuse one rendering/output path; keep throb and frame-buffer policy unchanged.
5. **Extract advanced MIDI behind one routing core.** Preserve basic forwarding,
   channel mapping, pickup targets, atomic message groups, ownership and cleanup.
   Then consider looper/files/arpeggiator/songs/switch groups with explicit hot
   placement and pinning. Allow simultaneous required modules; do not assume
   one interchangeable slot is sufficient.
6. **Decide whether to ship.** Require demonstrated net benefit for intended
   profiles and maintained fast-memory/stack reserves. If RAM loading only moves
   costs into the recording heap, stop before adopting it as a memory remedy.

Verification must include malformed/truncated packages, incompatible builds,
unsupported fixups and address ranges, allocation failure, failed candidate
initialization, stale handles, module removal with active notes/tails/files,
missing or removed SD media, setup rollback and repeated arena reuse. Compare
feature output with the resident implementation before removing its old path.
After extraction, there is one feature implementation, not permanent parallel
resident/dynamic implementations.

On hardware, measure worst MIDI-service gaps and output timing while loading,
rendering, recording, playing and cleaning up, including cache-cold execution.
Report actual kernel code, executable-arena consumption, module state and
stack/heap peaks. No configuration should pass on combined free bytes when
contiguous executable storage or required fast-memory placement is unavailable.

## Additional work beyond the prompt

None.
