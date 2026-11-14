# Run the saved VL70 bank in the emulator

The importer handles MC6 Pro bank 0, `VL70-1`, and follows C into bank 1,
`VL70-2`. C there returns to bank 0. Both banks live in one cntrl setup,
retaining shared counters. [vl70-1.toml](vl70-1.toml) is the editable result,
checked against the original backup by a regression test.

From the repository root, with the non-editable project installation and
`emulator` extra installed, use an empty output directory:

```sh
cntrl import-morningstar cntrl/maker/morningstar/mc/Morningstar_MC6PRO_All_Banks_Backup_20261021_135556.json --bank 0 --output cntrl/maker/morningstar/mc/firmware/build/vl70-1
cntrl emulate cntrl/maker/morningstar/mc/firmware/build/vl70-1/setup.toml --controller mc6-pro --storage cntrl/maker/morningstar/mc/firmware/build/vl70-1/session --source-root . --scenario cntrl/maker/morningstar/mc/firmware/build/vl70-1/start.toml --interactive
```

Import creates TOML, a source-hash/scope report,
a startup scenario and ideal 0..1023 calibration for virtual expression inputs.
These calibration files describe sliders, not connected pedals. The startup
scenario activates bank 0 without sending MIDI. `--interactive` opens the window
after replay; omit it for headless replay with display and trace exports.
The emulator exports the virtual target and fonts and compiles the setup.

A/B/D/E/F run program actions; C changes banks. Expression inputs 0 and 1 send
CC 13 and CC 4 on channel 1. MIDI goes to emulated DIN and USB-device outputs.
Host MIDI devices stay disconnected unless explicitly selected. Import does not
access USB, upload or flash firmware. A non-empty output directory and unsupported
messages, toggles, message scrolling, extra preset pages, triggers, expression subtypes
or routing produce errors rather than silently losing behavior.

The saved cross-counter references are preserved: in `VL70-2`, Synth advances
counter 8 on channel 1 but sends counter 3 on channel 2. Other second-bank
switches similarly reference first-bank counters on channel 2.

## Decoding evidence and limits

PC-scroll type 33 uses its first data byte's upper nibble for the operation and
lower nibble for the counter. Operations 0/1/2 are send/increase-and-send/
decrease-and-send. Changes happen before the first send. The third data byte
controls wrap for advancing operations. Initial values and bounds come from
controller settings. cntrl authoring adds one to PC values, matching `%F` names.

- [Morningstar scroll semantics](https://helpdesk.morningstar.io/support/solutions/articles/43000681039-sending-incremental-decremental-pc-numbers-or-cc-values)
- [Morningstar placeholders and messages](https://manuals.morningstar.io/mc-midi-controller/Message-Type-List.181698784.html)
- [Pinned FootSmart decoder](https://github.com/jgateley/FootSmart/blob/c1245fe93f50625063e9782dc1ae9029b2f437b9/simple_message.py)
- [Official editor JavaScript inspected on 2026-11-14](https://editor.morningstar.io/main-BXTCALJX.js),
  SHA-256 `86720050df77dd678e94777d1a8beefced5746470accf4848db6a1dc25f0257b`:
  `setPortOutputValue` maps bit 0 to DIN and bit 1 to USB-device port 1.
  Omniport mode 1 is Expression; 12 is Disabled. Expression message type 1
  uses controller/minimum/maximum in its first three data fields. Expression
  action 0 means Expression, rather than No Action.

## Display conversion

The imported view puts D/E/F in the top bands and A/B/C in the bottom bands,
with short names centred above/below their switches when `lcdAlign` is enabled.
The saved setting hides switch-letter prefixes. The middle bands show the
last pressed preset's long name on the left, the bank name in the centre, and
the bank/page indication on the right. The indicator uses compact blue text
(`B001 PG1`) at the lower left of its band.
All three middle bands use the saved bank background and foreground colors;
the blue indicator overrides the foreground. Long-name counter tokens stay live.

Preset and bank foreground/background indices are translated using the official
editor's `AQ` palette. This first importer supports the colors used by these
two banks, including dim black (64); other dim/mesh colors are not implemented.
The editor's icon 46 (`\.`) becomes a right chevron. The size settings encode
18 down to 8, with 0 meaning 18. Switch labels use Atkinson Next SemiBold at
twice that size limit, fitted to the band using actual exported glyph bounds.
The middle text uses Arial at the saved size limit. These choices follow the
original-pedal photo supplied on 2026-11-14, using the existing bundled fonts.
Bank/page sizes Large/Medium/Small map to maximum sizes 12/10/8; Hidden omits
the indication but preserves its background. These font and band metrics
approximate the stock display; they are not a pixel-exact reproduction of its
typeface or fitting algorithm.

See the [official display and preset description](https://help.morningstar.io/en/article/mc6-pro-user-manual-psevjy/).
The native visual regression records all three displays at bank entry, after
the first program send, in the second bank and after returning to the first.

Native tests execute compiler output in the production C++ emulator, checking
ordered PC messages, first-send stepping, send-only on another channel, both
outputs, wrap, retained values on bank return, and expression sweeps to CC 127.
Editable export recompiles to identical bytes. Format 43 adds optional binding
presentation records; existing format-41/42 records upgrade without changes to
their body. Older supported upgrades remain available.

Global MIDI thru/events, clock settings, auxiliary modes and unrelated banks
are outside this import. This is limited
support for the decoded message families, not every MC6/MC8 backup. Stock-firmware
traces and physical qualification remain necessary for timing and behavioral
equivalence claims.
