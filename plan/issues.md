# CNTRL issue inventory

Source reviewed: `01afbc4eb920b9d66e795906dc32b2de5247fd41` from `origin/main`.
This is a static review of firmware, the Python CLI, emulator, packaging and
selected hardware notes. A **code finding** follows from the implementation;
its consequences have not been reproduced unless stated. A **risk to verify**
has an additional dependency or evidence gap. Priorities reflect impact and
likely exposure. Deliberately deferred pedal navigation is outside scope.

## P2 | Reliability, correctness and recoverability

11. **Live input history grows while recording (mitigated).**
    [Replay recording](../doc/emulator.md#deterministic-replay) is off by
    default. During capture, each input port stores the original MIDI bytes
    and one 64-bit timestamp per completed message. Export expands running
    status and writes one event at a time.
    An active recording still grows with input traffic.

14. **Verify keyboard overflow feedback on the pedal.** The name editor
    shows a localized warning after the queue drops a key, while retaining
    queued edits. The native regression fills the queue and verifies the
    warning. Verify on the pedal with a USB keyboard under simultaneous
    SD and display load.

## P2 | Distribution and release risks to verify

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
