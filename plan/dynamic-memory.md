# Demand-sized firmware memory

Status: continuation implementation validated locally; physical qualification
remains in progress, 2026-11-13.
The inventory below records the original audit; implementation results and
outstanding qualification gates are tracked here and in the target appendix.

## Implementation decisions

The upgrade may discard temporary saved setup-return records when their format
changes. Finish implementation and automated tests before requesting approval
to flash and run physical qualification. Keep persistent setups, histories, songs and assets protected by
the existing backup and verified restore flow.

The first implementation stages an exact-count allocation for extra controllers
before setup selection commits. The primary controller continues to provide the
fallback used by management pages. Native tests cover one/four controllers,
failed replacement, cancelled preparation and reclamation. An exact baseline
build reduces static RAM2 from 118912 to 112736 bytes, freeing 6176 bytes for
the one-controller case. RAM1 variables grow by 32 bytes. Physical qualification
remains pending, along with subsequent milestones.

Invocation navigation snapshots now use checked nodes for live executors,
identified by executor address. Patch preparation acquires a snapshot before
validation or output, and completion/cancellation releases it. Tests cover
failure, reuse, stable addresses and executors sharing an output owner number.
Delivery owners now register stable live identities, and temporary firmware-return
records encode counted navigation data explicitly. Endpoint masks retain the
existing encoded sixteen-endpoint domain.

Font cache metadata is counted from distinct asset/size pairs before staging
and allocated once at that count. The active cache survives failed preparation.
The shared cache and compiler no longer impose the former 32-face limit.

Configuration and live collections now use exact-count arrays and checked stable
nodes. Setup graph and arpeggiator preparation stage resources before publication.
Stream traversal uses a prepared explicit work list instead of recursive graph
calls. Growing buffers admit the complete replacement while retaining the old
allocation, including the target allocator's alignment/header charge.

The tracked `firmware/test/test_dynamic_memory_stress.cpp` exercises dense graphs,
limited RAM and deterministic allocation faults. It exposed an unsent onset
blocking older releases; that path now refuses the new onset without retiring
the older voices. Host timings are comparative measurements, not pedal deadlines.
Physical stress measured an ingress call of 9.30 ms with about 2700 queued events.
The user chose preparation across cycles, preserving MIDI order and prioritizing
the causal preparation through a release over later work. Earlier accepted
changes can delay a release under overload; already-prepared playback and cleanup
run first. Preparation now yields between route inputs, graph steps, echo copies,
output branches, release bookkeeping and ordered-event insertion visits. New
admission retains graph-derived traversal workspace alongside terminal credits,
so an accepted pedal release can complete its fanout under allocation failure.
Stable event nodes move from staging to delivery without allocation. Intrusive
minimum indexes retain causal ordering across inputs without rescanning the whole
preparation queue for each output. Reconnects discard old output-generation work;
finite file reversal retains the original completion time while work drains.

Native sanitizer regressions cover continued SysEx publication, exhausted pedal
fanout, disconnect/reconnect, deferred finite reversal and nested node lifetimes.
Host RAM estimates and compiled fixtures match the checked ARM layouts. Normal
and diagnostic target builds pass. This bounds graph visits, not elapsed time:
processor callbacks, receive-time note-ledger scans and other atomic operations
still require physical timing measurements. Manual cross-platform qualification
and the physical exhaustion/timing matrix remain completion gates.

One bounded shared RAM2 allocator now replaces the SDK and checked application's
newlib allocation path. Metadata lookup/coalescing has bounded work; payload
zeroing/copying remains proportional to size. Independent sanitizer tests cover
fragmentation and failed replacement. Physical timing, payload-copy latency, linked
SDK allocation coverage and final stack/heap qualification remain completion
gates. Do not describe these checked collections alone as a bounded allocator.

## Objective and scope

Allocate storage for the setup, features and work actually in use. Remove
arbitrary item ceilings and reservations for hypothetical instances. In
particular, one patch switcher should require one controller and its navigation
state, rather than storage for four controllers and every possible invocation
owner.

This audit covers authored production code under `firmware/include/cntrl/`,
the host models/compiler that impose matching limits, and the target integration.
The [target inventory](../cntrl/maker/morningstar/mc/dynamic-memory.md) contains
hardware-specific findings, supporting firmware and memory measurement details.
Paths in the shared inventories below are relative to `firmware/include/cntrl/`
unless another root is specified.

This is a source inventory of allocation owners and their callers, not a runtime
proof that every library allocation has been found. Instrumentation of the
linked allocator and failure injection are required before declaring completion.
Test fixtures, historical archives, generated immutable font data and lookup
tables are not live feature reservations.

## Meaning of dynamic use

- Size configured collections from actual counts. Create optional feature state
  when enabled and release it when no longer used or pinned. Allocate configuration
  collections once at their final size wherever the counts are known.
- Grow performance data against a shared byte budget, rather than independent
  worst-case reservations for each feature. Preserve explicit musical policies,
  such as history retention, separately from storage implementation limits.
- Prefer lists or segmented storage for growing performance collections when
  this avoids moving existing records and requiring a second full-size buffer.
  Avoid vector reallocation during performance where practical, rather than
  prohibiting every reallocation. Justify retained growth by its access pattern,
  measured cost, transient memory requirement and failure behavior.
- Keep necessary protocol layouts, physical geometry, atomic message groups and
  small bounded working state. Replacing every three-byte MIDI packet or stack
  temporary with a heap allocation would add cost without saving useful memory.
- Allocate before an operation becomes externally visible. Do not introduce
  unbounded allocator work into MIDI callbacks, interrupts, release paths or
  playback deadlines merely to remove an array.
- Replace the existing representation and its limits together. Do not retain
  both fixed and dynamic implementations or add a second configuration syntax.

This requires architectural changes to ownership, admission and allocation
failure propagation. Implementation should follow the stages below, rather than
mechanically replacing arrays with `std::vector`.

## Fixed storage and item limits

The tables identify owning families, including repeated instances and temporary
copies. Numeric fields in serialized records are distinguished from capacities.

| ID | Location and present reservation or limit | Planned treatment |
| --- | --- | --- |
| F1 | `page_layout.h`: 4 controllers, 36 pages; `ui/application.h`: page-name slots for 3 tools plus 36 pages. Host layout validation rejects more than 4 patch switchers; setup models cap pages at 36. | Allocate controller objects and page metadata from the actual layout. Replace owner arithmetic and dependent snapshots with F3. Remove corresponding host caps when the representation and resource admission support it. |
| F2 | `hardware.h`, `controller.h`: 32 instruments, 128 patch cycles, 128 history entries. Each controller contains cursor and preview arrays, current/last-sent arrays and history. `NavigationState` repeats substantial storage; `navigation()` also produces value copies. | Size instrument/cycle state from the document. Store actual history entries with a deliberate retention policy and checked growth. Capture only necessary navigation state, transactionally; include temporary snapshots in the peak budget. |
| F3 | `midi_bridge.h`: 12 invocation owners; `midi_file.h`: 12 delivery slots per player. Controller/switch owner numbering and return navigation depend on this ceiling. `ui/firmware_visit.h`: two complete navigation snapshots per visit. | Register actual live owners with stable identities, and allocate snapshots/delivery state for actual invocations. Preserve identity until all releases and deferred work retire. Replace persisted visit serialization, which currently embeds value state, with explicit counted data; never write pointers to disk. |
| F4 | `switches.h`: 8 `State` objects/executors; per-state queue of 16 and event array of 16. Two on/off gesture layers per state are distinct roles. | Allocate configured switch states and queued work as admitted. Retain the paired gesture semantics. Reserve release work before accepting press work. |
| F5 | `midi_bridge.h`: every configured route embeds a 256-packet queue and 8 pedal batches, each batch holding up to 14 bytes; owner packets also use 14 bytes. `midi_outputs.h`: 16 routes represented by masks. | Make queue and pedal storage proportional to admitted traffic and actual routes. Replace endpoint masks before lifting the endpoint limit. Keep proven atomic packet-group capacity or derive it from the transaction format. |
| F6 | `midi_processing.h`: 64 stream routes and 64-bit owner masks. Default limits: 512 events, 256 voices, 256 notes and 256 captures. Fixed 16-output cursor/SysEx/clock arrays; route-sized stack scratch. LFO loading reserves parameter deliveries, bindings, events, captures and reverse parameters in advance. | Allocate topology from configuration, sparse output state from actual endpoints, and performance records from admitted work. Replace masks and route-derived input identifiers together. Avoid replicating whole-route scratch on the stack. |
| F7 | `midi_processing.h`, `midi_lfo.h`: up to 16 generators, 128 bindings and 256 parameter deliveries. `midi_bridge.h` parameter locks have 16 slots. | Size generator/binding data from the actual graph. Admit complete logical parameter transactions with their terminal/undo state before sending the first message. Keep CPU scheduling limits separate from memory availability. |
| F8 | `midi_looper.h`: default 512 events, accepted range 1..4096; three generations; each generation reserves event bytes (`events * 8 + 64`) and an index (`events * 32`), plus shared scratch (`events * 32`) and order (`events * sizeof(unsigned)`). Also 32 held notes and 64 beats, fixed hold scratch, capture and ownership reservations. | Keep active/undo/candidate roles, allocating their actual contents lazily. Use bounded-work growth for the candidate and prepare playback/index/release resources before publication. Do not allocate or finalize disk data at the first wrap. Review phrase-length and held-note policies separately from storage caps. |
| F9 | `midi_loop_capture.h`: 32 source/channel decoder states, 1792 bytes reserved by `reserve()`. `midi_ownership.h`: capacity-sized keys and two claims per key; looper asks for 640 keys, arpeggiator for `events * 16 + 128`. | Allocate decoder contexts for actual source/channel use and ownership credits for admitted voices. Share credits without reserving every hypothetical output. Keep credits alive until the final note/pedal release is accepted. |
| F10 | `midi_arpeggiator.h`: default 128 events, maximum 4096; held/snapshot vectors reserve event capacity and routed voices reserve `events * 16`. Inline 16 output voices. Pending settings record is 50 bytes; groups have 6 packets. | Size output state from selected outputs; grow captured data and routed state through admission. Keep the six-message atomic parameter group and serialized record layout unless their formats change. Account for nested expression groups, retired banks and shuffle state. |
| F11 | `midi_file.h`: recorder queue reserves/caps at 256; parameter controls `[16][6]`; prepared position/token arrays of 16; musical import has track/held-note ceilings. Cached reads in `midi_files.h` allocate a 512-byte sector. | Derive track and playback preparation storage from the file, decoder/output state from actual use, and pending recording capacity from available admitted bytes. Keep sector-aligned I/O scratch where justified, acquired before playback. Do not confuse fixed encoded event size with an item limit. |
| F12 | `midi_monitor.h`: opening allocates all 128 history entries, 512 decoders (`2 * 16 routes * 16 cables`) and 16 connection records at once; closing frees them. | Retain allocation only while open, but grow history to its explicit retention policy and create only active source/cable decoders and connections. An unavailable monitor must not affect MIDI delivery. |
| F13 | `ui/font_assets.h`: 32 active and 32 pending font metadata records, 256 KiB file limit. `ui/font_storage.h`: at most 32 sizes per asset, 256-entry size-seen scratch. Pixel data already uses actual-length buffers. | Size metadata from the required font set. Separate file-format limits from memory policy. Stage required pixels before setup publication, sharing pinned identical assets. Retain compact validation scratch if justified by the size field's domain. |
| F14 | `hardware.h`: panes have 16 lines, each text line 64 bytes, and 4 circles. Controller, switches and menu rendering commonly instantiate 3 panes. `ui/renderer.h` stores saved panes per screen and a geometry-sized transfer buffer. | Size composition storage from actual viewports and content requirements, reuse it between frames, and include stack copies in RAM accounting. Keep display geometry constraints explicit. Audit encoded label/line limits before changing them. |
| F15 | `ui/pages.h`, `ui/prompts.h`, `ui/input.h`, `auxiliary_switches.h`: fixed prompt/line buffers, 8 input events, 4 auxiliary states and 32 events. `PageSettings` has 4 expression assignments and 4 tempo preset slots. `clock_generator.h`/tap timing use bounded sample history (up to 8 tap intervals); averaging has its own policy ceiling. | Derive actual input/expression/preset counts from configured controls and capabilities. Size queues for admitted input work. Preserve semantic gesture/timing rules; treat history length as a policy rather than reserving all possible controls everywhere. |
| F16 | `cable_test.h`: eight probe-history entries per configured output. `panic.h`, `expression_pedals.h`, `relay_outputs.h`: vectors sized from actual endpoints/ports/relays. | Review probe history as a retention policy. Keep existing exact-count allocations; do not replace already demand-sized storage. Ensure safety features have their needed state before a performance begins. |
| F17 | `text_messages.h`: each active input contains an 80-byte message buffer, visible text has space for 80 characters plus terminator. `keyboard.h`: six keys per USB boot-keyboard report. | Keep the text limit as the existing message contract unless separately changed; allocate only actual parsers. Keep the six-key wire report. Make the separately growing held-key ownership collection safe. |
| F18 | `documents.h`, `revisions.h`, `setup_files.h`, `songs.h`, `midi_files.h`, UI pickers: fixed names, labels and paths, commonly 64/96/128/160/256 bytes. Songs and setlists themselves are streamed, not fixed-count arrays. `SlotStorage` limits readers to the supplied handle count. | Inventory each encoded field versus display scratch versus runtime path. Replace arbitrary runtime capacity with checked actual-length storage where useful. Coordinate any record-format change with producer/consumer validation; do not silently truncate or introduce legacy and new paths. Allocate actual simultaneous reader handles. |
| F19 | `usb_config.h`, `usb_session.h`: default 4096-byte blocks, 1024-byte text limit, multiple block/frame/reply arrays. Animation receiver/router have block/text/header scratch; player uses a 320-byte pixel stream buffer. Transfer handlers also use block-sized workspaces. | Share or acquire transfer workspaces according to real simultaneous lifetimes, while respecting in-flight USB buffers. Keep framing fields and bounded streaming. Movies remain on storage; allocating a whole movie is not the objective. |
| F20 | `startup_counter.h`: 512-byte records and two persisted records; packet fields, channel/value domains, CRC/LFO lookup tables, compiled bitmap fonts. | Retain journal redundancy, wire formats and immutable ROM tables. They are not reservations for unused feature instances. Document them as justified fixed storage rather than claiming every constant must disappear. |

Host-side limits must change alongside these consumers. In `cntrl/patch_switcher/`,
audit `models.py`, `layout.py`, `compiler.py`, `midi_processing.py`, `looper.py`,
`arpeggiator.py` and `setup_bundle.py`. These include page/instrument/cycle/button/
endpoint counts, route `via` lists capped at 16, LFO destinations capped at 128,
echo repeats capped at 64, event limits, encoded setup/patch counts up to 65535,
and font/window/name constraints. The stream estimator currently includes large
fixed allowances (163840 or 196608 bytes, plus 4096); replace these with actual
configured and admitted costs. Distinguish a semantic repeat limit from an
eagerly allocated repeat buffer. Search model validators and binary encoders as
well as C++ arrays. A number encoded in an 8/16-bit field remains a representation
constraint until that representation is deliberately changed.

## Allocations and frees after initialization

These are possible paths, including first-use allocations and reconfiguration.
They are not all exercised on every pedal press. Host transfers are included
because the pedal can still be performing when they arrive.

`clear()`, `erase()` and `pop_back()` generally retain a vector's own capacity,
but can free nested containers or objects. Growing `reserve()`, moves replacing
old owners, `unique_ptr::reset()`, and destructors can free memory elsewhere in
the call tree. Placement construction inside an existing buffer is not a heap
allocation; the buffer's acquisition and release are.

| ID | Allocation owner and player-visible trigger | Allocation/free path and concern |
| --- | --- | --- |
| A1 | `patch_executor.h`: start a patch, follow a branch, execute nested work. | Raw `realloc` grows frames/resources/bindings, initially in batches of 8 then doubling. Source collection can grow during execution. Destruction frees buffers. A later growth failure must not leave earlier output without its cleanup. |
| A2 | `switches.h`: bind controls, enter an editor/setup, press a configured switch. | Document copies use `MemoryBuffer`; pattern/resource arrays use `realloc`; per-action resource collection grows. Rebinding currently retires/reset old work before all new allocation succeeds. Stage first, then commit. |
| A3 | `patch_sources.h`, `setup_table.h`: cross-setup patch, song selection, setup browsing/activation. | Resident document buffers, setup/name tables, prefill data and pinned-source vectors grow; unpinned residents can be evicted/freed. Acquire both source storage and pin bookkeeping before exposing a source. |
| A4 | `document.h`: document validation/load and patch resource collection. | Graph traversal uses temporary pending/visited vectors; graph loading builds nested route data and plugin objects. This is reachable from patch preparation, not only boot. Budget traversal scratch and reject before executing. |
| A5 | `documents.h`, `revisions.h`, `history_import.h`: edit, receive, compare, import or select a document. | Data/staging/committed buffers, validation temporaries, comparison remaps and whole-file import storage allocate; replacements and cancellation release them. Preserve the active document and pinned readers until atomic publication. |
| A6 | `bundled_storage.h`: publish a bundle or rebuild prefill choices during a save/import. | Sizes, manifests, prefill flags and a capacity-dependent bitset allocate. Include this temporary algorithm workspace and old-plus-new peak, not just the resulting documents. |
| A7 | `midi_processing.h`, `midi_lfo.h`: load/select a stream or a foreign setup's graph. | Nested route/input/step/parameter vectors, mappings, processor factories, LFO objects and file players allocate. Removing/replacing routes destroys nested data and file handles. Prepare the whole candidate graph without disturbing the current graph. |
| A8 | `midi_processing.h`: receive notes/pressure, route delayed work, emit LFOs, reverse, stop or retire sources. | Notes/voices/events/holds/bindings/captures/parameters/visits grow. Pressure capture, source retirement, release notification and reverse processing also construct temporary vectors. Release paths themselves therefore currently have potential allocation. Reverse sorting can request library scratch. Replace with admitted records and reusable workspace. |
| A9 | `midi_state.h`: send the first note/controller/parameter on an output/channel, reset state or forget a source. | Nested output/channel/note/controller/parameter vectors grow in state recording after a physical write has succeeded. Erasing nested channels can free memory. Reserve state before the write; failure afterward cannot undo bytes already sent. |
| A10 | `midi_pickup.h`: first live CC, pitch bend, pressure or parameter selector for a source. | Entry and selector vectors grow. Queued packets can retain counter pointers, so relocation and lifetime must be addressed, not just allocation failure. |
| A11 | `midi_ownership.h`: enable/reconfigure looper or arpeggiator, change their required ownership capacity. | `reserve()` allocates new keys/claims, copies live records, then swaps and frees old buffers. This needs old-plus-new peak accounting. Runtime placement of a key/claim within admitted space is allocation-free. |
| A12 | `midi_looper.h`, `midi_loop_file.h`, `midi_loop_capture.h`: configure/enable, arm recording, clear/disable, load/save or replace a generation. | Several `MemoryBuffer` allocations plus recorder preparation occur; failed configuration can release previously resized buffers. Capture/overdub/reversal use reserved storage rather than direct per-event heap allocation today. Preserve that deadline property while making reservations demand-sized. |
| A13 | `midi_arpeggiator.h`, `arpeggiator_selection.h`: enable/apply settings, play/capture a phrase, publish a bank, shuffle, stop or disconnect. | Outer vectors reserve/grow; segment expression groups contain nested vectors; snapshot copies, retired banks and shuffle resizing allocate. Clearing/swapping banks frees nested data. Applying settings currently stops old work before unchecked reserves complete. Prepare replacement state and shuffle workspace before the next grid deadline. |
| A14 | `midi_file.h`: start/play/repeat/cancel a file or record MIDI. | Track/note/sounding/pedal vectors grow, including after accepted output; recorder reserves 256 pending records on start. Player replacement/destruction frees vectors. Admission must cover terminal releases and future file-read workspace. |
| A15 | `midi_files.h`: first read, open an independent stream, stop or replace it. | Cache first-use `resize(512)`, filesystem-created source/index handles and a `StreamFilePlayer` allocate. Reset/destruction cancels/closes/removes its index and frees state. Pre-open/pre-read before starting audible playback. |
| A16 | `clock_generator.h`: first clock start or new selected output range. | `pending_.assign()` can grow to the highest selected output plus one. Acquire clock state before Start; clock servicing and Stop must not depend on heap growth. |
| A17 | `keyboard.h`, `counters.h`: press a new owned key or execute a counter for the first time. | Held-key and counter-state vectors grow. Validate the report and reserve ownership before accepting a press. Counter failure must not partially advance a patch. |
| A18 | `text_messages.h`: receive a new source/cable SysEx sequence. | Input parsers are pushed on a new start, even before all identifying bytes are known; completion/invalid input/source loss erases them. Bound parser admission by available bytes and release it safely on malformed traffic. |
| A19 | `midi_monitor.h`: open/close its page. | Allocate/free the complete `Storage` object through `MemoryBuffer`; placement construction itself does not allocate. Existing failed-open handling is useful, but the storage is much larger than actual decoder use. |
| A20 | `ui/presentation.h`, `ui/renderer.h`: render a frame or configure displays. | `present()` creates merged-pane and line-count vectors when viewport and pane counts differ, allocating/freeing per call. Renderer screen/transfer vectors grow during configuration. Reuse geometry-sized workspace, including the merge case. |
| A21 | `ui/page_host.h`, `ui/application.h`, `ui/pages.h`, `expression_pedals.h`, `relay_outputs.h`: configure controls/pages, switch setup or render a selected page. | Actual-count vectors and callees can allocate. Polling expression/relay changes has no necessary collection growth of its own, but output callbacks enter A9/A10. Audit the complete callback chain. |
| A22 | `ui/font_storage.h`, `ui/font_assets.h`: select a setup with fonts, stage/validate an upload, download an asset. | Pixel buffers and temporary file-read buffers allocate; finishing or cancelling staging releases old/pending assets. Keep active pixels pinned until the new setup is committed and old rendering is finished. |
| A23 | `animation/color_cycle.h`, `animation/receiver.h`: start splash/idle playback, wake/change movie, transfer an animation. | Per-stream colour state is emplaced/popped by `begin()`; download reads allocate temporary buffers. These are runtime changes even though full movies are streamed from storage. Prepare colour state before playback and deny a new transfer safely. |
| A24 | `storage_listing.h`, filesystem implementations: browse into directories, enumerate, cancel or close a picker. | A `unique_ptr` level/reader per directory and path strings allocate. Walking back/destruction frees them, potentially as a nested chain. Budget depth/work and avoid expensive recursive retirement in a deadline-critical callback. |
| A25 | `bundle_transfer.h`, `disk_speed.h`, `songs_transfer.h`, `history_import.h`: commands while the pedal is running. | Chunk/download/import `MemoryBuffer` instances allocate and free; file operations may allocate transitively. Suspend or reject new transfer work under pressure without impairing active sources. |
| A26 | `ui/song_sources.h`, songs/setup UI, `ui/firmware_visit.h`: capture a song source, launch/return from another setup, load/save navigation. | Embedded document buffers and source pins acquire storage; clearing/failure releases them. Executors and document loaders add A1/A3/A4. Dynamic snapshots must survive the complete round trip and explicitly encoded persistence. |
| A27 | `midi_stream_timeline.h`, `midi_processing.h`: construct/reverse event timelines. | Vectors for voices/frames/result/tokens and `std::stable_sort` temporary storage may allocate. Budget reversal as a candidate operation; do not replace a playing sequence until all work succeeds. |
| A28 | `cable_test.h`, `panic.h`, renderer, expression and relay constructors: creation or future dynamic recreation of feature objects. | Several vectors are currently initialized once from actual capabilities. Making these objects lazy turns their construction/destruction into runtime allocation. Pre-admit safety dependencies; do not accidentally make Panic unavailable at low memory. |

Also inspect allocator calls made by registered processor implementations and
libraries, including filesystem open/close, strings, aligned allocations and
sorting. Processor registration currently uses a growing registry; establish
whether registration is boot-only and keep it out of performance callbacks.
See the target appendix for driver-specific boundaries.

## Failure contract

Finite RAM cannot guarantee accepting unlimited notes, indefinitely growing
recordings or every requested setup. The guarantee is that memory exhaustion
does not corrupt or interrupt already admitted work. It must be possible to
reject the new operation while existing playback, clocks, note/pedal releases,
Stop, source retirement and user controls continue with their established timing.
This is a requirement to verify, not a guarantee the current implementation makes.

| Operation that runs out of memory | Required result |
| --- | --- |
| New setup/page/graph/font set | Keep the active setup, its navigation and pins. Report that the replacement could not be prepared. |
| Patch start or subsequent patch step | Refuse the failed unit before its first effect. Follow the patch-step rules below for earlier effects and cleanup. |
| Additional routed note or parameter transaction | Reject the new unit before output. Existing voices/holds/transactions continue, including complete release/terminal messages. Never send half an NRPN because bookkeeping failed. |
| Looper candidate, overdub or destructive edit | Abandon only the unsuccessful candidate. Retain the published take and existing undo generation; release candidate-owned notes without releasing other sources' notes. |
| First recording with no published take | End the unsuccessful take cleanly and show the capacity error. Keep unrelated MIDI operation running. Do not silently save a truncated take as successful. |
| Arpeggiator replacement/capture | Keep the published phrase and settings. Reject the new candidate without leaving a dangling segment/voice reference. |
| Monitor, browser, animation, transfer or optional cache | Refuse the new operation or discard only unpinned expendable cache data. Keep active font/pixel/file/source dependencies. |
| Cleanup, note-off, Stop or Panic | Require no new general heap memory. Use resources already acquired when the corresponding work was admitted. |

Error reporting itself must not allocate: a bounded status code/message and
existing display/status transport suffice. Avoid repeated modal errors for every
incoming event; latch the condition with a dropped/refused-work count. Recover
when resources are released, without automatic partial re-execution of a failed
gesture. Do not trigger a broad MIDI panic merely to obtain free memory.

## Allocation and ownership design

### Pair repeated MIDI notes under pressure

Pair note-offs FIFO by input, cable, channel and pitch, including note-on with
velocity zero. MIDI supplies no strike ID for repeated notes. Once an onset for
a key is refused, close admission for that key until its accepted strikes and
refused strikes have drained. In its existing key record, count subsequent refused
onsets. Each off releases the oldest accepted strike first, then consumes one
refused count silently. Keep this record until both counts reach zero. Queue
accepted releases using their reserved delivery slots even if the output is busy.

If the first onset for a previously unseen key cannot even acquire a key record,
use an allocation-free flag on that input/cable to refuse further onsets until
an explicit source reset or reconnect establishes a new input session. Continue
servicing every accepted note, pedal release and clock. Use the same fallback on
counter saturation without wrapping the count. Show which input needs a reset.
This replaces the note-limit branch that retires the whole source on overload.

Test A accepted, B refused at the same pitch, output blocked, then two offs.
The first off queues exactly A's release and the second emits nothing. Try a
third strike between the offs, drain its off, unblock output and check the wire
trace and subsequent retrigger. Repeat with an independent source on that pitch
and with allocation forced to fail for a previously unseen key.

### 1. Establish one checked allocation boundary

Extend the existing memory facilities into a checked allocation interface used
by all firmware-owned dynamic storage. Preserve explicit failure results and
old-buffer retention on failure. Cover raw arrays, nested collections, object
construction and aligned buffers, not just `MemoryBuffer` payloads.

The current `memory_used` ledger counts only `MemoryBuffer` payload bytes.
`std::vector`, raw `realloc`, library allocations and allocator metadata bypass
it. Correct the accounting before treating it as an admission authority. Use
checked count/byte arithmetic, actual capacities, alignment and allocator overhead.
Account for temporary old-plus-new storage during growth, staged configuration,
reverse/undo and pinned asset replacement. Track both available bytes and the
largest usable block, plus failures and peak use by owner/lifetime.

Target exception settings and `operator new` behavior make ordinary STL growth
unsafe as a universal solution. A throwing allocator plus `catch bad_alloc` is
not sufficient. Select a small checked collection/object pattern consistent with
the existing code; avoid a parallel unchecked route for the same data. Audit
library scratch allocation independently.

For live growth, prove a bound on allocation and release work. General-purpose
`malloc`/`realloc` must not be assumed to meet MIDI deadlines. Use a shared
byte-budget allocator with bounded operations for admitted performance records,
and move preparation/reclamation outside interrupt and output-commit paths.
Select its implementation after baseline measurements; no new dependency is
required by this plan. Allocator size classes or blocks may be fixed implementation
units, but must not recreate separate arbitrary limits for controllers, notes or
features. Do not enable live growth until worst-case timing is qualified.

### 2. Choose storage by access pattern

Do not mechanically replace every vector with `std::list`. Select one intended
representation for each collection, using these defaults:

| Data and current examples | Intended storage |
| --- | --- |
| Known configuration counts: controllers/pages, document instruments/cycles, stream routes and nested steps/inputs/fields, font metadata, setup tables, ports, file track metadata and clock outputs | Count/validate first, then allocate an exact-size contiguous array and fill it. A vector constructed at its final size is suitable; an owning C-style array with an explicit count is also suitable where fixed size and direct indexing make the ownership simpler. No append-driven capacity growth while the configuration is active. |
| Append-heavy recording/capture/history: loop events and indexes, arpeggiator candidates and expression groups, monitor history, pending MIDI/recording queues | Prefer segmented storage that acquires another block without copying existing blocks. Preserve chronological iteration and use block-aware indexes/cursors where random access is needed. Published blocks remain stable and pinned until their readers retire. |
| Independently retired, stable-address records: active voices/holds, source pins, pickup entries/selectors, held-key owners, sparse decoder/state records | Prefer segmented stable slots; use `std::list` where frequent insertion/removal or transfer of nodes makes it simpler and traversal is acceptable. Include nested collections in this decision. Avoid a node allocation per record when block storage is smaller or faster. |
| Ordered scheduled events and live bindings | Evaluate list insertion against segmented storage with an explicit ordering index. Preserve ordering and bounded service work; do not trade a vector copy for an unbounded linked-list scan on every deadline. Allocate any ordering workspace through admission too. |
| Indexed playback work: shuffle order, reversal ordering, snapshots and graph-validation scratch | Allocate reusable contiguous scratch from the actual phrase/graph size before the operation. Use segmented capture as input and prepare an exact-size candidate index when required. If a larger contiguous replacement is necessary, apply the retained-reallocation rules below. |
| Render composition, screen metadata, transfer buffers and file-sector cache | Allocate once from actual geometry/format requirements before first use, then reuse. Eliminate the per-frame temporary vectors in `present()`. |

An owning runtime-sized C-style array means one checked allocation plus an
explicit count and deterministic destruction, not a variable-length stack array,
an arbitrary maximum-sized array or an unmanaged pointer. Construct and destroy
nontrivial elements correctly, including rollback after partial construction.
Reuse existing ownership facilities where suitable; do not introduce duplicate
array/container implementations for equivalent requirements.

For configuration whose encoded size is not immediately available, use a counting
pass or a staged builder, then publish final-size storage. Count nested payloads
as well as the outer collection. Keep already exact-sized vectors when changing
their type would add no useful saving. Reconfiguration creates a separate candidate
and may require old and new configurations simultaneously; it must not resize the
active configuration in place.

Segment blocks must not themselves be indexed by a vector that repeatedly moves
an ever-growing block directory during performance. Use stable links or a checked
directory whose growth is separately prepared and budgeted. Choose block sizes
from record size, allocation overhead and measured service latency; unused space
in the last block remains part of the memory charge. Do not provision a full
maximum recording or hypothetical feature count to avoid directory growth.

`std::list` avoids relocating existing nodes, but each node has links and allocator
overhead, and insertion normally allocates. Ordinary list insertion does not
provide a checked, nonthrowing out-of-memory result on this target. Use it only
where nodes can be prepared safely before admission and then linked/transferred
without allocation, or where a verified failure-safe construction path exists.
Allocator-compatible `splice` can transfer prepared nodes without allocating;
the source/destination allocator and node lifetime requirements must hold. Do not
assume a custom STL allocator returning null makes insertion safe. Where this
cannot be guaranteed, use checked segmented storage instead. Lists are not a
replacement for contiguous pixel/USB/DMA data or indexed shuffle tables.

Retain a vector/reallocation when its indexing, locality, memory efficiency or
required contiguous interface outweighs the alternatives. For each retained
performance-time growth site:

- State why exact-size preparation, reusable scratch, a list or segmented storage
  is unsuitable. Include nested vectors and raw `realloc`, not only named vectors.
- Charge the old allocation plus the complete replacement, allocator overhead and
  any staging work. Do not assume in-place `realloc` or a particular vector growth
  factor, and never depend on moving accepted work to recover memory.
- Prepare growth before an irreversible effect or deadline, preserve the old
  contents on failure, and ensure no outstanding references become invalid.
  If the copy itself is necessary, account for its worst-case service time.
- Qualify the allocation/failure path on the target. A logical item ceiling or a
  successful host test is not evidence that live reallocation meets the deadline.

The objective is to remove avoidable full-buffer growth peaks and copying during
performance. Necessary, measured and safely admitted reallocation remains allowed.
Note-release, Stop, Panic and established cleanup obligations retain their stronger
requirement of no new general heap allocation.

### 3. Admit work before effects

Prepare each atomic operation, including output bookkeeping, undo information,
source pins and eventual release/retirement capacity. Commit through non-failing
state changes at a safe point. On failure, destroy only the staged candidate.
Never clear the active state first in order to attempt a replacement.

Patch start admits the executor, its initial frames, required source pins and
cleanup state. Each later effect is a separate admission point. A delay consumes
its existing timer state. A multi-message parameter write reserves the complete
group and its retry bookkeeping before sending. File start and stream selection
prepare their player or graph before publishing it. Failure stops the patch at
that step, skips later steps, reports the failing step and retires patch-owned
temporary work through reserved cleanup. Already delivered program changes and
completed steps stand. Keep the previous player or graph if its replacement fails.
Test program change, delay, failed stream selection, then a final send. Expect
only the program change on the wire, the original stream still playing and a
visible partial-patch error. Retrying requires another explicit invocation.

Every accepted note/hold/transaction must own enough cleanup capacity for all
outputs it can reach. Those resources remain unavailable to unrelated new work
until cleanup completes, including under output backpressure or disconnect.
Plan for transactions whose wire bytes cannot be recalled. Move A9/A14 state
admission ahead of successful writes and keep retry progress without allocation.

Replace capacity-dependent owner numbers and masks with stable references.
Collection growth must not invalidate arpeggiator `Voice.segment`, pickup counter
pointers, document views, queued resource references, navigation or DMA pointers.
Choose stable storage/lifetimes where needed, rather than adding indirection to
every value. Delay reclamation until outstanding readers and callbacks retire.

### 4. Size storage by lifetime

- Setup lifetime: actual controllers, switch states, topology, configuration
  cursors, page metadata and required fonts. Stage all before activation.
- Active-work lifetime: histories, decoder contexts, ownership claims, queued
  events and phrases. Grow only through checked admission with cleanup credits.
- Operation lifetime: reversal, import, validation, browsing and save/load work.
  Budget the peak alongside active playback, and perform bounded work per service
  call where the operation shares the performance loop.
- Frame lifetime: reuse composition/merge scratch allocated for actual geometry.
  Steady-state rendering should not allocate or free.
- Core lifetime: minimal failure reporting, clocks, release delivery and hardware
  service state. Keep only the capacity justified by active obligations or the
  physical/wire contract, rather than speculative future feature use.

Free inactive feature data when safe; do not shrink collections on every event.
Reuse storage where its owner remains active and the memory use is intentional.
Under pressure, reclaim only unpinned optional data with a bounded reclamation
cost. Active takes, undo, ownership, source pins and release credits are protected.

### 5. Make admission visible to the host

Update compiler estimates from measured representations and actual configured
counts. Distinguish committed setup bytes, active performance use, staging peak,
available growth and platform-specific memory regions. Include allocator overhead
and temporary copies rather than subtracting the old fixed reservations twice.

Remove item caps only when their binary fields, masks, loops and owner mappings
support the new counts. Keep the existing event setting and counting rules.
Successful arm guarantees capacity for the configured number of candidate events.
Acquire usable blocks for those events and their required indexes, publication
scratch, capture decoder/ownership state and terminal records before acknowledging
arm. Candidate population can then be lazy within those private blocks. For
overdub, count retained events toward the combined candidate limit. Apply this at capture
start for the arpeggiator and at record/overdub arm for the looper. Charge only
the incremental requirement where storage is already owned. Release unused
blocks when capture ends. A setup with an idle looper pays no reservation for a
future take. Report idle cost, incremental arm requirement and active/undo bytes
separately in the host estimate. At 512 events, test opening the monitor after
arm under memory pressure. The monitor can be refused and the 512th event must
still fit. Test event 513 using the candidate-failure policy, and test refusal
at arm with the published take and undo intact.

## Implementation sequence

Each step should be independently reviewable and preserve existing behavior
except for the intended removal of limits and documented failure responses.

1. **Deliver the one-switcher saving.** Measure a one-switcher setup on the
   baseline. Stage exactly the configured controllers with checked allocation,
   keeping owner numbers, four-controller validation and visit serialization
   for this milestone. Test one and four switchers, navigation and a failed
   replacement. Report before/after RAM1 and RAM2 totals, including heap use
   and the replacement peak. Require a net steady-state saving for one switcher.
   This milestone can ship independently of stream cleanup and allocator redesign.
2. **Remove invocation reservations.** Replace the twelve snapshots with actual
   invocation state, migrate owner references and persisted navigation, then lift
   the controller ceiling together with host validation and budgeting. Exercise
   save/return and more than four switchers before removing the old mapping.
3. **Measure and establish failure tests.** Instrument the linked allocator,
   aligned allocations and stack/region measurements. Record boot, one-controller
   setup, maximum practical setup, file playback, loop/overdub/reverse, arpeggiator,
   monitor, transfers and setup/song return peaks. Add deterministic allocation
   failure injection through the same checked boundary used by the target.
4. **Make memory failure transactional.** Implement checked storage and ownership
   admission. Fix post-write bookkeeping and cleanup allocations first (A8-A11,
   A14). Define bounded allocation/reclamation timing and prove protected cleanup
   works when ordinary allocation always fails. Select segmented/list storage for
   growing performance records using the table above, and record justified retained
   reallocations. Keep old feature limits until safe.
5. **Make topology and optional tools demand-sized.** Finish F1-F4 with exact
   document-sized cursors, history and switch states. Address F5-F7 and F12-F19,
   actual-output/cable parsers, font metadata, reader handles, USB workspace
   lifetimes and reusable rendering scratch. Count configuration data before
   allocating final-size vectors or owning arrays, including nested collections.
   Preserve hardware/protocol bounds called out in the appendix and F20. Remove
   masks before removing their caps.
6. **Make musical buffers demand-sized.** Migrate F8-F11 using admitted event and
   ownership storage. Prefer segmented capture/queues and stable records; prepare
   exact-size playback/shuffle/reversal indexes before use. Keep wrap/grid paths
   allocation-free or demonstrably bounded and release paths free of new general
   heap allocation. Reserve candidate capacity at arm and retain undo on publication.
   Preserve existing playback
   during failed capture, reverse, load and configuration.
7. **Close remaining runtime paths.** Work through A1-A28, nested STL objects,
   file/driver boundaries and destruction. Remove remaining unchecked growth and
   arbitrary caps, revise compiler estimates, and reconcile instrumentation with
   the source inventory. Document each fixed allocation intentionally retained.
8. **Qualify exhaustion and timing.** Run the tests below and targeted physical
   measurements before claiming live safety. Update this plan with evidence and
   remaining physical checks; do not mark source changes as hardware qualification.

## Verification and completion criteria

Use the same target, setup and input trace for the baseline and each proposed
container. Run a 16-bar loop at 120 BPM with MIDI Clock at 24 PPQN, repeated
same-pitch notes, sustain, delayed routing and monitor redraws. Repeat capture
and first wrap 32 times, crossing block boundaries and forcing a failed growth.
Use the receiver to timestamp clock and first-wrap events and instrument the
firmware's longest MIDI service gap. Measure peak bytes alongside timing.

Proposed initial pass limits are a 1 ms maximum service gap, 1 ms maximum
absolute clock interval error relative to the configured period, and 1 ms
maximum first-wrap lateness relative to its scheduled boundary. Require zero
missed ticks or accepted releases. Exercise deliberate output backpressure in
a separate run, measuring service gap during blockage and delivery order after
unblocking. Record baseline maxima first. If the baseline misses a proposed
limit, report that separately and settle the target before choosing a container.

- Fail each allocation in turn during every A1-A28 path, including nested
  construction, growth, aligned allocation and optional library workspace. Verify
  rollback, unchanged active configuration/take/undo, correct source lifetimes
  and no leaks. Native `bad_alloc` handling alone is not a target-faithful test.
- With ordinary allocations forced to fail, release overlapping notes/pedals,
  retire sources, stop clocks, cancel patches and exercise Panic. Verify cleanup
  does not release another owner's notes and survives output backpressure.
- Exercise new output/channel/parameter state and multi-message groups under
  failure before and between write retries. Assert no output starts without its
  complete bookkeeping and no accepted release depends on new allocation.
- Exercise first wrap, capture overflow, overdub, undo, reverse edit/direction,
  arpeggiator shuffle/bank changes, file repeat and song/setup return while
  allocating optional UI/transfer state. No partial candidate publication.
- Exercise fragmentation and growth requiring simultaneous old/new blocks.
  Distinguish largest-block failure from total exhaustion. Verify bounds and
  counters with near-overflow sizes and pinned assets.
- Trace capacity changes during performance. Verify configuration arrays/vectors
  keep their size/capacity after publication, segmented growth does not copy or
  relocate existing records, and list/node admission remains failure-safe. Exercise
  block boundaries, ordering, index lookup and stable references across growth.
  Every retained vector/raw-buffer reallocation needs a documented reason, a peak
  memory measurement, failure rollback and a worst-case timing result. Avoidable
  reallocation is a defect; a justified safe reallocation is not.
- Compare memory use for zero, one and several feature instances. No unused
  controller/snapshot/full recording generation reservations. Test counts beyond
  old limits when RAM and service time allow. Verify real protocol limits remain.
- Verify no allocation/free per steady-state rendered frame, note release or
  clock tick, and no unbounded allocator/reclamation work in critical service
  paths. Measure worst-case stack use as well as heap use.
- Run relevant native and Python regression suites and target builds as changes
  are implemented. Run cross-platform qualification through manually triggered
  GitHub workflows at coherent checkpoints, never on every pushed commit.
- Separately qualify physical MIDI timing, display/DMA lifetimes, filesystem
  latency and repeated low-memory recovery on the target. This document does not
  authorize flashing or running the application as part of the planning task.

For source coverage, repeat searches for `new`, `delete`, `malloc`, `calloc`,
`realloc`, `free`, `make_unique`, owning strings/containers, collection growth,
fixed arrays, `capacity`/`max_` constants and host validators. Follow constructors,
destructors, moves and callbacks from each performance entry point. Compare the
result with linked allocator traces; a clean explicit-`new` search is insufficient.

Completion means all inventory rows are migrated or have a documented retained
constraint, all post-initialization owners have checked failure behavior, and
already admitted performance is proven to survive allocation failure within its
timing budget. It does not mean promising unlimited capacity.

## Additional work beyond the prompt

None.
