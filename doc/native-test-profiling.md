# Profiling native tests

Show the slowest pytest phases with:

```bash
uv run --no-sync pytest -q --durations=25
```

`test_switcher.cpp` also accepts `--profile` after its two fixture arguments.
This prints elapsed wall time for its major sections without changing the
assertions, failure points, compiler options or sanitizer settings. For example,
from the checkout root:

```bash
uv run --no-sync pytest -q test/test_shared_native.py -k test_switcher --basetemp build/switcher-profile
build/switcher-profile/native-programs0/test/test_switcher firmware/test/show.golden firmware/test/display.golden --profile
```

On Windows, append `.exe` to the executable name.

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
That loop tries every byte boundary in an upload, starting from a copied storage
image, attempting replacement, then rebooting again to verify the previous setup
remains readable. This is CPU work against in-memory test storage.

A macOS `sample` capture during that loop showed upload/history processing,
`Revisions::update`, slot checksum/validation during reboot, and byte-vector
insertion/copying/destruction in `TestStorage`. The sample covered only part of
the run; the section timings above cover the complete run. No recovery cases or
history iterations were removed, and no optimization was applied to this test.

For a statistical profile, run the test and, while it is running, use
`sample <pid> 12 -file <output-path>` on macOS. The resulting text is generated
profiling data; it does not need to be committed.
