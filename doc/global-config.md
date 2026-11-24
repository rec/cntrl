# Global rig defaults

Put your normal rig in `global.toml` and give each setup only the changes it
needs. Every setup field can inherit defaults, including instruments, patches,
MIDI routes, pages, counters, bindings, fonts and ordinary preferences.

The file is loaded automatically from:

| Platform | File |
| --- | --- |
| macOS | `~/Library/Application Support/cntrl/global.toml` |
| Linux | `$XDG_CONFIG_HOME/cntrl/global.toml`, or `~/.config/cntrl/global.toml` |
| Windows | `%APPDATA%/cntrl/global.toml`, or `~/AppData/Roaming/cntrl/global.toml` |

Both a setup and its global defaults can load instrument definitions directly:

```toml
instrument_files = ['instruments/synth.toml', 'instruments/keys.toml']
```

Paths are relative to the file declaring them. The referenced TOML fragments
are merged in order before that file's own settings. Compile or copy the setup
file alone; its instrument files are captured in source archives automatically.

A missing `global.toml` means there are no user defaults. An unreadable or malformed
existing file reports an error. Compilation does not create or edit this file.
Hardware target declarations and expression calibration remain separate.

## A shared rig and a small setup

For example, your global file could contain:

```toml
brightness = 160
pickup = true

[instruments.synth]
output = 'din'
channel = 1

[patches.sound]
instrument = 'synth'
program = 10
volume = 90

[patch_cycles]
Live = ['sound']
```

This setup uses that rig on a different channel and program:

```toml
[instruments.synth]
channel = 2

[patches.sound]
program = 12
```

It inherits the instrument's output, patch volume, page cycle, brightness and
pickup setting. Use output names declared by your hardware target.

Compilation merges raw authoring data before applying model defaults. Omitting
`brightness` inherits 160; explicitly setting it to 192 uses 192, even though
192 is the built-in default. Explicit `false`, zero and empty values also win
where those values are valid for the field. `pickup = 'none'` keeps pickup's
existing inheritance meaning.

Nested tables merge by field. Lists replace completely: an explicit empty list
clears an inherited list. An empty table makes no changes to inherited fields.
The existing `+` file-composition rules apply within your setup expression;
replacement rules apply between the global defaults and that composed setup.

## Removing inherited content

Use `inheritance.remove` to remove inherited fields or whole definitions:

```toml
[inheritance]
remove = [
    ['patches', 'sound', 'volume'],
]
```

Each entry is a list of literal table keys, so a name containing a dot remains
unambiguous. Removals happen before local overrides. You can remove a whole
definition and then supply its complete replacement, which is useful when
changing the type of a patch. Missing or overlapping removal paths are errors.
Normal validation still catches references to removed instruments or patches.

Fonts, movies and catalogue paths are resolved relative to the file declaring
them. A setup does not need to repeat an inherited asset path.

## Compilation, upload and sync

Rig defaults are applied on the computer during compilation. The pedal receives
complete setups. Changing `global.toml` affects the next compilation; recompile
and copy the setups that
should receive the change.

Each setup upload retains the exact authoring sources used to compile it,
including its defaults, target, referenced source files and required assets.
An immutable source archive is verified before its reference is published with
the setup. Different uploads can retain different versions of your global rig.
Source payloads stay on SD; the setup carries a 40-byte metadata reference.
Saved setup history keeps the corresponding reference.

When [setup sync](setup-sync.md) brings a saved pedal edit back to a value that
was inherited, it proposes a setup-local override. It does not change the global
file or materialize unchanged inherited values. The existing review and
conflict-resolution process still applies.

## Recovering on another computer

Download your setups as usual:

```sh
cntrl setup download --port PORT -o recovered
```

The download includes resolved setup exports and, for uploads with source
archives, `sessions/<snapshot>/` directories containing the original authoring
sources, recorded defaults, targets, dependencies and assets. Each session has
a `cntrl-session.toml` manifest, original archive, compiled baselines and current saved
pedal images. These distinguish the uploaded sources from later pedal edits.

Compiling a recovered root automatically uses that session's recorded sources
and defaults, including its original `+` composition and target inputs. The new
computer's per-user `global.toml` does not influence the result or get overwritten.
A recorded absence of global defaults is preserved too.

Use the existing reviewed sync workflow to bring saved pedal edits into the
recovered local setup sources. Keep the complete session directory together.
The original machine's absolute paths are not required. Missing or corrupt source
files are reported rather than silently replaced with the new machine's defaults.

Backups retain source archives alongside setups and history. Older uploads that
predate source retention still support resolved export, but their original
omissions and source-file organization cannot be reconstructed from values alone.

## Live pedal-wide preferences

The pedal also keeps a separate, persistent overlay for scalar preferences.
Resolution is:

1. A value explicitly authored in the setup.
2. A value set in the device-global overlay.
3. The setup's compiled fallback, including the host rig defaults used at upload.

Clearing a device-global preference restores each setup's own fallback. It does
not edit `global.toml`, rewrite uploaded setups, or change named definitions such
as instruments, patches and routes. Those still use compilation-time defaults.
Setups uploaded before explicit-setting metadata was introduced must be
recompiled and uploaded before they can inherit live device-global preferences.

On the pedal, open **Settings** and choose the current setup, or choose **Setups**
and open a saved setup. **Setup fields** groups the compiled preferences by
task, including MIDI, Navigation, Ports, Time, and Tempo presets. The display
says whether the value comes from the setup, the pedal, or the compiled fallback.
Adjusting a value makes a setup override. **Use pedal value** clears the
override when a pedal value is
available. Save commits the draft as a setup revision. Saving the loaded setup
reloads it.

The utility groups cover clock following, tap detection, saved tempo presets,
display layout, colors, fonts, and Status refresh. Choose MIDI clock outputs in
the MIDI group. The live clock's send switch and active source stay on the Time
page.

Select **Instruments** from the current setup, from the current patch's MIDI
channel field, or from a saved setup's field list. The instrument editor shows
the current channel and output. Saving applies the new route to instrument
programs that use that instrument in the owning setup. Raw MIDI actions keep the
channel and output stored in each action.

Use the USB connection to inspect, set or clear preferences:

```sh
cntrl global list --port PORT
cntrl global set brightness 160 --port PORT
cntrl global set frame_buffers 1 --port PORT
cntrl global set scrolling.horizontal_duration '250 ms' --port PORT
cntrl global clear brightness --port PORT
cntrl global restore device-globals.toml --port PORT
```

`list` reports `inherit` for unset device values. `restore` atomically replaces
the whole overlay with a downloaded snapshot, including clearing omitted values.

The supported preferences are brightness, screensaver timeout, MIDI interface
and USB forwarding flags, `fast_debounce`, frame buffers, animation flags, pickup
and pairing delay, scrolling directions and durations, navigation timings, TRS
port roles, auxiliary-switch modes, clock source and outputs, target-pulse
settings, and the loop-stop controller-reset policy. Physical TRS port indices
start at one, for example `trs_ports.1`. Patch actions may also use target port
names and clock output-name lists. The USB CLI uses physical port indices and an
integer bitmask for clock outputs.

Brightness and screensaver changes apply immediately. MIDI interface changes
require idle MIDI output. Other preferences can require reloading the active
setup. Reload-sensitive edits are rejected during active notes, held pedals,
recording, playback, an unsaved looper take or pending output. Changes that would
invalidate any stored setup are rejected before persistence. A rejected change
leaves the previous overlay in force.

`pickup = 'none'` and `'None'` inherit the live device-global pickup policy.
Explicit `true` or `false` protects the setup's own choice. A deliberate
`inheritance.remove` reset remains protected from device inheritance.

Existing switches can invoke the same operation through a patch:

```toml
[patches.dim]
operation = 'global-setting'
setting = 'brightness'
value = 80

[patches.restore_brightness]
operation = 'global-setting'
setting = 'brightness'
```

Omitting `value` clears that device-global preference. These actions do not add a
settings page or change the existing editor's setup-local scope. A local editor
save becomes explicit even if its numeric value matches the compiled fallback.

Downloads and reviewed sync keep `device-globals.toml` separate from rig defaults
and setup sources. Backups retain both the overlay and its on-device journal;
restoration applies the overlay within the staged deployment. A backup without
an overlay preserves the destination pedal's current overlay. A fresh emulator
storage directory imports a recovered session's snapshot, while subsequent
starts preserve its existing device preferences.

The startup set is another device-wide preference. In Songs and setlists, choose
a set, open its edit menu and select Toggle startup set. Its name appears below
that action; selecting it again clears the choice. Startup opens the set's initial
empty position without executing song or patch actions. The preference is saved
as `startup_set` in version-3 portable snapshots and restored with the other
device preferences. A missing saved set reports an error rather than choosing a
different set.

## Owner name and contact

`name_and_contact` is one device-wide text field with up to 127 UTF-8 bytes.
Set it on your computer with a quoted multiline value.

```sh
cntrl global set name_and_contact 'Alex Example
owner@example.org
+1 555 0100'
cntrl global list name_and_contact
```

CNTRL saves the text in `owner.toml` beside your computer's `global.toml`.
Setting or clearing it updates a single connected CNTRL pedal immediately.
`cntrl compile` also applies the saved value if one pedal is connected.
Use `--port PORT` to choose a pedal explicitly when more than one is connected.
Setup and song uploads apply the saved value to the pedal receiving the upload.
With no pedal connected, the computer keeps the value for the next compile or
upload. The pedal needs firmware that supports the field.

The pedal saves the text in its SD-card settings journal. On the MC6 Pro, open
Status and press C to switch between the diagnostics and the contact view.
Use `cntrl global list name_and_contact --from-pedal` to read the full text over USB.
To clear both copies, run `cntrl global clear name_and_contact` with the pedal connected.
Clearing it offline saves an empty value to apply on the next sync.
The translated Contact caption requires the language packs built for this firmware.

For the appearance of the Contact body, add an optional style to the setup.
The image sits behind the text. CNTRL checks the contrast under the lettering
and uses black or white with an outline where needed. Choose a built-in UI font
or omit the font to use the current language's font.

```toml
[page_settings.contact_style]
image = 'contact.png'
background_color = 'navy'
foreground_color = 'white'
font_name = 'Arimo'
```

The style and its image travel with the compiled setup. The owner text still
comes from the pedal's device settings.

Compiled setups and song libraries contain no contact text. Pedal settings
backups can include it in `device-globals.toml`, so share those files only if you
want to share the contact details. The text is an editable owner label.

## Default boot firmware

On an MC6 Pro with the combined image installed, open **Settings**, choose
**Pedal**, then **Boot firmware**. Saving there changes the next boot. To change
the same preference over USB while CNTRL is running, use these commands.

```sh
cntrl global list default_firmware --port PORT
cntrl global set default_firmware stock --port PORT
cntrl global set default_firmware cntrl --port PORT
```

`cntrl global clear default_firmware --port PORT` restores CNTRL as the default.
The pedal stores this choice in flash independently of its setups and SD-card
preferences. Installing a fresh combined image restores the CNTRL default.

From stock, press A+F together to enter CNTRL for one boot. From CNTRL, a
`stock-firmware` patch action enters stock for one boot and specifies the chord
that returns to CNTRL during that visit. Both switches preserve the boot default.
