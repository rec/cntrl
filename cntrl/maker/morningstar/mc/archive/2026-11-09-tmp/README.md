# Archive of temporary cntrl artifacts

Collected on 2026-11-09 from `/private/tmp/cntrl-*` before access to that
directory is retired. Original temporary files were left in place.

This directory is a historical archive. Its source fragments, diagnostics,
patches and setup formats are preserved as found; they are not part of the
current firmware build or test suite. Paths mentioned inside them describe
their original working environment.

## Contents

* `backups/cntrl-before-latest-88mcxda6/` preserves downloaded setup data,
  exported TOML, fonts, song data, selection/status records and a format-30
  conversion. The other eight `cntrl-before-latest-*` directories have the
  same original setup data.
* `backups/cntrl-trs-ports-device-backup/` preserves the earlier setup binaries,
  prepared replacements and associated assets/records. The files in
  `cntrl-derived-before-20261105/` are duplicates of this backup.
* `diagnostics/` preserves the derived-patch playback check and fixture,
  expression-document check and fixture, and expression hardware mocks.
* `patches/` preserves the nine top-level patch snapshots, including the
  uncommitted/preexisting TRS and clock work.
* `snapshots/` preserves small intermediate expression, TRS and page source
  versions that differ from committed snapshots. They are historical evidence,
  not replacements for the maintained implementations.
* `firmware/` preserves 17 distinct cntrl application payloads as raw binary
  images. Their load addresses and provenance are recorded in `manifest.toml`.

## Firmware ownership and extraction

No Morningstar Engineering firmware dump or combined stock/custom firmware
image is included in this archive.

The 11 distinct backup HEX images, plus the clock-stage combined image, contain
Morningstar's stock application below `0x60100000`. Only the cntrl application
range beginning at `0x60100000` was retained from those images. Every byte below
that address was excluded, including the stock application, stock patches,
boot-selector hook and intervening padding. The existing boot-selector build
in `../../boot_switch.py` defines this separation.

The other five images come from standalone cntrl builds: restoration-check,
cable-disabled, MIDI-files-final, MIDI-state and MIDI-state-baseline. Their
payloads begin at `0x60000000`. These are compiled cntrl applications, not
stock firmware dumps. The clock-stage standalone relocated build matches the
payload extracted from its combined image and is stored once.

All retained ranges are contiguous. Each binary preserves the original bytes
and load address without HEX record formatting. The filename combines that
address and the binary's SHA-256. The manifest also records original HEX hashes
and all matching source paths, so original combined images can be identified
without storing their contents.

These raw binaries are archival artifacts, not complete upload packages. The
relocated applications require the separate boot-selector layout. They must
not be treated as independently bootable or uploaded as complete flash images.

## Integrity and omissions

`manifest.toml` records the size, SHA-256 and original temporary source paths
for each of the 97 payload files. Source paths are relative to `/private/tmp`.
The README and manifest are archive metadata rather than payloads.

Copied data and source files were checked byte-for-byte against their origins.
Firmware payloads were independently decoded from the original Intel HEX
records and checked against the archived bytes and addresses. Original combined
images were also compared with the local stock reference to confirm the vendor
application was present and needed exclusion. No device was flashed or queried.

Build trees, object files, executables, caches, logs, Finder metadata and
one-use edit/rebase scripts were omitted. Of 74 top-level temporary Python
scripts, 73 already exist byte-for-byte in this repository's history under
`rescued_scripts`; they were later removed by `Replace rescued_scripts with
cntrl setup backup`. The remaining script, `cntrl-single-debounce.py`, only
performed source edits for debounce work already committed.

The audit covered 388 top-level temporary entries, 5,511 files and approximately
657 MiB. Only the selected historical material is preserved here; duplicate
setup data and identical firmware payloads do not need all their temporary
copies.
