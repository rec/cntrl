# Profiling native tests

Show the slowest pytest phases with:

```bash
uv run --no-sync pytest -q --durations=25
```

`test_chooser.cpp` also accepts `--profile` after its two fixture arguments.
This prints elapsed wall time for its major sections without changing the
assertions, failure points, compiler options or sanitizer settings. For example,
from the checkout root:

```bash
uv run --no-sync pytest -q test/test_shared_native.py -k test_chooser --basetemp build/chooser-profile
build/chooser-profile/native-programs0/test/test_chooser firmware/test/show.golden firmware/test/display.golden --profile
```

On Windows, append `.exe` to the executable name.

Native test builds now default to `-O1`, with assertions explicitly enabled and
the existing address/undefined-sanitizer flags retained on Clang/GCC. Historical
unoptimized results below used the earlier default. MSVC uses `/O1` and keeps
assertions enabled; its existing sanitizer coverage differs from Clang/GCC.

## Routine and exhaustive interrupted-write checks

The normal suite checks structural boundaries. An explicitly selected
qualification uses the same recovery assertions at every byte boundary:

```bash
uv run --no-sync pytest -q -s test/qualification_interrupted_writes.py
```

The qualification filename intentionally does not match pytest's default test
discovery pattern. Selecting that file explicitly collects its test. Both modes
use the same cached native executable, assertions and sanitizer settings.
The native executable also accepts `--exhaustive` alongside `--profile`.

Cases are derived from each actual storage write during the successful upload:

- Every partial header: the first write after opening an empty record.
- Every byte cut in short writes, including the four-byte commit marker.
- For each payload write: no bytes written, one byte written, the midpoint,
  and the last incomplete prefix. The next write's zero-byte cut checks the
  preceding write's completed state, including the header/payload transition.
- Complete upload and reboot checks verify the fully committed replacement.

Both modes also inject real upload failures at the start, midpoint and final
byte of each write call. Every recovery checks the complete original setup
byte-for-byte and retains the original display assertion.

With the current fixture, the routine mode checks 272 unique byte cuts and the
exhaustive mode checks all 9,728. On macOS ARM64 on 2026-11-16, with the same
`-O1` executable, the interrupted-write section took 0.220 seconds routinely
and 4.082 seconds exhaustively. The counts follow the actual write sequence;
they are not fixed limits in the test.

## Initial profile

Measured on macOS ARM64 with the normal unoptimized, address/undefined-sanitized
test executable, on 2026-11-15:

| Section | Seconds |
| --- | ---: |
| Interrupted inactive-slot writes at every byte boundary | 16.572 |
| Revision history | 2.051 |
| Patch UI, editing and selection power loss | 1.186 |
| Catalogue jobs | 0.544 |
| Decoder, gestures and undo | 0.045 |
| Setup options and display editing | 0.019 |
| Independent pages | 0.006 |
| Final recovery and protocol checks | 0.006 |
| Persistent upload and reboot | 0.004 |

The total was approximately 20.43 seconds; interrupted writes accounted for 81%.
The original loop replayed the complete upload for each failure budget from
zero through `payload size + 99`, then rebooted to verify the previous setup.
Those budgets also paid for archiving the old setup, so this range covered the
archive and only the start of the new slot's header. This was CPU
work against in-memory test storage.

A macOS `sample` capture during that loop showed upload/history processing,
`Revisions::update`, slot checksum/validation during reboot, and byte-vector
insertion/copying/destruction in `TestStorage`. The sample covered only part of
the run; the section timings above cover the complete run before optimization.

For a statistical profile, run the test and, while it is running, use
`sample <pid> 12 -file <output-path>` on macOS. The resulting text is generated
profiling data; it does not need to be committed.

## Reusing successful write prefixes

The exhaustive interrupted-upload test runs a successful production upload once.
At each storage write, it copies the current storage image, uses the same partial
write implementation with every byte limit from zero through `write size - 1`,
and boots the production recovery code on each copy. The complete original setup
must match byte-for-byte, and the original display assertion is retained.

This checks all 9,728 byte cuts in the complete archive-and-replacement write
sequence, including the setup payload and commit marker. The previous loop
checked 4,868 cuts. Complete-commit success and reboot checks are also retained.

The upload's error-handling paths still run with real injected write failures
at the start, middle and final byte of every write call. Within a given call,
every incomplete `TestStorage::write` returns false to the same caller branch;
each possible partial file image is independently covered by the exhaustive
recovery checks. Repeating all earlier successful upload/hash work for every
cut is unnecessary. The test storage's `finish()` does not modify file bytes,
so the partial image also represents the disk state after failure handling.

On the same checkout and with the same compiler flags and sanitizers, section
time decreased from 16.1595 seconds to 8.96394 seconds, about 45%, while extending
the covered range. No production code, compiler optimization settings or history
iteration counts changed.

## Optimized compilation

A subsequent comparison ran the same test twice at `-O0` and twice at `-O1`,
reversing execution order for the second pair. Every run passed and reported
9,728 recovered byte boundaries, with assertions and both sanitizers enabled.

| Measurement (mean of two runs) | `-O0` | `-O1` |
| --- | ---: | ---: |
| Interrupted-write section | 8.890 s | 4.554 s |
| Complete executable, wall time | 12.930 s | 6.875 s |

The compiler helper now enables this optimization for native tests by default.

## Parallel test execution

`pytest-xdist` is a development dependency. Run four worker processes with
module/class-based scheduling, or disable workers for a serial comparison:

```bash
uv run --no-sync pytest -q -n 4 --dist=loadscope --durations=10
uv run --no-sync pytest -q -n 0 --durations=10
```

Keep the checkout and compiler settings fixed between timed runs. Complete a
qualification run first so native and emulator caches are populated. A Git
commit changes the emulator build key and may require rebuilding those binaries.

Measured on the same macOS ARM64 checkout on 2026-11-15, with `pytest-xdist 3.8.0`,
`-O1`, assertions and the existing sanitizer settings:

| Warm-cache run | Elapsed seconds | Result |
| --- | ---: | --- |
| Serial, `-n 0` | 142.00 | 1,854 passed, 1 skipped |
| Four workers, `-n 4 --dist=loadscope` | 42.91 | 1,854 passed, 1 skipped |

Parallel execution was 3.31 times as fast, saving 99.09 seconds (69.8%). Both runs
reported the existing display-contrast warning. The preceding four-worker
qualification also passed, taking 72.64 seconds including some uncached emulator
builds. No parallel-specific fixes or exclusions were necessary.
