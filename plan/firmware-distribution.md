# Firmware distribution without a build toolchain

Implementation status: the packaged-resource build, verified stock cache,
installation command, first-install flow and installed-package checks are
implemented. Local preparation has been qualified without a compiler on PATH,
including verified offline cache reuse. Physical first-install/update and stock
bootloader-entry qualification remain separate authorized hardware work.
See the adapter's [installation and qualification notes](../cntrl/maker/morningstar/mc/firmware-distribution.md).

## Goal and agreed decisions

Ship our compiled firmware as committed resources inside the `cntrl` Python
package. Installing and flashing a published package must not require a source
checkout, ARM compiler, SDK, linker, font generator or other build tools.

The package contains only our compiled components. At installation time, cntrl
downloads the supported stock firmware, caches it and combines it locally with
our components. Neither stock firmware nor a combined image is committed or
included in our Python distributions.

The agreed policy is:

- Each packaged firmware supports one explicitly pinned stock release.
- Cache the stock file under the vendor's exact download filename. Also verify
  our recorded SHA-256 of the bytes tested with our patches. This is our
  verification hash, not a hash supplied or certified by the vendor.
- Reuse a valid cached file without contacting the vendor, including for update
  checks. Never resolve a moving "latest" release during installation.
- First-time users upgrade to the supported stock release using the vendor's
  updater, then export a backup with the vendor's editor before installing cntrl.
- Existing cntrl installations keep the automatic backup, restore and
  verification workflow.

This document describes the generic distribution flow. Hardware-specific
resources, code, tests and instructions remain under their manufacturer adapter.
For the existing implementation that is `cntrl/maker/morningstar/mc/`.

## Existing implementation and necessary changes

`cntrl/flash.py` currently builds by default and dispatches to the selected
target's flash entry point. `cntrl/build.py` exports an image and target through
`build.toml`. The existing adapter builds both our application and a boot hook,
then combines them with stock firmware in `boot_switch.py`.

The byte-composition operation already exists separately from compilation. Reuse
it as the single composition implementation. Export the compiled hook as well
as the application: the hook is linked to that application's entry point, and
composition also needs its stock-reader address. Shipping the application alone
is insufficient.

Two current bench restrictions also prevent distribution: the upload helper
requires an image inside a registered Git worktree, and device selection contains
a fixed unit serial. Replace these restrictions with private installer staging
and explicit selection of a compatible physical unit, preserving the helper's
authentication and image validation. This is necessary for users without Git
checkouts and for pedals other than the development unit.

## Packaged resources

Keep one current compiled resource set per supported controller in its adapter's
Python package. Use raw binary files for our application and hook, together with
a TOML manifest. Do not add Git LFS or an external firmware download service.
Old resource sets remain in Git history rather than accumulating in every wheel.

The manifest records the information required to validate and compose that set:

- Manifest format version and supported physical controller identity.
- Firmware source commit and build provenance, including toolchain and options.
- Application and hook resource paths, SHA-256 values, load addresses and the
  linked entry/reader information required by the existing composer.
- The supported stock release, original filename, official download URL and our
  verified SHA-256.
- Packaged target metadata and its referenced fonts, with resource hashes.
- A minimal safe starter collection for first installation, compiled for this
  firmware's data format, with its source configuration retained in Git.

Keep patch offsets, expected stock bytes and target-specific image validation in
the adapter. A downloaded stock file must not supply executable configuration,
patch instructions or an alternative download location.

All resources must work when loaded from an installed wheel outside the
repository. Read installed package resources and copy the required files into
private staging; never modify `site-packages`. Include the resources in both
wheel and sdist, and verify the contents of each distribution. Do not rely on
ignored build directories or local provisioned SDK files.

Committing binaries increases repository history and package size. Accept that
tradeoff for the simple, self-contained distribution requested here. Commit only
the releasable components and required runtime data, not ELF/debug files, maps,
SDKs, stock downloads or combined images.

## Maintainer build and firmware identity

Keep `cntrl build` as the maintainer operation. It builds the application and
matching hook and exports the resource set and manifest. It must not require an
end user to reproduce that build.

The firmware embeds its source commit. A binary cannot embed the hash of the
commit containing that same binary. Use this sequence:

1. Commit firmware source as commit S.
2. Build from the clean source at S, embedding S and recording it in the manifest.
3. Qualify the generated resources against their pinned stock release.
4. Commit those resources and manifest in a following packaging commit A, without
   intervening firmware source changes.

The pedal reports S as its firmware source identity. The Python package may come
from A or a later commit that does not change that firmware. Release checks must
detect stale resources relative to firmware inputs, and verify that the manifest
and embedded source identity agree. Record the build procedure and inputs so a
maintainer can reproduce and inspect the committed payload.

## Stock firmware cache

Use the platform's user-cache location, following the existing uploader cache
conventions, with a controller-specific directory and the original vendor
filename. Do not put downloads in the package, repository or current directory.

For every installation:

1. Read the exact stock requirement from the installed resource manifest.
2. If the cached file exists, verify its complete SHA-256 locally. A valid hit
   requires no network request.
3. If absent, download that exact official URL into a temporary file alongside
   the cache entry. Verify it before atomically publishing the cache entry.
4. On a hash mismatch, stop with a clear error naming the file and expected
   release. Do not use it, silently accept replacement bytes, substitute another
   version or repeatedly redownload it.

An interrupted download is never a valid cache entry. Failed downloads must not
replace a valid entry. Multiple installers must not expose partially written
files. Use the standard library and existing dependencies.

A filename identifies the intended release; its hash identifies the tested
contents. If the vendor changes bytes at the same URL or removes the download,
installation fails until a maintainer reviews the change. A previously verified
cached copy remains usable offline.

## Installation command and composition

Extend the existing `cntrl flash` command to select a supported physical
controller and use its packaged resource set. Normal flashing prepares that set
instead of compiling. Keep explicit maintainer builds available without retaining
two independent patching or installation implementations.

Resolve physical controller support through the existing catalogue and adapter
boundary. Do not depend on emulator layouts or treat provisional emulation
profiles as supported flash targets. Generic CLI modules must not eagerly import
manufacturer implementations.

Before requesting bootloader entry:

1. Validate the packaged manifest, resources and supported controller.
2. Obtain and verify the cached stock file and required native uploader.
3. Compose the image using the existing adapter checks: stock hash, expected
   patch bytes, application/hook placement, flash geometry and reserved regions.
4. Write and validate the resulting image in private staging.
5. Complete the applicable backup and preflight workflow below.

No compiler, linker, `objcopy`, `nm` or font generation may run on this path.
Local composition does not itself write the pedal. Preserve the existing
upload-only helper boundary and pinned native uploader provisioning. Do not
silently start an unrestricted device service.

Select the user's device explicitly and retain its identity through application
and bootloader transitions. Reject ambiguity and incompatible models before
upload. A generic USB board identity alone is not proof of controller model.
Allow private staged images outside Git while retaining ownership, immutable
snapshot, digest and target checks.

The uploader remains a native platform requirement, not a build toolchain. Follow
the existing platform support policy, including an explicitly supplied uploader
where no pinned native binary is available. Offline flashing requires that
uploader, the Python package and the verified stock file to be present already.

## First installation and subsequent updates

### First installation from stock

Explain the prerequisites before touching the pedal:

1. Upgrade older stock firmware to the pinned supported version using the
   vendor's updater and its documented procedure. Do not make our installer
   perform an unqualified settings migration from arbitrary older releases.
2. Export the required editor backup after that upgrade. Keep the original file
   and record its path and hash with the installation record. Do not claim that
   the cntrl backup protocol can back up stock firmware settings.
3. Verify that the selected pedal is the supported model and stock version. If
   the existing protocol cannot establish these facts, define the required
   user-confirmed identification step in the adapter's installation instructions.
4. Compose and upload the supported combined image.
5. Install the packaged safe starter collection through the existing
   transactional deployment/recovery mechanism, then verify the running firmware
   identity, startup readiness and stored data.

The starter collection must provide valid initial cntrl data without automatic
musical actions. Do not format the SD card, forge conversion/completion markers
or bypass deployment guards. Preserve any existing cntrl data and reject a
mistaken first-install classification rather than overwriting it.

The editor backup remains a stock recovery artifact. Automatic conversion of
vendor presets into cntrl setups and a new automatic stock-backup client are
outside this implementation.

### Updating an existing cntrl installation

Keep the existing automatic backup of setups, history, referenced assets, songs
and selection. Prepare and validate any required offline data conversion before
upload; preserve immutable backups and restore through staged activation and
readback verification afterward. Never reuse a stale backup as evidence that the
current pedal is safe to update.

Use the stock version paired with the new packaged firmware when composing the
update. Existing cntrl users must not be sent through a stock-only flash that
would bypass this preservation workflow. Maintainers must qualify the transition
when changing that stock pairing.

Treat uploader failures, reset failures and restore failures as failures even if
the pedal subsequently reappears. Preserve the image, backup and diagnostic
record for explicit recovery. Do not hide a failure with an automatic second
upload.

Keep a durable local installation record outside the package and cache. Record
the controller/unit, package version, firmware source commit, stock filename and
hash, component and combined-image hashes, uploader identity, backup paths and
upload/restore result. Backups must survive cache eviction and failed installs.

## New vendor releases

A new vendor release changes nothing automatically. Existing packages keep their
pinned pairing and valid cache entries. Users must not assume a newer stock
release is interchangeable with the one the package supports.

To support a new release, maintainers inspect the stock changes, update and test
the adapter's patches and hook, verify settings/data transitions and qualify a
fresh compiled resource set. Publish that pairing in a new cntrl package. Cache
different vendor filenames separately; do not relabel an older cached file.

## Implementation and verification

Implement in this order:

1. Separate maintainer component export from local composition, reusing the
   current composer. Add the TOML manifest and committed own-only resources.
2. Add exact-version cache preparation and installed-resource loading.
3. Make ordinary `cntrl flash` prepare packaged components; remove source-tree
   assumptions from that path and generalize helper staging/device selection.
4. Add the explicit stock-first-install flow and retain the cntrl update flow.
5. Verify packaging, document platform prerequisites and qualify real installs.

Focused tests should cover manifest/resource mismatches, valid offline cache
hits, missing and corrupt cache files, interrupted downloads, wrong stock patch
bytes, invalid component ranges, wrong/ambiguous devices and staged-image
validation. Test downloads at an injected boundary without live service calls.
Keep existing backup, composition and deployment tests rather than duplicating
their implementations.

Test both wheel and sdist contents: our binaries and runtime data are present;
stock and combined images are absent. Install the wheel outside the checkout and
prepare an image with build tools unavailable on PATH. Verify a warm-cache run
makes no network requests and leaves installed resources unchanged.

Use platform CI for macOS, Linux and Windows preparation and packaging. Hardware
qualification is separate and requires explicit authorization: exercise first
install and cntrl update on a supported pedal, including through a USB hub.
Require one successful upload, normal reset, correct displayed source identity,
ready state and verified durable data. Retain evidence of backup and recovery.

Acceptance means a published Python package can prepare and flash its supported
firmware without a checkout or compiler; repeated flashing uses cached stock;
first-install backup requirements and existing cntrl data preservation are
enforced; and unsupported stock bytes or devices fail before upload.

## Additional work beyond the prompt

None.
