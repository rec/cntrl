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
   synchronously, with a [ten-second request watchdog](../cntrl/emulator/backend.py#L146).
   User input and repainting stop during a stall. Also, [native advance](../firmware/emulator/main.cpp#L559)
   steps through every elapsed millisecond, so a large replay gap or host
   suspend can look like a dead child. Measure idle catch-up and stuck-peer
   behavior separately before choosing execution and cancellation bounds.

9. **Mirroring can capture several points in time (code finding).**
   [Capture](../cntrl/emulator/mirror.py#L40) reads Status first, scans
   storage, then downloads and rechecks mutable files already seen. A new
   journal slot created after enumeration can go unchecked, and a
   status snapshot need not correspond to the final saved state. Size checks
   alone do not catch same-sized changes to files outside the rechecked set.
   Failed captures also leave a partial directory. Validate the complete
   manifest against one generation or require a demonstrably idle device,
   and distinguish complete captures from partial ones.

11. **Live input history grows for the life of the emulator (code finding).**
    [Session input methods](../cntrl/emulator/backend.py#L438) append replay
    events including inbound MIDI. The [GUI bounds the visible log](../cntrl/emulator/gui.py#L333)
    and drains output messages, but the input list lives until session exit
    and is [used on scenario export](../cntrl/emulator/gui.py#L718).
    This is unbounded intentional retention, rather than a lost allocation.
    Explicit recording or streaming to disk would preserve replay fidelity
    while bounding live memory.

14. **Keyboard queue overflow silently discards editing actions
    (code finding).** [The ring](../firmware/include/cntrl/keyboard_input.h#L8)
    holds 31 pending keys and returns false when full. [The target callback](../cntrl/maker/morningstar/mc/firmware/patch_chooser/target.cpp#L110)
    ignores that result. A burst while the loop is busy can lose a character,
    Enter, or Escape with no feedback. Verify under realistic SD/display
    load and decide how the editor indicates discarded input.

15. **Crash reporting can fail to open a draft or identify the actual runtime
    (code finding).** GitHub can reject a [prefilled form URL](../cntrl/crashes.py#L37)
    if the traceback makes it too long. The full traceback remains on stderr.
    [The build hook](../scripts/build_hook.py#L11) skips the stamp when no
    Git metadata exists, which is correct for a stamped source distribution
    but also applies to an ordinary unstamped source archive. In that case
    reading the stamp aborts report creation. Furthermore, the Python package
    revision can differ from [the native emulator revision](../cntrl/emulator/backend.py#L117)
    selected by `--source-root`. Allow an explicit unknown stamp and report
    both revisions when available. Separately, the CLI's
    [broad conversion of OSError and ValueError to short messages](../cntrl/patch_chooser/cli.py#L765)
    can hide programming errors of those types. Identify expected user and
    transport failures at their boundaries so unexpected ones keep a trace.

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

19. **Setup sync and the compiler are hard to review by responsibility.**
    [compiler.py](../cntrl/patch_chooser/compiler.py#L282) has about 2,800
    lines and mixes catalogue expansion, patch graph validation, display
    assembly and binary encoding. [setup_sync.py](../cntrl/patch_chooser/setup_sync.py#L283)
    has about 1,270 lines and mixes review construction, source attribution,
    migration, transaction publication and recovery. Function size compounds
    file size. Isolate boundaries as behavior changes, starting with review
    publication and recovery because they share critical invariants.

21. **Historical snapshots and native builds can consume disk unchecked
    (risk to verify).** [Sync snapshots](../cntrl/patch_chooser/setup_sync.py#L342)
    create new directories with source and setup copies, and the [native cache](../cntrl/firmware/native_cache.py#L49)
    retains outputs by build key. Measure growth during ordinary development
    and show use. Any pruning must preserve current baselines, pending
    recovery records and intentional backups; build-cache retention can be
    handled separately.

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
