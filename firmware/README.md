# Patch switcher

The portable C++ implementation is under `include/cntrl/`. It contains no
Arduino headers or target pin numbers. `Hardware` accepts complete MIDI message
sequences and semantic panes; `SetupStorage` provides named pairs of document
slots and an application-owned persistent selection record.
`PatchExecutor` owns executable dispatch, ordered composition, pending delivery
and source pinning. Switch timing, browser activation and songs all use it.
The controller owns navigation, gestures, known program Undo and layout. A board adapter owns
scanning, electrical debounce, pixels, output queues and storage access.

Run host tests, including address and undefined-behavior sanitizers:

```bash
make -C firmware test
uv run --frozen pytest
```

The Python transport tests compile `test/document_server.cpp` and connect the
Python sender to the actual C++ receiver through a pseudo-terminal. They never
access a physical serial device. `test/show.golden` is the shared binary fixture,
stored as hexadecimal text; Python and C++ both verify it.

## Host command

Install the Python distribution with `uv tool install cntrl-VERSION.whl`, or use
`uv run cntrl` in this checkout. Python 3.13 or later is required. Serial upload
currently supports POSIX systems, including macOS and Linux.

```bash
cntrl compile examples/patch-switcher/patches.toml \
  --definitions examples/patch-switcher/definitions.toml \
  --table examples/patch-switcher/table.toml \
  --table examples/patch-switcher/tags.toml \
  --target TARGET.json --output patches.bin

cntrl setup copy Evening=examples/patch-switcher/patches.toml \
  --definitions examples/patch-switcher/definitions.toml \
  --table examples/patch-switcher/table.toml \
  --target TARGET.json --port /dev/YOUR_SERIAL_PORT
cntrl setup list --port /dev/YOUR_SERIAL_PORT
cntrl setup set Evening --port /dev/YOUR_SERIAL_PORT
cntrl setup rotate -b --port /dev/YOUR_SERIAL_PORT
cntrl setup download Evening --port /dev/YOUR_SERIAL_PORT
cntrl setup del Evening --port /dev/YOUR_SERIAL_PORT
cntrl status --port /dev/YOUR_SERIAL_PORT
```

Compilation never opens a device. `setup copy` compiles and copies data to the
running application; it cannot flash firmware. The old single-show `upload`
command is replaced by named setup management. Close other serial users before
transferring data. A status response reports format version, active byte
count and CRC32. Recheck status after a power cycle to verify persistence.

Data files can be combined using a standalone ` + ` argument wherever the CLI
accepts them. Put positional expressions before options, for example
`cntrl setup copy Evening=jazz.toml + classical.toml ...`. Separate expressions
create separate setups. Default names join file stems with `+`. `NAME=` names
the whole expression and is allowed only before its first operand. Escape an
equals sign within a name as `==` or `\=`; quote backslashes for the shell.
The host setting `avoid_confict` defaults to true: recursively merge dictionaries,
concatenate lists and let later scalars overwrite earlier values. With
`--no-avoid-confict`, differing lists/scalars are errors; equal values are retained
once. Incompatible types are errors in both modes. Diagnostics accumulate before
any uploads. `--table` remains repeatable and each occurrence accepts expressions.

Names are 1..63 filename-safe printable ASCII characters, ordered and matched
case-insensitively, with display capitalization retained. Case-only duplicates
are rejected. Copy and download refuse collisions unless `--overwrite/-o` is
given. Downloads default to the current directory and produce `NAME.toml`.
Each export contains resolved patch names, instrument identities and bank
definitions, so it recompiles without the original definitions or tables.
It does not reconstruct source regexes, comments, paths or original formatting.

`setup set` without a name only prints the persistent selection. `rotate` wraps
alphabetically, with `--backward/-b` reversing direction; fewer than two valid
setups is an error. Selection never sends MIDI. Delete confirms once for the
whole batch unless `--yes/-y` is supplied. Deleting the active setup selects the
earliest valid survivor outside the batch. Deleting the final setup is legal:
all three screens say `No setups`, normal controls are silent and management
remains available. Invalid catalogue entries are reported rather than silently
treated as an empty card. No automatic migration of old single-show files occurs.

Listings show commit dates as local `YYYY-MM-DD HH:MM:SS`, or `unknown`. Copy
syncs the pedal's clock from the Mac. Without a persistent clock, on-pedal edits
after an offline reboot have unknown dates. Each copy is individually durable;
multi-setup batches can partially succeed and report those results.

Both TOML and JSON accept the same show, definition and table structures.
User-adjustable controller settings belong in the show configuration whenever
possible, rather than firmware build options.
Set top-level `brightness = 192` in a TOML show, or `"brightness": 192` in JSON.
It defaults to 192 and accepts integers from 0 (off) to 255 (maximum PWM).
The controller applies it when the show is uploaded and whenever that show loads
at startup. Changing it requires a show upload, not a firmware rebuild.

Program numbers use 1..128 and channels 1..16. Definitions associate each
instrument/setup pair with explicit bank mappings. Table entries identify
`instrument`, `setup`, `program` and optional `bank`. When a table record omits
`instrument`, loading the file supplies its filename stem. Explicit values are
preserved. The current catalogues, `instruments/vl70m.toml` and
`instruments/patchman.toml`, instead identify instruments by table key and programs
by bank key and array position. Tables join in command-line
order, filling missing metadata, combining tags in first-seen order, and
reporting conflicts. SysEx references are never read or included in the binary.
Personal program tags are separate from these documented catalogues: also supply
`--table instruments/tags.toml` to include those classifications.

Regexes search within original synthesizer `name` values, never display labels.
Zero matches always fails; multiple matches fails
unless `take_first_match = true`. The first match follows table-file and entry
order. An explicit bank filters the search. Name matches inherit their table
bank; numeric PCs with no bank send no bank selection.

`name` is the original synthesizer patch name. It can be supplied directly on a
numeric show patch when no library name is available, but cannot replace a
different known library name. To change only the displayed text, use optional
`display_name` on the program description (table entry) or on the show patch.
Display precedence is show patch `display_name`, program `display_name`, then
original `name`. If none is supplied, numeric patches use `PC N` or
`Bank B / PC N`. Display labels never change the MIDI address or regex lookup.

For example, a table entry can have `name = 'Trumpet 2'` and
`display_name = 'Muted Trumpet'`; a show patch selecting that program can use
`display_name = 'Opening solo'`. All three values are retained separately.
Resolved downloads embed the used program descriptions in
`definitions.programs`, alongside `definitions.setups`, and retain each show
patch's own `display_name`. They recompile without the original library files.
Font sizing and precomputed offsets use the final display label only; a hidden
original name is preserved as UTF-8 metadata without display-character limits.

Executable values require an explicit `type`. Named definitions use `patches`;
ordered browser entries use `patch_cycles`. Delimiter shorthand, implicit string
references and bare-list sequences are rejected. See
[the Patch schema and format 20 authoring changes](../doc/patches.md) for MIDI,
state, composition, switches and stable references. Independent authoring errors
are reported together before any binary is written. Display text is checked
against target capabilities.

Volume is CC7, 0..127. Pan is CC10, -64..63 plus 64 on the wire. A send emits
bank MSB, bank LSB, PC, volume and pan in that order, omitting unspecified values.
Acceptance means the local output queue accepted the complete sequence, not
that the receiving instrument acknowledged it.

## On the controller

Buttons are logically arranged as:

```text
D E F
A B C
```

D/E wrap patch cycles, retaining one cursor per patch cycle. A/B wrap patches within
the patch cycle. Browsing only changes NEXT. C executes the candidate and leaves NEXT
unchanged. A simple program selection records a known program Undo entry.
Composites and side effects clear unsafe program history rather than invent an
inverse. F restores the latest known program selection by restoring that
instrument's previous selection, leaving other instruments unchanged. If its
previous selection is unknown, Undo pops the action without sending anything.

On the performance page, a successful C press that changes the instrument's
last-sent program or bank sets all screens to maximum brightness until C is
released. Release restores the configured brightness immediately, with no minimum
flash duration. The first send counts as a change because previous state is
unknown. Repeated program/bank selections and failed sends do not flash. Leaving
the performance page or changing the active setup also ends the brightness boost.

UNDO previews the next restoration target. SENT shows the last selected or
restored patch. Screens contain only patch cycle and patch, not titles or instrument
labels. Undo without a restoration leaves SENT unchanged
while history remains; emptying history blanks UNDO and SENT. A new show and
startup both clear history and select the first candidate without sending MIDI.

A/B/D/E singles wait up to `chord_ms` (default 100). Early release fires a single
immediately. A second press at or before the deadline consumes both singles.
Recognized AB/BE/DE pairs are reserved. A+D opens the module menu; choose Setups for setup management or MIDI cable test. A pair
blocks further A/B/D/E actions until all four are released. C/F remain immediate
after electrical debounce. No hold, repeat or double-press actions are installed.

In management, A/B browse, C confirms and F goes back. Choose a setup, then
Select or Edit. The numeric editor chooses a patch, a field and its value.
Program, bank, volume and pan belong to that patch; channel belongs to its
instrument alias; brightness and idle timeout belong to the whole setup. Bank
cycles through that instrument's declared mappings and None. Volume and pan
can also be None. F from patch selection offers Save/Discard/Cancel. No edit
auditions or sends MIDI; only Save updates storage. USB operations that could
displace unsaved edits return `unsaved-edits`. Text/font/routing edits are not
included. Management uses built-in readable typography even without a setup.
Management uses a 16-pixel classic font, twice the performance-independent
editor's original size. Labels and values occupy separate screens; long names
wrap at 12 characters per line within the padding, without truncation.

A+D used to wake the pedal does not open management. Release the complete wake
gesture, then press A+D again. During screensaver entry, buttons consume the
complete wake gesture and cancel the fade without a musical action, except C
on the performance page: C cancels the idle fade and sends normally. Successful
program-change feedback bypasses fading and applies at the button edges, including
a quick press/release. Startup-animation dismissal still consumes its gesture.

## Pages, windows and line styles

The default `patch` page reproduces the patch cycle and patch-name display. Undo,
sent and next supply their own patch to that page. The same generic text-line
renderer also draws management, empty-state and error messages.

```toml
contrast = 0.6
pad = 4

[style]
font_name = 'Arial'

[pages.patch]
fields = ['patch_cycle.name', 'display_name']

[windows.undo]
page = 'patch'

[windows.sent]
page = 'patch'

[windows.next]
page = 'patch'

# Optional overrides:
# [windows.next.style]
# background_color = '0x002040'
# [pages.patch.line_styles.display_name]
# font_name = 'ComicSansMS'
```

Field names share one namespace. Built-in patch attributes are `patch_cycle.name`, `tags`,
`display_name`, `name`, `program`, `bank`, `volume`, `pan` and `instrument` (the
routing alias). Built-in global settings are `brightness`, `splash`,
`usb_midi_interface`, `screensaver_timeout`, `chord_ms` and `transition_ms`.
These read the current document, including numeric edits. Additional shared
display values are defined as `[parameters.tempo]` with `value = 120` and an
optional `style` block; they are configuration values, not a tempo engine.
Their names must not collide with built-ins. Pages list names in display order;
unknown or duplicate names are rejected. Absent/empty values are omitted, while
zero and false remain visible. A page supports at most 16 fields, further
limited by the available display height.

Each partial style supports `foreground_color`, `background_color`,
`font_name` and `icon`. Resolve properties independently: show style, window, page,
ordered tags, program, patch, shared parameter (where applicable), then
page field overrides. Omitted properties retain the earlier value.

Apply program tags in listed order, followed by patch tags in listed
order; later explicit properties win. A locally repeated tag applies again.
The `tags` field displays the unique first-occurrence membership list and
uses tag styles without program/patch overrides. Navigation headings and
global fields do not acquire patch styles. There are no tag priorities.
See [formatting](../plan/formatting.md) for the complete contract.

For example, the program can declare `tags = ['sax']`, while a patch that
sets up the whole board adds `tags = ['whole-system']`. That patch displays
`sax, whole-system`; another use of the same program displays `sax`. Tags
classify objects and supply styles; they do not cause MIDI or mixer actions.
Downloads preserve only each object's explicit assignments, with shared tag
definitions. Program tags joined from multiple table files form a union.

Configuration fragment:

```toml
[tags.sax.style]
foreground_color = 'yellow'

[tags.whole-system]
style = {font_name = 'CourierPrimeBold'}

[[definitions.programs]]
instrument = 'synth'
setup = 'default'
program = 1
name = 'Sax'
tags = ['sax']

[[patch_cycles.'Song 1']]
type = 'program-change'
instrument = 'synth'
program = 1
tags = ['whole-system']

[pages.patch]
fields = ['patch_cycle.name', 'tags', 'display_name']
```

The default font is `classic`, default padding is 4, and default colors are
black on white. Set the show font with `[style].font_name`; each window may
override `pad`. Each line independently uses the largest available bitmap face
that fits its width and equal share of the padded height, preserving proportions.
There are no authored sizing controls. Performance pages reject initial overflow;
management retains wrapping/clipping. Numeric edits wider than their compiled
face are clipped. Styles support optional `icon` image-file references; omission inherits and an
empty string suppresses an inherited icon. Image rendering is deferred.

The compiler measures each line with its own font and stores centered offsets.
The pedal recalculates horizontal centering for mutable numeric/global fields.
Vertical spacing uses the current nonempty rows. Each line's background fills
its horizontal band inside the padding; band boundaries split the gaps halfway.

The whole screen is filled with its background, including empty history panes.
Text is clipped to the rectangle inset by `pad` on every side. Runtime errors
retain the center screen's colors/padding and use its left inset.

The rightmost patch name alone is stippled when its instrument, program and
bank-select address match the most recent successful send for that instrument.
The tag and the other two screens keep their normal foreground color.
Names/tags are not part of this comparison; the same addressed patch can
appear under different names. Sending to another instrument does not clear it.
Undo updates it only when a restoration is actually sent. A failed send does
not change it, and starting a new show clears known selections. Pressing C on a
greyed-out patch does nothing: it sends no MIDI, adds no history entry, and
triggers neither a transition nor brightness feedback.

The stipple preserves the resolved foreground color and exposes the
line's background at pixels where `(x + y) & 1` is nonzero. Masking follows
composition of all glyphs, so overlapping letters share a consistent pattern.
Tag headings, other windows and padding keep their normal appearance.

Colors accept `0xRRGGBB` or case-insensitive names: black, white, red, green,
lime, blue, yellow, cyan/aqua, magenta/fuchsia, silver, gray/grey, maroon, olive,
purple, teal, navy and orange. Green is 0x008000; lime is 0x00FF00.
Distance is Euclidean RGB distance divided by `255 * sqrt(3)`, so black/white
distance is 1. This is not perceptual luminance contrast. A distance <= `contrast`
warns without rejecting the show; `contrast` defaults to 0.6 and accepts 0..1.
When only one color is supplied, the other starts at its RGB complement. The
compiler chooses the smallest uniform integer brightness shift (with channel
clamping) reaching the threshold. If no such shift works, it uses the furthest
RGB-cube corner, warning if that distance is still <= the threshold. Contrast is
computed in input 8-bit RGB; encoded display colors are rounded down to RGB565.

Text lines share the unused height inside the padding evenly: for N nonempty
lines, H is the remaining height divided by N. The top and bottom gaps are H/2,
and each gap between lines is H. Wrapped rows count separately, and each line
uses its own font height. Positions are rounded to whole pixels. If the text
exceeds the available height, extra gaps are zero and clipping still applies.
The renderer computes these positions from the complete pane; compiled row-y
values do not determine vertical spacing.

## Binary and transfer protocol, version 14

Version 14 stores resolved pages and line styles with foreground and background
colors. Each display record ends with four reserved zero bytes. Authored
formatting metadata, including icon references, is retained for editable exports. The
default-off `usb_midi_interface` Boolean remains a
performance-setup setting, including in resolved exports and the pedal editor.
`screensaver_timeout` remains an integer
idle timeout in seconds.
Configuration quantities use `reccy.configuration.units`: `chord_ms` and
`transition_ms` accept durations such as `"100 ms"` and `"0.5 s"`, while bare
numbers remain milliseconds. `screensaver_timeout` accepts whole seconds such
as `"2 min"` or `"1:30"`; bare numbers remain seconds. Values must convert
exactly to the field's integer unit and satisfy its existing bounds.
padding, target display dimensions and font metrics accept pixels (`"24 px"`);
the target document-size limit accepts bytes (`"64 KiB"`). Contrast accepts
dimensionless ratios or percentages (`"60 percent"`). Resolved exports contain
normalized numbers. An unspecified transition duration remains unspecified.

`transition_ms` optionally controls send/undo slide duration in milliseconds,
with an allowed range of 1..65535. An omitted or null setting stays unspecified
through compilation and firmware decoding. Only the renderer supplies the
500 ms default when starting a transition. Resolved TOML omits unspecified
durations and retains explicit values, including 1000. The target frame cadence remains 60 Hz;
duration is a visual target, not a MIDI delay or a hard real-time guarantee.
Zero (the default) disables the screensaver. Positive values enable the installed
animation after no physical input for that interval, independently of startup
`splash`. The asset is transferred separately using the target's upload tooling. Older
documents are rejected; recompile the source show and upload it after installing
version 14 firmware. Replace old `screens.NAME` color tables with
`windows.NAME.style` tables before recompiling. No automatic
conversion of stored documents is performed.

Startup `splash` defaults to true: an omitted setting uses the currently installed
animation. Explicit `splash = false` disables it. A boot without a selected setup
also uses the installed animation. If no valid animation is installed, the normal
UI appears immediately. Returning to the empty state through deletion or closing
management does not replay the startup animation. Existing compiled documents
retain their encoded true/false setting until recompiled.

All integers are little-endian; fixed-width strings are NUL-terminated,
zero-padded ASCII. Name metadata uses length-prefixed UTF-8 instead.
Records have fixed widths; no C++ memory layouts or host pointers are serialized.

| Record | Layout |
| --- | --- |
| Header, 28 bytes | `CSW1`, u16 version (14), u16 chord milliseconds, u32 total bytes, u16 instrument/patch cycle/patch counts, u8 brightness (0..255), u8 flags (bit 0 splash, bit 1 USB MIDI interface; other bits zero), u32 body CRC32, u32 screensaver timeout in milliseconds |
| Screen, 16 bytes, three records | u8 font ID, size, pad, zero; u16 foreground RGB565, background RGB565, line height, top-bearing adjustment; four zero bytes |
| Instrument, 68 bytes | alias[32], output[32], u8 channel 0..15, three zero bytes |
| Navigation patch cycle, 68 bytes | name[64], u16 first patch, u16 count |
| Patch, 82 bytes | u16 instrument, u16 navigation patch cycle, u8 PC, MSB, LSB, volume, pan, three zero bytes, effective display label[64], three u16 resolved page indices (undo/sent/next) |
| Metadata header, 8 bytes | `CMTA`, u16 bank mapping count, u16 transition milliseconds (0 means unspecified) |
| Identity, 64 bytes per instrument | instrument identifier[32], instrument-level setup identifier[32] |
| Bank mapping, 8 bytes | u16 instrument index, u32 logical bank, u8 MSB, LSB (255 for omission) |
| Logical bank, 4 bytes per patch | u32 logical bank, `0xffffffff` for no bank |
| Name metadata | `CNAM`, then original `name`, program `display_name`, show `display_name` for each patch, in order; each value is u16 UTF-8 byte count followed by bytes, or `0xffff` for absent |
| Display header, 16 bytes | `CDSP`, u16 style count, page count, text count, page-data byte count; u32 source-metadata byte count |
| Resolved style, 16 bytes | Same layout as a screen record, one per distinct resolved style |
| Resolved page | u8 line count followed by 8-byte line records: u8 field code, u8 already-sent indication flag, u16 style index, centered cursor x, literal-text index |
| Literal text | u16 byte count including the final NUL, followed by ASCII text; at most 64 bytes |
| Display source metadata | UTF-8 TOML preserving pages, windows, partial styles, shared tag definitions, explicit program/patch tags, global parameter values, show style, padding and icon references within partial styles |

The existing firmware tag counters and target `max_patch_cycles` limit refer
to navigation patch cycles. Classification tags are resolved by the host and
stored in authored display metadata; they do not change navigation records.

Records occur in the order header, screens (undo/sent/next), instruments,
tags, patches, metadata header, identities, bank mappings, logical banks
and name metadata, followed by the display header, styles, pages, literal texts
and display source metadata. Name metadata has no terminators or trailing padding; empty
and absent values are distinct. Its variable length avoids allocating three
fixed-width name slots per patch. The firmware validates its bounds and retains
it unchanged; rendering continues to read the effective label from the patch.
MIDI values
are 0..127; optional values use 255 for omission. Tags contain contiguous,
nonempty patch ranges covering every patch exactly once. Both compiler and
receiver validate bounded sizes; the receiver also validates strings, routes,
indices and message values before activating a document.

`CNTRL STATUS\n` replies `CNTRL READY 20 SIZE CRC\n`. Errors use
`CNTRL ERROR REASON\n`. Partial commands/transfers time out after ten seconds
without a byte. The host uses 115200 baud, not a bootloader-triggering baud rate.
Names in commands are hex-encoded ASCII, never paths. Requests are:

- `CNTRL TIME EPOCH_SECONDS`: synchronize commit timestamps, reply `CNTRL OK`.
- `CNTRL SETUP CURRENT`: reply `CNTRL CURRENT NAME_HEX` (empty name allowed).
- `CNTRL SETUP LIST`: first `CNTRL ENTRY NAME_HEX EPOCH SIZE CRC`, or
  `CNTRL INVALID NAME_HEX`; subsequent `CNTRL NEXT` requests advance until
  `CNTRL END`. Invalid records have no valid committed generation; tombstones
  are deliberately omitted.
- `CNTRL SETUP PUT NAME_HEX SIZE CRC OVERWRITE`: reply `CNTRL SEND`, receive
  exactly SIZE binary bytes, validate, commit and verify, then `CNTRL OK`.
  `OVERWRITE` is 0 or 1. The host reads back all bytes for verification.
- `CNTRL SETUP FINISH`: select the earliest setup if selection is empty after
  a copy batch; otherwise retain the selection, reply `CNTRL OK`.
- `CNTRL SETUP GET NAME_HEX`: reply `CNTRL FILE SIZE CRC`. Each `CNTRL NEXT`
  returns `CNTRL DATA HEX_BYTES` with up to 128 bytes, until SIZE is complete.
- `CNTRL SETUP SET NAME_HEX`, `CNTRL SETUP FORWARD` and
  `CNTRL SETUP BACKWARD`: persist selection and restart the GUI, reply `CNTRL OK`.
- `CNTRL SETUP DELETE SIZE CRC`: receive a checksummed binary list of
  NUL-terminated names after `CNTRL SEND`; validate the whole list, safely change
  selection and commit deletions; reply `CNTRL OK` or report failure. Query the
  catalogue after a failure to determine which individual deletions completed.

Each setup has `/cntrl/setups/NAME/0.bin` and `1.bin`. Its 96-byte header contains
`CNST`, u32 generation, length, payload CRC, epoch timestamp, name[64], eight zero
bytes and a CRC of the preceding 92 header bytes. The document follows, then
`DONE`. A zero-length committed generation is a deletion tombstone: it suppresses
older content even after restart. Previous payload generations are retained
until the name is reused; deletion is logical, not SD garbage collection.
Selection uses two 84-byte records under `/cntrl/selection/`: `CSEL`, generation,
name[64], four zero bytes, CRC of the first 76 bytes, then `DONE`. An explicit
empty selection prevents interrupted final deletion from reactivating a setup
that was still awaiting deletion. Corrupt selection records are errors, not
equivalent to an unset selection. All metadata and payloads are checked on read.
Only the inactive slot is replaced. Boot chooses the newest valid committed slot.
Interrupted and short writes are covered by host tests; filesystem and physical
power-loss guarantees depend on the board's storage implementation and require
separate qualification.
