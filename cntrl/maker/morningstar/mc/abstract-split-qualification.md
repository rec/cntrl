# Split qualification and deployment state

Date: 2026-11-07. Portable implementation: `4ea6e30`, with the warning and
porting-guide correction in `38a5bd5`. Additional work beyond the prompt: None.

This is a historical deployment record. Format migration and the
`setup upgrade-backup` command were removed on 2026-11-09; current tools accept
only the current formats.

The portable application extraction is implemented; its maintained contract is
in the [porting guide](../../../../doc/porting.md).
The final image has also been flashed and the converted backup restored and
verified on the pedal. Host and hardware evidence are distinguished below.

## Requirement audit

| Plan requirement | Implementation and evidence |
| --- | --- |
| One explicit implementation selection | `cntrl/build.py` invokes the supplied directory's executable with absolute source/output paths; the executable produces relative `build.toml` artifacts. Generic build tests check relocation and failure propagation; the local entry test checks unrelated working directories and stale imported helpers. |
| Portable Python and assets | `cntrl/fonts`, `cntrl/animation` and `cntrl/firmware` own shaping, rasterization, formats and HEX handling. Isolated tests run without `cntrl/maker`; a relocated export prepares outline fonts at a non-unit scale and compiles a setup. |
| Inward dependency boundary | `scripts/qualify_portable.py` checks imports, includes, generated C++, build recipes, CPU/register/linker dependencies and fixed geometry before copying the portable tree. Only standard headers or portable/generated headers are accepted. The copied tree contains neither the implementation nor its SDK. |
| Complete UI path | Portable `ui/pages.h`, `ui/page_host.h`, `ui/renderer.h`, font/raster/presentation components own pages, pixels, layout and transitions. Native UI tests cover retained buffers, rejected submissions, clipping, ring retries, heterogeneous windows and saved font bearings. The adapter submits bounded display transfers. |
| Input and runtime coordination | `buttons.h`, `ui/input.h` and `runtime.h` own debounce, fast switches, dispatch and deadlines. Native pipeline tests pass raw samples through the controller and MIDI sink: first delivery is 1 ms normally, 3 ms when a display transfer delays acquisition, and 16 ms with normal debounce. Bounce produces one expected message sequence. Runtime tests check scan-before-display/MIDI ordering and timestamp wrap. |
| Application ownership | `ui/application.h`, `ui/song_sources.h`, `ui/firmware_visit.h` and `asset_commands.h` own menus, focus, song capture, operation gating, continuation and command routing. The local composition binds drivers and service objects. |
| Persistence | Portable filesystem, setup/slot/asset storage, startup journal, history import, expression and MIDI-file components own policy. Native and Python tests cover interrupted saves, short journal I/O, corrupt slots, retry conflicts, revision reindexing and recording/playback. Local code binds FsFile, ADC and the device transports. |
| Memory planning | Explicit target capacity/runtime values drive the host planner and firmware validator. Static/LRU tests vary residency budgets; app/page demand uses the largest active setup. The same linked runtime allowance is required on load. Image measurements are listed below. |
| Capability-dependent records | CLAY v2, CEND and CINP carry window IDs, MIDI endpoints and input IDs in document format 35. Tests vary input labels/positions, MIDI topology, window counts and memory. An adapter regression permutes all nine MIDI routes and verifies physical bindings independently of route numbers. Wire limits are documented in the porting guide. |
| Existing data recovery | `setup upgrade-backup` converts saved compiled graphs, editable exports, historical records, embedded songs and movie references. Tests preserve an edited volume byte and its authoritative graph across historical formats. All 17 actual converted setup/history images pass the native decoder using the exported capabilities. |
| Published API | `doc/porting.md` covers build artifacts, capabilities, services, buffer lifetime, work bounds, input phases, file operations, optional features, memory and formats. A port implements the core interfaces and executable convention without maker registration. |
| Hardware deployment and checks | The existing approved shell-form bootloader command succeeded. The existing uploader flashed the final digest. Restore and independent verification pass; both saved setups load, and the startup counter reports ready. Physical appearance, feel and external MIDI wiring were not observed. |

## Image and memory comparison

The final build has no compiler warnings or errors. The image at
`build/abstract-split/export/firmware.hex` has SHA-256:

`524ebe058c824cb8f8dea7bcfc82beeb33832d9942baea7d559bec921b80d69a`.

| Region | Before | After |
| --- | ---: | ---: |
| Flash code | 204,072 | 206,192 |
| Flash data | 551,028 | 882,068 |
| Flash headers | 8,800 | 8,440 |
| RAM1 variables | 352,960 | 337,952 |
| RAM1 code | 163,752 | 160,104 |
| RAM1 padding | 88 | 3,736 |
| RAM1 local-variable allowance | 7,488 | 22,496 |
| RAM2 variables | 53,600 | 53,600 |
| RAM2 available for allocation | 470,688 | 470,688 |

The larger flash data contains portable CFN2 UI font resources. The renderer
still uses caller-owned work/current/previous pixel buffers; extraction has not
added another framebuffer or a full in-memory movie. The existing target's
buffer dimensions are generated from its capability description. Memory figures
are linker measurements; they do not establish runtime stack high-water usage.

## Recovery artifacts and device results

- Immutable original backup:
  `/Users/tom/code/cntrl/build/abstract-split/before-complete`.
- Converted format-35 backup:
  `build/abstract-split/converted-native` in this implementation worktree.
- Both contain the two saved setups, all 15 history revisions, stored assets,
  song library and saved selection `simple-setup`. Conversion preserves history
  numbers, labels and timestamps; source and converted hashes are recorded.
- The old firmware last reported
  `CNTRL READY 34 23084 a4c4967e starts=2 starts_status=ready`.
- The uploader's staged image was replaced with the final image above. The
  preparation snapshot is at
  `/Users/tom/code/cntrl/build/abstract-split/flash35-final`; the helper checked
  the exact digest before uploading to `14468280-Teensy`.

The uploader sent the image and reset command, then returned status 1 because
the board disappeared while re-enumerating. Subsequent USB responses establish
that the new firmware booted. Before restoration it correctly rejected the old
selected setup. The new image then accepted the converted setup bundle/assets
and alternate history slots. The original history slots remain intact.

`cntrl setup restore` reported all 15 revisions and both setups restored and
read-back verified. `cntrl setup verify` then independently compared setup
binaries, history bodies/metadata, fonts, movies, songs, catalogue and selection;
it reported all equal. This repeated the movie read-back unnecessarily: restore
already verifies those bytes. No additional asset transfer/backup pass was run.

Both setups were selected separately and returned READY:

```text
readability-demo: CNTRL READY 35 3604 5d6e7b5a starts=3 starts_status=ready
simple-setup:    CNTRL READY 35 23998 2a24cf36 starts=3 starts_status=ready
```

The final selection is `simple-setup`. Counter continuity is verified from the
old value 2 to the new value 3; the counter was not reset. These device checks
cover boot, USB protocol, setup loading, persistent storage and converted data.
Physical switch feel, display appearance and external MIDI cable/gear behavior
require separate observation. MIDI ordering, state, recording/playback and input
timing are covered by the host/native tests, not by a physical loopback test.

Local qualification logs live under `build/abstract-split/`. They are build
artifacts rather than committed source. The original baseline log/source paths
are recorded in `abstract-split-baseline.md`.

Final repository checks: 885 Python tests pass, with seven existing color-contrast
warnings. Ruff's selected B/E/F/I rules and Ty pass. The isolated Python suite
passes all 655 tests. The isolated sanitizer suite and relocated-export
compilation/font preparation also pass, as recorded in
`portable-final/result.toml` (3,954 compiled bytes).
