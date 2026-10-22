# Patch switcher

The portable C++ implementation is under `include/cntrl/`. It contains no
Arduino headers or target pin numbers. `Hardware` accepts complete MIDI message
sequences and semantic panes; `Storage` provides two independent document slots.
The controller owns navigation, gestures, Undo and layout. A board adapter owns
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

cntrl upload patches.bin --port /dev/YOUR_SERIAL_PORT
cntrl status --port /dev/YOUR_SERIAL_PORT
```

Compilation never opens a device. Upload sends a show document to the running
application; it cannot flash firmware. Close other programs using the serial
port before uploading. A status response reports format version, active byte
count and CRC32. Recheck status after a power cycle to verify persistence.

Both TOML and JSON accept the same show, definition and table structures.
Program numbers use 1..128 and channels 1..16. Definitions associate each
instrument/setup pair with explicit bank mappings. Table entries identify
`instrument`, `setup`, `program` and optional `bank`. Tables join in command-line
order, filling missing metadata, combining tags in first-seen order, and
reporting conflicts. SysEx references are never read or included in the binary.

Regexes search within names. Zero matches always fails; multiple matches fails
unless `take_first_match = true`. The first match follows table-file and entry
order. An explicit bank filters the search. Name matches inherit their table
bank; numeric PCs with no bank send no bank selection. An explicit display name
takes precedence over table metadata; otherwise numeric patches use `PC N` or
`Bank B / PC N`.

Shorthand uses its first character as delimiter, with fields instrument,
program, volume, pan, bank. Empty and omitted trailing optional fields are
allowed. All-digit program fields are PC numbers; other text is a regex. Choose
another delimiter or expanded fields if a field contains the delimiter. Separate
fields can fill omissions, but duplicating a populated shorthand field is an
error. Every patch is checked and errors are reported together before any binary
is written. Display text is checked against target capabilities.

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

D/E wrap categories, retaining one cursor per category. A/B wrap patches within
the category. Browsing only changes NEXT. C sends the candidate, records an Undo
action and leaves NEXT unchanged. F undoes the latest action by restoring that
instrument's previous selection, leaving other instruments unchanged. If its
previous selection is unknown, Undo pops the action without sending anything.

UNDO previews the next restoration target. SENT shows the last selected or
restored patch and instrument. Undo without a restoration leaves SENT unchanged
while history remains; emptying history blanks UNDO and SENT. A new show and
startup both clear history and select the first candidate without sending MIDI.

A/B/D/E singles wait up to `chord_ms` (default 100). Early release fires a single
immediately. A second press at or before the deadline consumes both singles.
Recognized AB/AD/BE/DE pairs are reserved; all pairs currently do nothing. A pair
blocks further A/B/D/E actions until all four are released. C/F remain immediate
after electrical debounce. No hold, repeat or double-press actions are installed.

## Binary and transfer protocol, version 1

All integers are little-endian; strings are NUL-terminated, zero-padded ASCII.
Records have fixed widths; no C++ memory layouts or host pointers are serialized.

| Record | Layout |
| --- | --- |
| Header, 24 bytes | `CSW1`, u16 version, u16 chord milliseconds, u32 total bytes, u16 instrument/category/patch counts, u16 zero, u32 body CRC32 |
| Instrument, 68 bytes | alias[32], output[32], u8 channel 0..15, three zero bytes |
| Category, 68 bytes | name[64], u16 first patch, u16 count |
| Patch, 76 bytes | u16 instrument, u16 category, u8 PC, MSB, LSB, volume, pan, three zero bytes, name[64] |

Records occur in the order header, instruments, categories, patches. MIDI values
are 0..127; optional values use 255 for omission. Categories contain contiguous,
nonempty patch ranges covering every patch exactly once. Both compiler and
receiver validate bounded sizes; the receiver also validates strings, routes,
indices and message values before activating a document.

The USB serial request is `CNTRL BEGIN SIZE CRC32_HEX\n`. The receiver replies
`CNTRL SEND\n`, then accepts exactly SIZE binary bytes. After validation,
storage commit and read-back verification it replies `CNTRL DONE SIZE CRC\n`.
`CNTRL STATUS\n` replies `CNTRL READY 1 SIZE CRC\n`. Errors use
`CNTRL ERROR REASON\n`. Partial commands/transfers time out after ten seconds
without a byte. The host uses 115200 baud, not a bootloader-triggering baud rate.

Each storage slot has `CNST`, u32 generation, u32 length and u32 whole-document
CRC, followed by the document and a final `DONE` commit trailer. Only the
inactive slot is replaced. Boot chooses the newest valid committed slot.
Interrupted and short writes are covered by host tests; filesystem and physical
power-loss guarantees depend on the board's storage implementation and require
separate qualification.
