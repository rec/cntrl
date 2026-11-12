# MIDI file patches

For beat-synchronized live capture, overdub and Undo, use the
[live MIDI looper](midi-looper.md).

Store MIDI files in `/cntrl/midi/` on the SD card. Playback accepts Standard MIDI
Files types 0 and 1, merges simultaneous tracks, and follows tempo changes.
Both ticks-per-quarter-note and SMPTE timing divisions are supported.

`cntrl setup copy` uploads explicitly referenced playback and looper-load files
to that directory, including file sources in MIDI streams and referenced
companion setups. Put each file beside the source document that declares its
`filename`. Composed documents retain their own source directories. Files with
the same pedal filename must have identical bytes; filenames differing only in
case are rejected so the collection behaves consistently across filesystems.

The host checks all referenced source files before opening USB. Files already
on the pedal with identical contents are reused. Replacing different contents
requires the existing `--overwrite`/`-o` flag. File uploads are staged, checked
and read back before the setup bundle is sent. A failed transfer does not
establish a synchronization baseline. Setup snapshots and backups retain these
MIDI files too; backup restore uploads them before restoring setups.

This requires firmware with the named MIDI-file transfer handler. A CLI update
alone cannot add that handler to an already flashed pedal. Active MIDI file
playback, recording or stream readers must stop before transferring these files.
The `.cntrl-` filename prefix is reserved for transfer staging and recovery.

```toml
[patches.play]
operation = 'play-midi-file'
filename = '255-3.mid'
outputs = ['din', 'trs']
loops = 1
pre_delay = 0
post_delay = 0
speed = 1.0
reverse = false
transpose = 0

[patches.record]
operation = 'record-midi-file'
channels = [1, 2, 3]
inputs = ['din', 'trs', 'usb-device', 'usb-host']
```

`filename` defaults to the empty string, selecting the most recently completed
recording. It is a basename within the fixed directory. `outputs` defaults to
`['din']` and accepts the target's enabled MIDI output names. All selected
outputs receive the same events. One file can play at a time.
An empty filename is a runtime selection and does not cause a host file upload.

`loops` defaults to 1; 0 repeats indefinitely. `pre_delay` and `post_delay` are
nonnegative integer seconds, applied before and after each pass. Unit strings
such as `'2 seconds'` are also accepted. The post-delay starts after the file's
end time and local delivery of its final events. `speed` is a positive multiplier
for the MIDI timeline; the delays use wall-clock seconds.

Reverse playback mirrors event times over the complete file duration and
exchanges Note On and Note Off, including zero-velocity Note On releases. Each
reversed release uses its matched original Note On velocity. Unmatched releases
have no onset to reverse and are skipped. Notes still held at the file end
receive a matching release there, so they also play when reversed.
Sustain, portamento, sostenuto, soft pedal, legato and hold-2 switches (CC 64–69)
restore their previous values at each mirrored event, independently per channel
and controller. Their initial value is assumed to be 0 (off). This preserves
repeated presses and intermediate pedal values. Controls still held at the file
end are activated at the beginning of reverse playback and released at their
mirrored original onset. Other control changes retain their original values.
SysEx messages retain their byte order,
including messages stored in several continuation events. Transposition applies
to note numbers and polyphonic pressure; notes outside 0–127 are skipped.

Playback occupies its invoking patch until all passes and delays finish. Normal
patch cancellation releases its outstanding notes, turns off CC 64–69 switches
held by the file, and terminates any open SysEx. Held notes and
pedals are also released at each pass end.
A momentary or latched switch provides cancellation for an indefinitely looping
On patch through its usual release or Off action, for example:

```toml
[switches.loop]
button = 'A'
mode = 'momentary'
on = {operation = 'play-midi-file', loops = 0}
off = {patches = []}
```

Each invocation of `record-midi-file` toggles recording. Channels default to all
16 and inputs default to all four available input types. Recording observes
input independently of MIDI forwarding. Channel messages obey the channel
filter; SysEx and system messages have no channel and obey the input filter.
System-common and real-time messages, including Clock, Start, Continue, Stop
and time code, are retained as SMF escape events. A file containing MIDI Clock
owns clock delivery on its selected outputs while active. The internal generator
and forwarded clock are suppressed there. Start the internal generator again
after playback if it should resume on those outputs.

Recordings use SMF type 0 at 1000 ticks per quarter note and a fixed 60 BPM tempo,
giving millisecond timestamps. Names are `{Starts}-{recording_count}.mid`.
`Starts` is the existing durable startup counter, and the recording count starts
at 1 each restart. The third recording during startup 255 is `255-3.mid`.
Existing files are opened with exclusive creation. A recording becomes the
latest file after successful finalization and synchronization. Startup scans
completed recordings to recover the latest filename; unfinished files are
excluded. Input or storage overload reports an error and leaves that recording
unfinished.

Playback prepares a temporary `.playback-index` in the same directory, with one
32-byte record per event. This supports seeking backward while keeping only
track heads, active notes and CC 64–69 values in RAM. Preparation and playback
advance in bounded polling steps. Record input is queued with timestamps and drained outside the
input-reading loop. Concurrent input streams are serialized around SysEx
messages; deferred channel events follow the complete message. An unfinished
SysEx is closed when recording stops. During a playback SysEx message, ordinary bridge traffic on
its selected outputs waits until the message ends; real-time messages can pass.

Native tests cover scheduling, tempo maps, reverse velocity, transposition,
multiple outputs, backpressure, loops, cancellation, SysEx, timer rollover,
recording filters and record/playback round trips. Physical SD latency and MIDI
wire timing require hardware qualification.
