# Installing packaged MC6 Pro firmware

The Python package contains our compiled application, matching boot hook, target
fonts and a quiet starter setup. It does not contain Morningstar's firmware or a
combined image. No compiler or source checkout is required to install it.

Only the pinned MC6 Pro v3.14.0 release named in `distribution/manifest.toml` is
supported. Stock firmware is downloaded from Morningstar's GitHub release and
cached under its original filename in the platform user cache, inside
`cntrl-firmware/mc6-pro/`. Our verified SHA-256 must match on every use. A valid
cached copy is used without any network request; no latest-version lookup runs.

## Prepare and start the upload helper

Provision the pinned uploader once, without accessing a pedal:

```sh
cntrl upload-helper --controller mc6-pro provision
```

Linux needs an explicitly managed `CNTRL_TYCMD`, as described in
[the helper guide](firmware/upload-helper.md). Offline use also requires the
Python package, uploader and verified stock file to be present already.

Start the helper manually in a separate terminal, selecting your unit's USB
serial number. Keep it running during the installation:

```sh
cntrl upload-helper --controller mc6-pro serve --serial SERIAL
```

This is the unit serial, not a serial-port path. Use your operating system's USB
device information or `tycmd list --verbose --output json` with the provisioned
uploader to find it. The helper checks that each request names the same unit.

## First installation from Morningstar firmware

1. Use Morningstar's updater to upgrade an older stock version to the pinned
   supported release. Follow Morningstar's backup instructions before upgrading.
2. Export a fresh **All Banks** backup, including controller settings, with the
   Morningstar editor after that upgrade. Retain this stock recovery artifact.
3. Close the editor and other USB serial applications and start the helper.
4. Run:

```sh
cntrl flash --controller mc6-pro --serial SERIAL --first-install --stock-version 3.14.0 --editor-backup /path/to/editor-backup
```

`--first-install` explicitly confirms that you selected a physical MC6 Pro;
`--stock-version` confirms the version you checked on it. The stock protocol does
not provide cntrl's identification service. The installer checks the stock USB
product and selected unit, keeps an exact copy/hash of your editor backup and
prepares everything before requesting bootloader entry.

The pinned image advertises CDC serial interfaces, VID `331b`, PID `0007`.
The installer tries Teensy's 134-baud software bootloader request on that exact
USB identity and serial. Where TyTools advertises a reboot capability, it can
also use its software entry command. It waits for the same serial to enumerate
as an upload-capable Teensy 4.1 HalfKay device. If entry is unavailable, it stops
noninteractive installs and offers an interactive manual update-mode step.
It never uploads to a different detected board.

Static inspection does not prove that stock handles the reboot request. This
automatic stock-entry path still requires physical qualification. The normal
[Morningstar update instructions](https://help.morningstar.io/en/article/mc6-pro-user-manual-psevjy/)
use the software uploader; the internal button accessed by removing the backplate
is their [recovery procedure](https://help.morningstar.io/en/article/firmware-upload-failure-controller-does-not-boot-up-1b7h2fe/).

After the firmware returns, the installer backs up its initial cntrl storage,
refuses to overwrite existing cntrl collections and installs the quiet starter
through transactional deployment. It sends no MIDI, defines no instruments and
disables MIDI routes and forwarding. Upload your own setup afterward. The SD
card is never formatted, and vendor presets are not converted into cntrl setups.

If the upload reports an error, the installer checks which firmware boots. If
the expected build reports ready, it continues with the starter installation.
Otherwise it can retry up to five times, with delays of 100 ms, 200 ms, 300 ms,
1 s, and 2 s. Each upload requires the selected pedal to be in HalfKay again.
After those attempts, it can make up to five attempts to restore the pinned
Morningstar image. It confirms rollback when the stock upload finishes and
the same pedal returns in Morningstar mode. If recovery fails, the terminal
shows instructions for the internal PROGRAM button. Upload errors and retries
are recorded in [the flash log](../../../../doc/flash-record.md).

## Updating cntrl

```sh
cntrl flash --controller mc6-pro --port PORT
```

The installer discovers the serial from the selected cntrl USB port, checks it
against the running helper, makes a fresh backup, and prepares offline conversion.
It uses the same boot verification and silent retries as a first install. Once
the expected firmware boots, the installer restores and verifies the saved data.
Add `--legacy-usb` only for the earlier cntrl USB transport.

Every installation uses a fresh `.cntrl-install/<id>/` directory by default.
`--output PATH` selects another new directory. Keep its `installation.toml`,
image, `before/` and `restore/` trees for recovery. If retries are exhausted,
the update reports failure and keeps the backup. Stock rollback applies only
to first installs. The installation record identifies the source commit,
stock, component and image hashes, and the uploader.

To populate the cache and inspect an image without opening any device:

```sh
cntrl flash --controller mc6-pro --prepare-only
```

## Maintainer resource updates

Commit source first, then build from that committed source:

```sh
cntrl build --target-dir cntrl/maker/morningstar/mc --output build/release --publish
```

`--publish` verifies the component hashes and source inputs, then copies only the
own-code resource set into the tracked `distribution/` directory. Stage
that directory explicitly in a following artifact commit. The firmware's displayed
Git ID is the source commit, not that later artifact commit. Never stage the
downloaded stock or combined image. `compiler-options.toml` and `toolchain.toml`
record the build inputs; the resource manifest hashes them and the payload.

For a maintainer's uncommitted component export, use the same installer with
`--resources build/release/distribution`; this does not rebuild or create a
second composition implementation. Publishing resources requires committed
inputs that still match the build.

To reproduce the read-only stock USB investigation:

```sh
python -m cntrl.maker.morningstar.mc.stock_usb --stock /path/to/pinned-stock.hex --output build/stock-usb
```

This requires the maintainer ARM disassembler and writes disposable decoded
stock and disassembly into the output directory. None of those outputs belongs
in a Python distribution. TyTools' software entry implementation is documented
in its [source](https://github.com/Koromix/rygel/blob/master/src/tytools/libty/class_teensy.c).

Build both Python distribution formats and check their contents and installed
preparation before publishing:

```sh
uv build
python -m cntrl.maker.morningstar.mc.check_package --artifacts dist/cntrl-0.1.0-py3-none-any.whl dist/cntrl-0.1.0.tar.gz
```

The cross-platform qualification workflow also installs the wheel outside the
checkout and runs this check with an empty compiler PATH, including a second
composition with downloads blocked. Combined images stay in its temporary
directory and are excluded from published CI artifacts.

New Morningstar releases require maintainer review and qualification of patches,
hook, settings transitions and compiled resources before publishing a new pairing.
Existing packages do not silently change their stock requirement.

## Local qualification, 2026-11-15

The initial committed payload was built and published with `cntrl build --publish`
from source `5d620027e649cfd42fa1379a856ae8a8db317828`. It contains a
1,305,600-byte application and a 1,120-byte hook, plus target metadata, fonts and
the quiet starter collection. Its displayed/USB Git ID is `5d62002`.

Both wheel and sdist passed the resource/hash inventory check. Neither includes
stock/combined HEX images, the SDK, generated firmware build directories or the
unrelated worktree screenshot. The installed wheel was checked in a separate
environment outside the checkout, without the emulator extra, native rasterizer
provisioning or compilers on PATH. Both its public `cntrl flash --prepare-only`
command and the package preparation check succeeded. A second preparation with
the download function blocked produced an identical combined image from cache.

These checks did not open a serial port, launch an upload helper or flash a
pedal. Automatic entry from stock, first-install storage initialization, and the
complete upload/reset/restore flow still require physical qualification on the
supported hardware, including the hub case. The manually started old helper
must be restarted with the new selected-serial protocol before such a test.
