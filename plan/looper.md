# Live MIDI looper

## Objective and proposed scope

Add a live MIDI looper page, building on the existing
[recording and playback support](../doc/midi-files.md). A performer records a
phrase, hears it repeat, adds an overdub, undoes the last overdub, stops or
clears the loop, and saves or loads a MIDI file.

The user authorized implementation, shared recorder/player and ownership changes,
and the RAM2 revision below. Flashing firmware and physical qualification remain
separate. The scope is one shared loop, one-level overdub undo and beat-synchronized
operation. Multiple looper pages show the same session.
Multitrack arrangements, independent loop lengths, audio looping, an event
editor, time stretching and additional playback effects are outside this plan.
Existing file recording and playback keep their documented behavior.

## Approved implementation revision

The SD prototype could cache the first prepared records, but synchronous SD
writes and sync still prevented a bounded first wrap during finalization. The
user approved bounded RAM2 generations instead, for notes and pedals over up to
64 quarter-note beats, with 512 events per generation by default. The shared
prepared-index format and SMF reader/writer remain in use. SD is used for explicit
Save/Load, whose physical latency still needs qualification.

The implemented limits are 32 simultaneous notes/holds, 32 controller decoder
contexts and 16 tracks for SMF import. The default shared RAM2 allowance is
113,344 bytes, including working generations, scratch/index/order buffers,
ownership, recorder/import allowances and allocation overhead. Setup compilation
and native validation account for this once, independently of the number of views.

The user also specified that explicitly bound Clear and Load actions execute
immediately. Page controls retain their confirmation prompts. See the
[user documentation](../doc/midi-looper.md) for configuration and behavior.

The user subsequently requested both an undoable Reverse phrase edit and an
independent playback direction toggle. Both use the shared prepared-event
reader and existing RAM2 buffers. Reverse edits publish at a loop boundary;
stopped edits prepare without waiting for a clock. Direction changes preserve
the stored phrase and Undo, and disable overdubbing while playing backward.

## Implementation and qualification status

The software implementation is complete. Host regressions cover a full
512-event take, multiple overdub passes, one-level Undo, 4,000 irregularly polled
cycles without accumulated drift, output backpressure, ownership cleanup,
controller transactions, transport changes, raw running status, USB cables,
storage failures and page confirmations. Cross-platform qualification uses the
existing manually dispatched workflow, including installed and relocated builds.

The target build leaves 10,848 bytes in RAM1 for stack/local variables and
405,376 bytes of RAM2 for dynamic allocation before setup-specific reservations.
The default looper's 113,344-byte allocation is included in that setup budget.
Physical qualification in step 7 remains outstanding. No firmware was flashed
as part of implementation.

## Reuse and required changes

| Existing path | Use in the looper |
| --- | --- |
| `firmware/include/cntrl/midi_file.h` | Reuse SMF decoding/writing, prepared event indexes, playback and cancellation. |
| `firmware/include/cntrl/midi_files.h` | Reuse filesystem adapters, cached reads, exclusive recording names and completed-file discovery. |
| `firmware/include/cntrl/midi_runtime.h` | Reuse timestamped input observation, cooperative service order and output availability. |
| MIDI streams and output scheduling | Reuse source ownership, routing, transaction delivery and bounded cleanup. |
| Shared clock beat position | Use the selected internal/external clock and its transport/restart generation. |
| Named pages and control bindings | Add a looper view and actions through the existing page lifecycle and gesture recognizer. |

Before implementation, the recorder wrote millisecond timestamps as 1000 PPQN
at 60 BPM. Its `finish()` drained the whole queue and synchronized the file
synchronously. The player indexed a completed file before playing, waited for
delivery and note cleanup at each pass end, then reset its elapsed timer. Those
were the concrete gaps for live, tempo-following looping.

Extend these shared implementations with bounded finalization, a reusable
prepared-index handoff and a beat-driven playback cursor. Preserve the ordinary
file APIs' semantics through that common implementation. Do not create a second
SMF parser, recorder, private clock, note-cleanup engine or hardware writer.
Review the required shared timing and ownership changes before implementation;
they must also preserve existing patch playback and stream-file generators.

## Configuration and controls

Register one proposed `midi-looper` page type in the existing named page table.
Use one `page_settings.looper` configuration table for the shared session's
input endpoints, channels and output endpoints. Resolve the same endpoint
aliases used by recording and routing; reject unavailable capabilities when
compiling. Do not introduce a separate looper routing format.

Use frozen Pydantic configuration models, the existing units conventions and
the normal compile/export/download/history path. Determine resource limits from
the measurements below before assigning defaults. Existing file recording and
playback settings remain distinct operations, not alternate looper controls.

Expose Record/Overdub, Play/Stop, Undo, Clear and Save/Load through the existing
control-binding system. Page controls and patch actions call the same looper
commands. Commands start background work and return acceptance or an error;
they do not occupy a patch executor for the lifetime of the loop.

Show state, loop position/length in quarter-note beats, clock readiness,
pending boundary actions, recording/overdub activity, undo availability and
storage/output errors. Show the current action beside each assigned control.
Use existing confirmation or hold gestures for Clear and replacing unsaved work.
Do not add another button recognizer or hard-code a target's switch labels.

## Session and state transitions

| State | Record/Overdub | Play/Stop |
| --- | --- | --- |
| Empty | Arm the first take for the next beat. | No loop to play; show that status. |
| Armed | Repeated requests leave the same start pending. | Cancel arming. |
| Recording | Schedule the first take's end at the next beat, at least one beat after its start. | Discard the unfinished take and clean up. |
| Closing | Repeated requests do not create another take. | Cancel the pending launch; finish or discard storage work safely. |
| Playing | Arm overdub for the next loop boundary. | Stop immediately and release this session's delivered notes/holds. |
| Overdubbing | Close the overdub at the next loop boundary. | Stop playback and discard the unfinished overdub. |
| Stopped | Start playback and overdub together at the next loop boundary. | Start committed playback at the next loop boundary. |

Preparation, saving and cleanup are visible pending substates. A new start waits
for required cleanup and resource preparation. Reject conflicting commands
without partially changing the session. Repeated stop/clear commands are
idempotent. Clear cancels pending work and discards the working loop after
confirmation; it does not delete previously saved MIDI files.

The first take fixes the loop length. An overdub can span several passes and
places new events modulo that length. It does not extend the loop. Continue
playing the committed loop while collecting the candidate overdub. After close,
stop capturing candidate events and show preparation until it can replace the
old loop at a boundary. Preserve the old loop on cancellation or failure.

One-level Undo restores the previous committed version after a successful
overdub, switching at a loop boundary while playing and immediately while
stopped. Do not expose Undo for an unfinished overdub or add a redo/history
system. A pending undo is idempotent and cannot overlap another publication.

## Musical time and loop boundaries

Use quarter-note beat position from the shared clock. The initial command grid
is one quarter note; do not invent a four-beat bar or a time-signature model.
Show Waiting when an external clock is unavailable. Reject recording/launch
until a usable clock and prepared resources are available.

Schedule cycle boundaries from an absolute origin and fixed beat length:
`origin + cycle_number * length`. Tempo changes affect the rate while preserving
phase. Never derive the next cycle from the time the previous cycle finished
writing MIDI. Poll delays, storage work and blocked destinations cannot stretch
the loop or accumulate boundary drift.

Recording stores musical tick positions, with rational conversion from the
shared beat position and a documented PPQN. Retain sub-beat performance timing;
the command grid does not quantize individual note onsets. Define stable order
for simultaneous events, including old-take versus overdub events. Keep the
silence between the final event and the loop boundary in the loop duration.

MIDI Start resets an armed playback run to its loop origin. Clock Stop cancels
unfinished capture and releases owned notes, retaining the committed loop and
its transport-armed state. Continue schedules a transport-paused loop at the
next complete loop boundary. An explicit looper Stop disarms that restart.
Clock loss cancels unfinished capture, stops safely and requires an explicit
Play after clock recovery. Never replay missed cycles or emit a catch-up burst.

### First-wrap handoff

Reserve RAM2 buffers and index capacity before arming. Capture maintains an
ordered candidate index using the shared writer and event representation.
Pending controller writes are inserted while capturing, so the first wrap does
not sort or index the entire take. Boundary releases complete the prepared
cursor, and playback begins before the common writer flattens the generation
to its RAM2 SMF. Playback does not read that unfinished SMF; Save remains unavailable
until finalization completes. No SD operation is required by this handoff.

The initial launch policy rejects a request observed more than one millisecond
past its requested boundary. Host tests verify the RAM-only handoff, rejection
of missed first wraps and 4,000 irregularly polled cycles without cumulative
phase drift. They do not establish physical first-wrap latency or CPU/storage
jitter on the pedal. Those acceptance measurements remain in step 7. A missed
launch reports an error and retains a previous committed loop instead of
starting halfway through a cycle.

## Captured events and output ownership

Capture selected external input events once at the existing timestamped input
tap, independently of MIDI forwarding. Playback, generated MIDI and cleanup
must never feed back into capture. Live monitoring follows the setup's existing
routes; the looper must not add a duplicate thru path. Preserve source/USB-cable
identity for decoding and note pairing before merging the selected inputs.

Initially capture channel voice messages: notes, pressures, pitch bend,
controllers and Program Change. Preserve original velocities and event order.
Clock, transport, system-common and SysEx continue through their configured
services but are excluded from loop content. This prevents a loop from replaying
its own transport or fragmenting a SysEx transfer. Do not change the ordinary
recorder's broader message support.

Define notes at the capture edges: a Note Off without a captured onset cannot
create a voice; notes begun before capture are not invented. Close notes still
held at the loop end with owned boundary releases. Initially do not tie notes
across cycles. Send boundary cleanup before the next cycle's onsets. Physical
releases after the take closes cannot mutate the committed take.

Use shared logical ownership for accepted playback notes and CC 64..69 holds,
including overlap with live thru, other routes and file players. Cancelling the
loop must not release another source's held note or overwrite its newer control
value. Do not use channel-wide panic as normal loop cleanup. Keep cleanup alive
after its page or source document is gone, with bounded pending records.

Preserve complete fine-CC and NRPN/RPN writes through overdub merge and output
scheduling, using the existing parameter transaction/state support. Do not move
or interleave their selector/data bytes independently. Define incomplete groups
at capture boundaries and reject/discard them with diagnostics before playback;
do not guess a selected parameter. Program/bank selections are deliberate loop
events, and stopping does not promise to restore an instrument's old program.

Drain delivery work within the shared scheduler budget. If an output cannot
finish a cycle before its next boundary, retire that destination, cancel its
stale ordinary traffic and retain required cleanup. Other destinations and the
global loop phase continue. A disconnect follows the existing identity and
generation rules; a replacement never receives predecessor cleanup. Require a
fresh Play to rejoin a recovered destination, without replaying a backlog.

## Storage, save/load and resource ownership

Use ordinary SMF files and the existing MIDI directory, filesystem interface
and exclusive naming rules for Save/Load. Live SMFs and prepared indexes reside
in bounded RAM2 buffers, with no proprietary disk format or SD scratch files.
Flatten an overdub into the next RAM2 SMF while keeping the active generation
immutable. Keep only the active version, one previous version for Undo and one
candidate, plus a bounded scratch index and merge order.

Save publishes a completed, synchronized SMF with tempo/tick metadata and an
end-of-track time that preserves the full loop length. A live tempo change must
not change the loop's beat length. At Save acceptance, snapshot the session's
most recent valid clock tempo as the export tempo, preserving musical tick
positions. Load accepts SMF
types 0 and 1 with PPQN timing, using the common decoder and prepared index.
Preserve the positive end-of-track duration, including silence and fractional
beats, rather than rounding imported material to the command grid. Embedded
tempo events do not change the controller's selected clock. Reject unsupported
performance messages before replacing the loop; SMF tempo/end metadata remain
valid. SMPTE files remain available through ordinary file playback and are
outside the initial beat-synchronized looper import scope.

Add MIDI-file selection using the existing page/menu interaction patterns and
filesystem directory service; there is no current MIDI-file picker to wrap.
Loading or replacing unsaved work requires confirmation. Pin the committed
generation during Save and reject overlapping overdub/publication/load jobs;
playback continues. Failed writes, removal of storage
and power loss must not overwrite a saved file or promote a partial candidate.
After reboot, discover only completed saved files; do not automatically play or
promise crash recovery of an unsaved performance. Reclaim only files identified
as a newly created failed save through the existing filesystem API.

Reserve the existing primary recorder/player resources before arming, including
the upcoming record-to-play transition. Ordinary file patches must report Busy
when the looper owns those resources, and the looper must reject arming while
they are in use. Do not silently preempt either caller. Existing independent
stream-file generators retain their normal scheduling and resource limits.

Page changes leave the session running. Setup replacement, stock-firmware entry
and port-owning diagnostics stop it through the shared handoff/cleanup path;
unfinished takes are discarded. Saving a setup stores configuration, not a copy
of the live event buffer. Session resources are released only after pending
cleanup and file operations have reached a safe terminal state.

Measure and enforce maximum events, held notes, loop length, pending input,
index/cache sizes and scratch storage. Include working and undo/candidate
generations, prepared indexes and handoff buffers in target RAM2 planning.
Charge the shared session once and display buffers per configured view.
Reject an unsupported setup before activation. Reaching a recording limit
aborts that candidate with a visible error and preserves the committed loop;
do not drop releases or silently shorten the phrase. Report high-water counts,
late boundaries, output failures and maximum input-scan gaps.

## Implementation sequence and acceptance

1. Add focused regression cases around current recorder finalization, file
   repeat timing and cleanup. Specify the shared beat cursor, resource leases
   and source ownership changes before editing those implementations.
2. Refactor common recording finalization and prepared indexing into bounded
   service steps. Verify existing recording/file playback behavior, then prove
   the first-wrap handoff and cumulative timing with a fake clock and slow
   storage before adding UI.
3. Implement the single-session state machine, immutable candidate publication,
   overdub and one-level Undo. Exercise commands during pending transitions,
   cancellation, full buffers, failed sync and interrupted publication.
4. Add page configuration, compilation/native validation, controls, status and
   save/load. Test compile/export/recompile, setup bundles and history; reject
   unsupported configurations without replacing the current setup. Version the
   compiled looper configuration and use the normal source-recompilation rules,
   without adding a compatibility reader or migration path.
5. Test silence-only loops, exact-boundary events, held notes/CCs, overlapping
   live and loop voices, partial parameter groups, several input cables, tempo
   changes, Start/Continue/Stop, clock loss and timer rollover. Run thousands of
   cycles with irregular polling and assert no cumulative phase drift, duplicate
   capture, stale writes or resource growth. Exercise blocked/disconnected
   outputs independently and verify healthy destinations keep progressing.
6. Run repository-required checks and shared native suites. At a coherent
   checkpoint manually dispatch the existing Windows/Linux/macOS workflow,
   including installed-wheel and relocated export checks. Keep workflows
   manually triggered, not on every push. Build the target and record actual
   RAM1/RAM2 headroom and measured capacity defaults.
7. Qualify with an independently timestamped physical MIDI receiver: first-wrap
   gap, long-run boundary drift, jitter, start/stop latency and note/hold cleanup
   during scrolling, overdubbing, saving and competing MIDI traffic. Exercise
   storage removal and real receiver reconnects. Record captures and practical
   limits; host tests alone do not establish live-loop timing or reliability.

## Additional work beyond the prompt

None.
