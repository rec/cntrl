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

Defaults are applied on the computer during compilation. The pedal receives
complete setups and does not merge defaults when switching setups. Changing
`global.toml` affects the next compilation; recompile and copy the setups that
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
