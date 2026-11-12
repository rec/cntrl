# Morningstar dynamic-memory inventory

Supporting hardware-specific audit for
[the shared plan](../../../../plan/dynamic-memory.md).
Original source baseline: `a32af63`, 2026-11-12. The original inventory is retained
below; implementation checkpoints are recorded separately. Physical qualification
is pending. Paths below are relative to this directory.

## Controller allocation implementation

The initial controller change removes the three statically reserved extra
controllers and acquires exactly the extra instances required by the candidate
layout. Allocation and construction precede setup handoff. A failed preparation
keeps the active controllers. Cancelling preparation releases candidate storage.

An isolated source export of baseline commit `e33722d`, built with the same
installed toolchain, produces the following linker totals.

| Region | Baseline | Demand-sized extra controllers |
| --- | ---: | ---: |
| RAM1 variables | 319904 bytes | 319936 bytes |
| RAM1 available for local variables | 7776 bytes | 7744 bytes |
| RAM2 static variables | 118912 bytes | 112736 bytes |
| RAM2 available for allocation | 405376 bytes | 411552 bytes |

A one-controller setup acquires no extra controller heap storage, recovering
6176 bytes of RAM2 with a 32-byte increase in RAM1 variables. Multiple controllers
charge their actual allocation to `memory_used`. Replacement can temporarily
hold both old and candidate allocations. Runtime allocator overhead and stack
high-water still require device measurement.

Reproduce the baseline by exporting `git archive e33722d` into a generated
directory, using the installed pinned SDK and stock image, and invoking
`python -m cntrl.maker.morningstar.mc.build_target` from that exported tree with
its own source root and output directory. Run the same build command from the
working tree for the candidate and compare their exported `memory.txt` files.
The source export, build logs, binaries and memory reports under `build/` are
disposable generated artifacts. The original sources and build procedure remain
tracked.

The next change replaces the twelve invocation-navigation reservations with
checked nodes held only by live executors. The target linker reports 94176
bytes of static RAM2 and 430112 bytes available for allocation. RAM1 variables
are 319968 bytes, with 7712 bytes left for local variables. This recovers another
18560 bytes of static RAM2 relative to the controller-only build, including
section alignment. Each live snapshot's actual node bytes are charged to the
shared memory ledger. Cancellation and completion reclaim the node.

## Integrated implementation checkpoint

The integrated target build passes with the same pinned toolchain:

| Region | Original baseline | Integrated candidate |
| --- | ---: | ---: |
| RAM1 variables | 319904 bytes | 309728 bytes |
| RAM1 available for local variables | 7776 bytes | 17952 bytes |
| RAM2 static variables | 118912 bytes | 90752 bytes |
| RAM2 available for allocation | 405376 bytes | 433536 bytes |

This recovers 28160 bytes of static RAM2. Local-variable availability is a linker
total, not a measured stack high-water mark. Cold preparation/reclamation/UI code
uses FLASH placement; hot insertion/iteration/parsing/ISR paths retain their
normal placement. Final timing qualification must use this placement.

Configuration arrays use encoded counts, while admitted performance records use
checked stable nodes. Candidate controllers, fonts, stream graphs and
arpeggiator resources are prepared before publication. Active/undo/candidate
looper roles remain separate, but disabled/empty generations reserve no event
buffers. Explicit arm estimates are reported separately from idle allocations.

The linked allocator is traced through the reentrant newlib allocation APIs.
`MemoryBuffer` charges actual nano chunks on this target and retains the old
allocation until a complete replacement is admitted. Native tests and host
estimates use the documented alignment/header model, independently of the native
operating system's allocator. SDK bootstrap use is subtracted from the available
budget; later SDK growth is reported separately. The tracked
`firmware/patch_switcher/heap_trace.cpp` depends on the pinned toolchain's nano
chunk layout. Its totals and validity must be checked on the physical target.

`CNTRL HEAP` is an explicit idle-only diagnostic. It reports actual checked/SDK
bytes, SDK growth, largest free block, free block count, conservative peak,
allocation failures, allocator cycle maxima and interrupt allocation count.
It walks the free list only when requested, outside MIDI service. Linked
allocation fault injection is available to qualification code through
`heap_fault_after`; normal firmware leaves it disabled.

`CNTRL MIDI STATUS N` adds a maximum `service_gap_us` across calls, including
intervening display/storage work. `CNTRL MIDI RESET` resets timing counters.
The status also identifies Bridge/stream input cable quarantine masks and the
generated pedal route quarantine. After an unseen key cannot record a refused
strike, reconnect/reset the source before accepting new onsets; accepted releases
continue. Heap recovery alone cannot safely identify an untracked old note-off.

The tracked native stress test is `firmware/test/test_dynamic_memory_stress.cpp`.
An optimized host run with a 430112-byte modeled budget accepted all traffic in
the 24/32-route cases. Denser 48/64-route bursts exhausted that budget and
refused new work while preserving the accepted seed's releases. A 24-point
allocation fault sweep completed without stranded voices, pending events or
recovery work. Rebuild with the usual native test flags for sanitizers, or
`-O2` without sanitizers for comparative timings. Logs and binaries in `build/`
are disposable outputs; the complete harness and native suite entry are tracked.

These results do not establish pedal timing. The CPU defer/reject policy and
the general allocator's worst-case latency remain open pending target stress
measurements. No bounded allocator or completed physical qualification is claimed.

## Physical qualification procedure

Use `uv run --frozen cntrl qualify --target-dir cntrl/maker/morningstar/mc`
to discover the MIDI endpoints. Target options follow `--`. The DIN output must
connect to the DIN input for the forwarding checks below.

The tracked `qualification/dynamic-memory-thru.toml` setup enables the ordinary
interface. Copy it with `cntrl setup copy`, using the built `target.toml`, then
select `dynamic-memory-thru` with `cntrl setup set`. Run:

```bash
uv run --frozen cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --run --expect-enabled --destination 'cntrl experimental DIN' --source 'cntrl experimental DIN' --report build/dynamic-memory-thru-physical.toml
```

This checks exact Program Change, modulation, 64 paired note strikes, sustain
and streamed SysEx bytes. It does not measure wire timestamps or clock jitter.

The separate `qualification/dynamic-memory-dense.toml` setup has 24 parallel
USB-to-DIN routes with 16 identity transforms each, plus a DIN-to-USB return
route. Copy and select it with the same setup commands, then run:

```bash
uv run --frozen cntrl qualify --target-dir cntrl/maker/morningstar/mc -- --run --expect-enabled --destination 'cntrl experimental DIN' --source 'cntrl experimental DIN' --stress-only --stress-seconds '30 seconds' --report build/dynamic-memory-dense-physical.toml
```

The dense graph changes output multiplicity. Stress-only reports explicitly skip
byte verification and capture CPU/service-gap, heap and refusal counters instead.
The flood sends Program Change and modulation messages without holding notes.
Save the original setup selection before testing and restore it afterwards;
temporary probe setups can then be removed with `cntrl setup del`.

Managed flashing retains original binaries and history in `before/`, and validates
a separate current-format `restore/` tree before entering the bootloader. Wire
upgrades preserve existing patch node IDs and inactive new gesture actions;
current RAM estimates are refreshed from the saved fonts and target description.
Standalone restore/verify commands require that current `--target` description.

The retained fixed storage has specific roles: physical display pixel caches
and DMA transfers; byte/mask/record format domains; three looper generation roles;
USB framing and retry units; redundant flash journals; and explicit text/history
retention policies. Endpoint-sized constructor vectors are created once from
actual hardware counts and are included in SDK/bootstrap heap reporting. They
do not grow on player actions. Shared presentation no longer allocates per frame.

## Original product target reservations

| Location | Present storage or constraint | Required treatment |
| --- | --- | --- |
| `firmware/patch_switcher/target.cpp` | Primary controller plus `DMAMEM extra_controllers[max_controllers - 1][sizeof(Controller)]`; placement constructs all three extras. | Allocate only the configured controller count. Placement `new` here does not allocate storage; removing only the constructor loop would leave the reservation in place. |
| `firmware/patch_switcher/target.cpp` | `DMAMEM invocation_navigation[Bridge::owners]`: twelve complete navigation snapshots. Owner numbering reserves eight switch owners followed by controller owners. | Allocate actual active invocation snapshots using the shared stable-owner design. Update switch/controller delivery and return logic together. |
| `firmware/patch_switcher/target.cpp` | Firmware visit record embeds two navigation states and fixed setup/song references; document/song/transfer objects also have static storage. | Stage dynamic return state before leaving a setup. Persist explicit values and counts, with no raw dynamic object serialization. Review optional module state by lifetime. |
| `firmware/patch_switcher/target.cpp` | `RenderPixels` has current and previous full-display caches in RAM1: 61,440 pixels each, two bytes per pixel, 245,760 bytes together. `DMAMEM render_work` holds the largest display: 20,480 pixels, 40,960 bytes. | These sizes derive from the three physical 160-by-128 displays. Measure the need and lifetime of each copy, especially previous pixels for transitions. Any lazy buffer must be prepared before use; do not remove rendering buffers without preserving fades and transfer timing. |
| `firmware/patch_switcher/displays.cpp` and `firmware/usb_probe/displays.h` | Two `DMAMEM DisplayBand` objects support display transfer and preparation. | Keep simultaneous producer/DMA lifetimes and required alignment/cache maintenance. A dynamic buffer must remain at a stable address until completion. No allocation or freeing in the completion interrupt. |
| `firmware/patch_switcher/target.cpp` | Global USB session and splash receiver/router embed the shared 4096-byte transfer and text/frame workspaces. | Establish which transfers can coexist and acquire only the required workspaces. Preserve any in-flight serial/USB data before reusing or freeing storage. |
| `firmware/patch_switcher/target.cpp` | Four `song_readers` and a four-pointer handle array are supplied to `SlotStorage`. | Map actual simultaneous reader roles, then allocate handles when needed. Do not replace a concurrency requirement with one shared mutable reader. File open/close may allocate inside the storage library. |
| `firmware/patch_switcher/target.cpp` | Stream identities are indexed by the shared sixteen-route ceiling. Physical button/port arrays and four expression assignments follow the actual hardware. | Replace route-indexed state with actual endpoint counts. Keep geometry/capability-sized physical input state unless the target configuration makes a device optional. |
| `firmware/patch_switcher/target.cpp` | SD identity formatting uses nine fixed text values; names, paths and replies use small bounded scratch. | Keep justified bounded formatting and allocation-free diagnostics. Do not heap-allocate every temporary string during an error report. |
| `firmware/patch_switcher/usb_forward_device.cpp` | High/full-speed descriptor scratch is 512 bytes each; product scratch is 128 bytes. USB identity code also contains immutable descriptors. | Separate descriptor format/endpoint constraints from optional scratch. If made dynamic, construct before enumeration and keep storage alive for the USB driver's full access period. |
| `firmware/patch_switcher/target_capabilities.h` and host target data | RAM capacity is 524,288 bytes and the host runtime allowance is 294,912 bytes. Target capabilities specify six buttons, four ports, three displays, ten endpoints and switch-count restrictions. | Keep physical capabilities separate from configurable collection limits. Replace the coarse runtime allowance with evidence-based fixed/core, configured and transient costs as the shared plan removes reservations. |

## Runtime allocation and dependency boundaries

The target's `filesystem.h` creates file and directory implementations through
owning pointers. Stream playback opens independent source/index handles and
constructs its player. Browsing recursively creates directory levels; cancellation
closes and destroys them. These paths are reached from pedal controls and host
commands while MIDI may be active, so storage allocation failure must propagate
without retiring the active source.

TRS mode reconfiguration constructs/destroys optional `FlexSerial` instances.
USB host enumeration/disconnection, display transfer/DMA setup, SD/SdFat file
operations and driver callbacks are transitive allocation boundaries. Instrument
their actual linked allocation behavior, including first use, rather than assuming
an absence of allocation from the target's explicit `new` sites. Do not move
enumeration or DMA lifetime management to an unchecked shared allocator.

The toolchain builds C++ with `-fno-exceptions`. In the inspected Teensy 1.62.0
core, ordinary `operator new` and `new[]` return `malloc` results; deletion uses
`free`, and aligned forms use aligned allocation. Consequently an ordinary
container growth failure is not a recoverable `bad_alloc` contract on this target.
The shared plan must cover both explicit allocation and library/container growth.
This audit does not propose changing toolchain or dependency versions.

`std::stable_sort` in the shared stream/timeline code can request temporary
storage. Target/native tests must cover the real nonthrowing checked path, not
rely on differences in the host standard library's allocation failure behavior.

## RAM1, RAM2 and measurement

`firmware/memory.ld` derives the budget from the linked heap
boundaries. Static `DMAMEM` objects consume RAM2 before the ordinary heap can use
it; ordinary static data and stack usage affect RAM1. Moving an object to dynamic
storage changes which region bears its cost, and may introduce DMA/cache alignment
requirements. Record actual linker placement rather than assuming every dynamic
or fixed buffer lives in the same region.

The status code in `target.cpp` currently estimates RAM2 free space from the
unextended heap plus `mallinfo().fordblks`. It does not report the largest free
block or distinguish the shared `MemoryBuffer` ledger from raw/STL allocations.
RAM1 free space uses the current stack pointer relative to static data, which is
not a worst-case stack high-water mark. A large navigation or pane copy can matter
even if a later status query looks healthy.

Qualification needs linked static totals, allocator capacity/metadata, current
and peak live bytes, largest allocatable block, failure counts, stack high-water
and transient old/new peaks. Report region and alignment restrictions explicitly.
Do not add the full .cnsa movie size to RAM2: the player streams movies from SD;
charge resident pixel/colour/transfer state and any retained indexes instead.

Measure repeated setup changes, source retirement, display fades, monitor open/
close, loop and arpeggiator replacement, SD browsing and USB attach/detach under
pressure. Verify the audio/MIDI service loop's worst-case delay, not only absence
of a crash. Physical execution requires separate authorization and remains a
completion gate for the implementation.

## Other authored firmware and justified fixed data

| Area | Finding and scope decision |
| --- | --- |
| `firmware/boot_switch/` | Gesture history reserves 128 entries. Review the required boot gesture window, but keep boot/recovery independent of an exhausted application heap. Flash selection records and the 128-word NOR configuration are persisted/ROM layouts, not unused feature instances. |
| `firmware/usb_probe/` | Display/button arrays follow the physical device; small line/row scratch is bounded diagnostic working state. Shared display bands must retain DMA lifetime semantics. |
| `firmware/scroll_probe/` | The diagnostic image has dimensions 1280 by 256, occupying 327,680 bytes, plus physical buttons and small text/status buffers. It is a measurement fixture, not the product's live musical state. Preserve the reproducible experiment unless a separately requested change needs different image allocation. |
| `firmware/button_timing/` | Button-specific timing state follows the diagnostic's selected physical inputs. It is not an application collection limit. |
| Splash firmware/board and shared animation components | Panel dimensions and stream counts reflect the display layout. Full movie data stays on SD; shared colour state can still allocate at playback start and is included in the shared runtime inventory. |
| Compiled fonts, USB identity and flash journal layouts | Immutable ROM/flash data and required redundant records remain fixed. They must not become heap objects just to eliminate constants. |
| `archive/` and native test fixtures | Historical snapshots and test-only arrays do not reserve memory in the product binary. Exclude them from product totals, while retaining their role as test/reference material. |

## Additional work beyond the prompt

None.
