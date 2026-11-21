# CNTRL issue inventory

Source reviewed: `01afbc4eb920b9d66e795906dc32b2de5247fd41` from `origin/main`.
This is a static review of firmware, the Python CLI, emulator, packaging and
selected hardware notes. A **code finding** follows from the implementation;
its consequences have not been reproduced unless stated. A **risk to verify**
has an additional dependency or evidence gap. Priorities reflect impact and
likely exposure. Deliberately deferred pedal navigation is outside scope.

## P2 | Reliability, correctness and recoverability

7. **Native I/O stalls block the emulator window (code finding).**
   [The GUI timer](../cntrl/emulator/gui.py#L585) calls session operations
   synchronously, so user input and repainting stop during a stall. Headless
   replay and the live clock both advance long gaps in watchdog-bounded
   requests. A three-hour replay that previously timed out completed in
   31 seconds, and an injected live clock gap reached the exact target time.
   Verify catch-up after actual host suspend. Keeping the window responsive
   during a stalled request remains open.

9. **Mirroring needs a device-side consistency check.**
   [Capture](../cntrl/emulator/mirror.py#L44) rechecks every downloaded file,
   repeats the storage scan, and publishes completed mirrors atomically.
   A write after a file's final read, or one reversed between reads, can
   still escape detection. Validate the complete manifest and Status
   snapshot against one device-side generation or prove that the device
   stayed idle.

11. **Live input history grows for the life of the emulator (code finding).**
    [Session input methods](../cntrl/emulator/backend.py#L438) append replay
    events including inbound MIDI. The [GUI bounds the visible log](../cntrl/emulator/gui.py#L333)
    and drains output messages, but the input list lives until session exit
    and is [used on scenario export](../cntrl/emulator/gui.py#L718).
    This is unbounded intentional retention, rather than a lost allocation.
    Explicit recording or streaming to disk would preserve replay fidelity
    while bounding live memory.

14. **Verify keyboard overflow feedback on the pedal.** The name editor
    shows a localized warning after the queue drops a key, while retaining
    queued edits. The native regression fills the queue and verifies the
    warning. Verify on the pedal with a USB keyboard under simultaneous
    SD and display load.

## P2 | Distribution and release risks to verify

16. **Bitmap font provenance needs a release decision.**
    [The font exporter](../cntrl/maker/morningstar/mc/export_fonts.py#L63)
    derives ComicSansMS assets from a Teensy SDK source. It emits notices for
    bundled outline fonts and Tamzen, but has no corresponding notice branch
    for ComicSansMS. The derived resource is tracked in
    [the distribution tree](../cntrl/maker/morningstar/mc/distribution/fonts/ComicSansMS.cfont).
    Confirm the original glyph provenance and which terms cover redistribution
    of the derived raster. The presence of code in an SDK does not establish
    the rights to every bundled asset. Replace the raster if redistribution
    terms cannot be established.

17. **First-party licensing and vendor image handling lack a clear release
    boundary.** The reviewed tree has asset-specific notices but no root
    project license or license declaration in [package metadata](../pyproject.toml).
    Separately, the [distribution plan](firmware-distribution.md#L24) explicitly
    retains a cached stock download while repository instructions prohibit
    preserving vendor firmware. Record the owner's intended license and
    resolve the retention policy. Verify package contents, caches and release
    artifacts against that decision. These are provenance and policy gaps,
    not conclusions about infringement.

## P3 | Maintenance and performance

21. **Historical setup snapshots have no retention limit (risk to verify).**
    [Sync snapshots](../cntrl/patch_chooser/setup_sync.py) create new
    directories with source and setup copies. A two-setup round-trip test
    retained five snapshots and conversion records using 320 KiB on disk.
    Measure growth during show use before choosing a retention policy that
    preserves current baselines, pending recovery and intentional backups.
    The [native build cache](../cntrl/firmware/native_cache.py) now has a
    separate 2 GiB limit for generated executables.

## Hardware and lifetime follow-up

- The [physical scrolling investigation](scrolling-bug.md) records an
  unresolved leftward fringe. Keep this as a hardware reproduction item.
  The source review does not establish whether today's firmware still
  reproduces it or what causes it.
- No confirmed use-after-free was found in the sampled allocation and language
  paths. Language lookup and localized font views borrow the active pack's
  storage, which [Languages::finish](../firmware/include/cntrl/ui/languages.h#L138)
  swaps and then releases. Verify that pending draws and other consumers
  cannot hold a view across a language change, using sanitizer checks on
  native code and a separate device check around display DMA. Audit other
  firmware ownership paths separately before making a general lifetime claim.
