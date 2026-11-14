# Run the saved banks in the emulator

The importer puts all 128 MC6 Pro banks into one named `Banked` patch,
inside an editable setup. The patch owns its bank collection, preset-page
definitions, initial bank and navigation chords. A Banked Page displays the
running patch; the ordinary patch switcher selects and sends it alongside
other patches.
The three populated bank slots and all empty slots retain their original order,
including duplicate names. Each bank contains its enabled preset pages, up to
four. Empty banks and preset pages are implicit: only configured slots appear
in the document, keyed by their original zero-based numbers. Definitions use
bank names; identical pages share a definition, including duplicate bank names.
For this backup the entire slot mapping is:

```toml
[patches.Banked.banks]
0 = {0 = 'VL70-1'}
1 = {0 = 'VL70-2'}
2 = {0 = 'VL70-1'}
```

`bank_count = 128` and `page_count = 4` retain the navigation bounds. Optional
`page_counts` overrides retain banks with different page limits. Unlisted pages
in a configured bank inherit its title, theme and expression assignments from
its first configured page, with empty switches. Unlisted banks have a blank
title and empty switches, using the shared theme. Common display settings live
on the `Banked` patch, with local overrides only where needed.
Counters belong to the setup
and retain their values across bank changes. [vl70-1.toml](vl70-1.toml) is the editable result,
checked against the original backup by a regression test.

From the repository root, with the non-editable project installation and
`emulator` extra installed, use an empty output directory:

```sh
cntrl import --target-dir cntrl/maker/morningstar/mc -- cntrl/maker/morningstar/mc/Morningstar_MC6PRO_All_Banks_Backup_20261021_135556.json --bank 0 --output cntrl/maker/morningstar/mc/firmware/build/vl70-1
cntrl emulate cntrl/maker/morningstar/mc/firmware/build/vl70-1/setup.toml --controller mc6-pro --storage cntrl/maker/morningstar/mc/firmware/build/vl70-1/session --source-root . --scenario cntrl/maker/morningstar/mc/firmware/build/vl70-1/start.toml --interactive
```

Import creates TOML, a source-hash/scope report,
a startup scenario and ideal 0..1023 calibration for virtual expression inputs.
These calibration files describe sliders, not connected pedals. The startup
scenario sends the `Banked` patch to open its Banked Page on bank 0,
without sending MIDI. `--bank` chooses the initial
zero-based bank slot. `--interactive` opens the window
after replay; omit it for headless replay with display and trace exports.
The emulator exports the virtual target and fonts and compiles the setup.

A/B/D/E/F run program actions in the populated banks; C follows the saved
bank-up/down action. B+C advances banks and A+B goes back, wrapping at the ends.
E+F advances preset pages; D+E goes back. D+F opens the cntrl menu, where other
configured pages can be selected. The Banked Page keeps its selected bank,
preset page, switch state and pending actions while another page is selected.
Sending `Banked` again returns to that instance. Loading a different setup
ends the instance. It starts when the patch is sent or its Banked Page is first
visited. Counters and port roles remain shared setup configuration.
Chord members are deferred for 30 ms to recognize the pair without sending
their individual actions. These chords are omitted when the source dual-lock
setting is enabled.

Expression inputs 0 and 1 send
CC 13 and CC 4 on channel 1. MIDI goes to emulated DIN and USB-device outputs.
Host MIDI devices stay disconnected unless explicitly selected. Import does not
access USB, upload or flash firmware. A non-empty output directory and unsupported
messages, toggles, message scrolling, triggers, expression subtypes
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
saved banks, including dim black (64); other dim/mesh colors are not implemented.
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
Editable export recompiles to identical bytes. Format 47 stores each banked
patch in a type-17 graph node containing its bank/preset-page references,
local definition names and navigation chords. The compiler fills implicit slots
with shared empty definitions; only authored definitions have local names. The
node flags retain the default page count for editable export. CBPG maps Banked Pages to patch
nodes. Each banked patch needs one view, for example:

```toml
[pages.banked]
type = 'banked'
patch = 'Banked'
```

Each instance uses the existing switch executor for independent selection and
background execution. Format-46 setups upgrade without changes to their body. Re-import
the original backup to replace a legacy format-44 Banked Page. Older supported
upgrades remain available.

The capacity test requires the entire imported setup to remain
resident and measures the native checked heap while exercising bank changes,
chords, the menu and expression inputs, with no allocation failures against
the target profile's 213,184-byte allowance
after its runtime reserve and safety margin. Native object sizes differ from
the MCU; the embedded build separately checks Cortex-M7 object size bounds.
The embedded build also enforces the ITCM reserve. These are build/emulator checks;
physical timing and stack high-water use still need hardware qualification.

Global MIDI thru/events, clock settings, auxiliary modes and active custom bank arrangements
are outside this import. This is limited
support for the decoded message families, not every MC6/MC8 backup. Stock-firmware
traces and physical qualification remain necessary for timing and behavioral
equivalence claims.
