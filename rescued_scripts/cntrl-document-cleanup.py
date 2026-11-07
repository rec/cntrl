from pathlib import Path

# Retain the implemented authoring contract as documentation, rather than a plan.
p = Path('plan/formatting.md')
s = p.read_text()
s = s[s.index('## Goal'):s.index('## Implementation and verification')]
s = s.replace('## Goal\n\nKeep formatting in the existing TOML configuration. Simplify the current style\nmodel and make its precedence explicit. Reuse object-attached partial styles,\npage fields, tag membership and host-side compilation.', '## Scope\n\nFormatting uses TOML configuration, object-attached partial styles, page fields,\ntag membership and host-side compilation.')
s = s.replace('Use show-level `style` for authored appearance defaults. Remove duplicate\nshow-level font controls as part of converting configurations, so each setting\nhas one authoring path.', 'Use show-level `style` for authored appearance defaults. Font names belong in\nstyles; there is no separate show-level font setting.')
s = s.replace('Registry declaration order has no effect. Remove integer\n`priority` and the errors for conflicting values at equal priority.', 'Registry declaration order has no effect. Tags have no integer `priority`.')
a = s.index('Remove authored `font_size`')
b = s.index('Already-sent text', a)
s = s[:a] + '''There are no authored `font_size` or `autoscale_fonts` controls. Selected raster
sizes and metrics are compiler details. The patch-manager card divides the window
into three equal vertical bands. Its first band is reversed, using the heading
color as background and the page background as text color. The second and third
lines share the smaller fitting size, while retaining their font families.
Other pages, including switch pages, retain their own layout rules.

Window padding, horizontal centering, field order and missing-value omission
remain part of the page model. Transitions end with the destination appearance.

''' + s[b:]
s += '''## Pages and fields

Pages contain ordered field names, for example
`fields = ['patch_cycle.name', 'names.vl70m', 'names.patchman']`. Each window supplies
its own data item to the page. Missing and empty values are omitted; zero and
false remain visible. Built-ins and shared parameters use one unique namespace.
Unknown or duplicate field names are errors. Pages are bounded by record capacity
and target height. See the [application guide](../firmware/README.md) for the
complete field catalogue and compiled format, and [Patch authoring](patches.md)
for styles generated automatically for cycles.
'''
Path('doc/formatting.md').write_text('# Display formatting in TOML\n\n' + s)

Path('doc/midi-interface.md').write_text('''# USB MIDI interface module

Open MIDI interface from the module menu. Each visit starts Off. B toggles
forwarding; F returns to the module menu and restores the setup's active bridge
preference. Toggling the tool does not save or modify that preference. The module
uses the existing bridge, parsers, queues and output scheduling.

The center display shows the title, host connection, four activity indicators
and On/Off above B. The outer displays are black except for the right-hand Back
label near F. Backlighting stays unchanged. While Off, activity is cleared and
the host-status value is hidden. While On, Host connected means the USB device
has been configured by the computer. It does not mean a DAW opened a MIDI port.

DIN in and TRS in indicate complete received MIDI packets, including real-time
and streamed SysEx, independently of onward queue acceptance. DIN out and TRS out
indicate successful physical transmitter submissions, rather than USB reception
or queue insertion. A lit output does not establish cable continuity or receipt
by an instrument. Each indicator remains lit for 100 ms after its latest event.
Off, exit and disconnection clear activity; reconnection does not replay it.

On enables the existing physical-to-USB and USB-to-physical routes without USB
re-enumeration. It generates no test messages and adds no physical MIDI thru or
USB-host routing. With no computer connected it remains responsive, reports No
host, and does not retain physical input for later replay. Off clears bridge
queues and parser state, preserving accepted pedal work and required SysEx
termination. The target coordinates exclusive tool ownership.

`firmware/include/cntrl/midi_interface.h` owns the page, switch and activity state.
`MidiInterfaceControl` supplies temporary bridge control and restores the setup
preference on exit. The target supplies host and activity events and optional
build inclusion. There is no dependency on the cable-test module or second
implementation of MIDI forwarding.

Native tests cover lifecycle, setting restoration, host state, independent
activity, expiry and timer rollover, stale-state clearing and disconnected-input
parsing. Target-specific qualification, deployment and remaining physical checks
are recorded in the [hardware investigation](../cntrl/maker/morningstar/mc/usb-midi-interface-plan.md)
and [target guide](../cntrl/maker/morningstar/mc/firmware/patch-switcher.md).
''')

# Keep only the part of the song plan that is still future work.
Path('plan/songs-and-setlists.md').write_text('''# Remaining song and setlist work

Songs, ordered Patch positions, live references, on-pedal creation and setlist
editing are implemented. Their current behavior and storage limits are in the
[usage guide](../cntrl/maker/morningstar/mc/songs-and-setlists.md) and
[Patch authoring](../doc/patches.md).

## Optional named sections

Named sections such as intro, verse, chorus and solo remain deferred. Add them
only when a performer chooses to use them. A simple song must remain usable
without section fields, a section editor or an artificial main section. Reuse
existing song pickers and setlist editing rather than creating parallel flows.

Different section orders for separate occurrences of one song are also deferred.
Define their identity, persistence and advancement semantics before implementing
them. Repeated occurrences must stay independent, and merely browsing or editing
must never recall a sound.

## Physical qualification still required

Verify on the pedal that a performer can create a song without typing, build a
five-song setlist in one picker visit, distinguish duplicate names and navigate a
large library. Exercise Add next ordering, removal and movement of the current
occurrence, edit Undo, End of set, restart and restart-after-save behavior.

Capture MIDI for ordered Patch positions and a stock-firmware visit between two
MIDI positions. Check nondefault return buttons, power cycling in stock, held
return buttons, a final stock position and changed or unavailable source data.
Automated continuation tests and a successful flash do not qualify these physical
flows. Measure service latency under sustained MIDI and storage load.
''')

p=Path('doc/patches.md'); s=p.read_text().replace('The top line has a reversed full-width heading band, from the top edge to two\npixels below its text bounds.', 'The patch-manager window is divided into three equal vertical bands. The top\nband is reversed across its full width.').replace('Setup format 21 and song records CSG4 replace the preceding formats.', 'The current setup format is 25; song records use CSG4.').replace("instrument = 'piano'\nsetup = 'default'\noutput = 'din'", "output = 'din'")
s=s.replace('as one ordered patch cycle\non its output', 'as one ordered message sequence\non its output')
s=s.replace('tag styles. The current', 'tag styles. The current')
p.write_text(s)
p=Path('doc/momentary-latched.md'); s=p.read_text().replace('This configuration uses setup format 21\nand the tagged authoring schema.', 'This configuration uses the current compiled setup format\nand the tagged authoring schema.'); p.write_text(s)
p=Path('doc/setup-revision-history.md'); s=p.read_text().replace('The cooperative implementation has been flashed; post-boot\nverification and physical storage timing qualification remain pending.', 'The cooperative implementation has been flashed and subsequent boots have loaded\nsaved setups successfully. Dedicated physical history-workflow and storage timing\nqualification remain pending.'); p.write_text(s)
p=Path('doc/feature-modules.md'); s=p.read_text().replace('The MIDI cable tester is the first module. Its target adapter grants port access\nand delivers physical input, while the module owns test behavior and its page.\nBuild the MIDI monitor against the shared core services next. Use those two\nmodules to establish which interfaces are actually shared before formalizing\nmore of the framework.', 'The cable tester, MIDI interface and Panic use target adapters for physical\nservices while owning their own behavior and pages. A future MIDI monitor should\nreuse those services. Establish additional shared interfaces only when another\nmodule actually needs them.').replace('The first implementation is documented in [midi-cable-test.md](midi-cable-test.md).', 'Current modules are documented in [midi-cable-test.md](midi-cable-test.md),\n[midi-interface.md](midi-interface.md) and [panic.md](panic.md).'); p.write_text(s)

# Move board-specific upload details into the already-existing board guide.
p=Path('cntrl/maker/morningstar/mc/firmware/upload-helper.md'); s=p.read_text().replace('The patch-switcher application has repeatedly booted and communicated over USB,\nincluding software-triggered uploads of the current format 9 firmware. See the\n[deployment record](patch-switcher.md#current-deployment-2026-10-25).', 'The patch-switcher application has repeatedly booted and communicated over USB\nafter uploads through this helper. Firmware and compiled setup data are separate;\nverify both the application boot and the selected setup after an upload.')
s=s.replace('and heartbeat separately af', 'and heartbeat separately af')
s += '''
## Verify after upload

An I/O write error is a failed upload attempt. Recheck the expected HalfKay
identity and the unchanged image digest before a deliberate retry. Let every
attempt finish; the helper never retries automatically. A later reset followed
by board disappearance can return status 1 even when flashing succeeded.
Check that application USB returns, query `CNTRL STATUS`, and verify the saved
selection and setup bytes. USB enumeration alone does not verify setup data or
physical MIDI behavior. Recompile and restore valid saved setups if their compiled
format changed, preserving backups and already-invalid documents.
'''
p.write_text(s)
Path('doc/upload_helper.md').write_text('''# Firmware upload helper

Use the running, user-launched upload helper for firmware flashing. Reuse that
process; do not start a second server. It accepts an upload request for a fixed
build image with an exact SHA-256, validates and snapshots the image, then calls
the installed updater. It does not expose arbitrary commands or image paths.

Build and inspect the image first, close serial captures, request bootloader
entry, verify device identity, then upload with the digest. Let the upload finish.
Investigate write failures before retrying, and verify application status and
saved setup data after USB returns. Firmware flashing and setup transfers are
separate operations; the helper does not grant serial-transfer permissions.

The [target-specific helper guide](../cntrl/maker/morningstar/mc/firmware/upload-helper.md)
contains the implementation link, exact commands, device identity, image path,
validation rules, startup and recovery instructions.
''')

p=Path('plan/features.md'); s=p.read_text().replace('Recall a collection of instrument patches and controller values with one press.\nA scene could select a synth sound, change an effects preset and set a mixer\nlevel together. Preview the whole scene before sending, and make undo restore\nthe previous scene\'s known selections.', 'Composite Patches already execute several instrument and controller changes\nwith one press. A future scene view could preview those children together and\nrestore the previous scene\'s known selections; current Undo only restores known\nprogram selections, without reversing arbitrary effects.').replace('- Tap tempo.\n', '').replace('- Receive beats, song position and time.', '- Extend the existing incoming-clock tempo measurement to song position and time.')
s += '''
## Input, remote UI and display extensions

- Accept a USB typing keyboard for text entry, search and navigation, keeping
  keyboard drivers in the target adapter and verifying socket power on hardware.
- Provide an optional browser UI over the existing USB serial transport for setup
  transfer, browsing and explicit actions. Detect browser support and keep the
  pedal independent when the browser disconnects.
- Add bounded temporary text/graphics popups over SysEx or serial. Expiry and
  dismissal must restore the current underlying UI without changing patches.
- Render the optional image references already retained in styles. Conversion,
  embedding, layout and image loading remain future work.
'''
p.write_text(s)

# Replace links to completed plans with their maintained contracts.
replacements={
 'plan/display-format.md':'doc/formatting.md',
 'plan/formatting.md':'doc/formatting.md',
 'plan/patch-switcher.md':'firmware/README.md',
 'plan/multiple-setups.md':'firmware/README.md',
 'plan/midi-interface.md':'doc/midi-interface.md',
}
for name in ('README.md','firmware/README.md','cntrl/maker/morningstar/mc/firmware/patch-switcher.md'):
    p=Path(name); s=p.read_text()
    for old,new in replacements.items(): s=s.replace(old,new)
    s=s.replace('display-format design','display formatting guide').replace('patch-switcher plan','application guide').replace('multiple-setups plan','setup guide').replace('module plan','module guide')
    s=s.replace('carry multiple reusable tags, with inherited membership and prioritized\ntag styles. The current compiled document format is version 18;', 'carry multiple reusable tags, with inherited membership and ordered\ntag-style overrides. The current compiled document format is version 25;')
    s=s.replace('The [application guide](firmware/README.md) and\n[setup guide](firmware/README.md) describe implemented behavior;', 'The [application guide](firmware/README.md) describes implemented behavior;')
    p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/code-audit-issues.md'); s=p.read_text().replace('Part of [plan/issues.md](../../../../plan/issues.md), reviewed 2026-10-28 at', 'Hardware findings from the source audit reviewed 2026-10-28 at').replace('Paths below are relative to this directory unless stated otherwise.', 'All five findings below are resolved. The completed generic audit plan was\nremoved; this appendix retains the hardware investigation evidence. Paths below\nare relative to this directory unless stated otherwise.').replace('Compare that record with the stale status text in\nthe generic plans; do not confuse installation with physical MIDI qualification.', 'Installation does not establish physical MIDI qualification.'); p.write_text(s)
