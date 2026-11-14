# Cross-platform build and upload tools

Build a validated image and portable target/font export:

```sh
uv run --frozen cntrl build --target-dir cntrl/maker/morningstar/mc --output "build/device export"
```

The entry point uses the selected interpreter. It provisions Arduino CLI 1.5.1,
Teensy core 1.62.0 and ARM compiler 15.2.1 into `firmware/.tools`. SDK hosts are
macOS arm64/x64, Linux x64 and Windows x64. CLI archive digests are pinned;
Arduino verifies core/tool archive checksums. Git applies and verifies tracked
SDK patches. Host/compiler/patch hashes go into `.tools/provenance.toml`.
Git must be installed; Git Bash and WSL are unnecessary.

The stock v3.14.0 image is downloaded from its exact upstream release asset when
absent and checked against SHA-256
`228d73e768fad383bc205c8fc1ef52f3a49de08c754aca11d4a069ca1aab9fc8`.
The same digest, original patch bytes, boot headers, flash configuration, ELF/HEX
comparison and complete-image readback checks remain mandatory. Build outputs
and downloaded SDKs are disposable generated data. Builds do not flash a pedal.

The maintained native tests run with:

```sh
uv run --frozen python -m cntrl.maker.morningstar.mc.native_tests
```

The Makefile wraps portable Python commands for POSIX convenience. Old diagnostic
transcripts describe historical commands, not the current build contract.
Physical observations apply only to the recorded host/device combination.

Dispatch complete host and firmware qualification explicitly:

```sh
gh workflow run cross-platform.yml --ref main -f profile=full -f target_dir=cntrl/maker/morningstar/mc -f cold_cache=true
```

The reports include native/Python logs, JUnit output, installed-wheel artifacts,
the target export and its `memory.txt` section-size report. The installed-wheel
check retains deterministic `show.bin`, `setups.bin`, `font.cfont` and `movie.cnsa`
files for comparison between hosts. The export is also tested after relocation
with the adapter and SDK unavailable.

The source fonts and committed bitmap font files remain unchanged. FreeType's
monochrome raster originally produced two extra pixels on Windows, in Rubik
Distressed's `5` and `Q` at size 48. The provisioned library normalizes its internal
coordinate width and profile layout to match the 64-bit macOS/Linux raster. Run
`python -m cntrl.maker.morningstar.mc.font_diagnostics` to compare it with Pillow's
bundled library. Font generation now uses the provisioned library for both legacy
bitmap faces and outline assets.

The [final cold-cache full matrix](https://github.com/rec/cntrl/actions/runs/42643823346)
passed on 2026-11-09 at `b862417590ae25df4ee036e1a90109b76d126a64`:

| Host | Native compiler | Python | Full Python suite | Adapter-free Python suite |
| --- | --- | --- | --- | --- |
| Windows Server 2025 x64, build 26100 | MSVC tools 14.51.36231 | 3.13.17 | 1,045 passed, one POSIX tty skip | 799 passed |
| Ubuntu 24.04 x64, Linux 7.0.0-1017-azure | GCC 13.3.0 | 3.13.18 | 1,046 passed | 799 passed |
| macOS 15.7.9 x64 | Apple Clang 17.0.0 | 3.13.15 | 1,046 passed | 799 passed |

Each host also passed all 32 common native programs, all seven adapter native
programs, lint/type checks, validated firmware generation, installed-wheel
compilation and relocated export compilation. Common native tests ran again
from the relocated, adapter-free source copy under a path containing spaces.
All hosts used the pinned reccy revision and FreeType 2.14.3 recipe described in
the [host setup guide](../../../../doc/cross-platform.md).
Provenance records raw checkout hashes: the Windows lockfile hash reflects CRLF
line endings, while POSIX uses LF. Frozen installation used the same tracked lockfile.

Local ARM64 qualification on 2026-11-09 used macOS 14.5, Python 3.13.12 and Apple
Clang 16.0.0 at `8c4df09a657bcaa3727f795d425c68012a49e57a`. The full profile
passed 1,046 Python tests, all 32 common native programs, all seven adapter native
programs, the firmware build and the installed-wheel checks. The relocated,
adapter-free copy passed 799 Python tests and the common native suite. The later
MSVC signedness correction in `test_midi_streams.cpp` passed its 12 focused tests
locally as well.

The installed-wheel artifacts matched byte for byte across all three hosted
systems and the local ARM64 run, with these SHA-256 digests:

| Artifact | SHA-256 |
| --- | --- |
| `show.bin` | `de2711e8e8e0e9cfb7b716103f41903c7f9b8d5300ee85f3f4090e332aedb664` |
| `setups.bin` | `82ee2f83e1a3b6dfaf5e7982b4e2d80e951c1408a9853a6eaece7350fd3a2999` |
| `font.cfont` | `d72288078602ba016a3f22d543d9de90516ed3e81a11e814e6b5e3d227b3b374` |
| `movie.cnsa` | `8b0bd9c14c67a914c7db4a87bd5e18686160257b9e43df5fddd5af5d606638e1` |

All four hosts produced the same static firmware section sizes. Link accounting
for this image:

| Region | Bytes |
| --- | ---: |
| ITCM code | 190,528 |
| RAM1 initialized data | 31,424 |
| RAM1 zero-initialized data | 276,512 |
| RAM1 gap below the initial stack pointer | 19,744 |
| RAM2 static DMA storage | 118,688 |
| RAM2 heap region after static DMA storage | 405,600 |

The custom flash region is 6,912 KiB. Its initialized image spans 1,153,024 bytes,
leaving 5,924,864 bytes in that region. RAM1's gap follows the linker's ITCM/DTCM
bank split. RAM2's heap figure is `_heap_end - _heap_start`, also exported as
`cntrl_memory_budget`. Dynamic runtime, workspace and safety-margin costs still
need to be accounted for; these figures are not stack or heap high-water marks.

Firmware images contain absolute source paths in assertion strings, including
`setup_table.h`; those paths differ between hosts. Debug paths also differ.
Firmware byte-for-byte reproducibility is therefore not established by these
builds. The qualification checks validate image structure and readback and
compare static section layouts separately from the deterministic asset fixtures.

On macOS and Windows, first run
`cntrl upload-helper --target-dir cntrl/maker/morningstar/mc provision` to install
the pinned official TyTools 0.9.9 executable in the platform cache. The `serve`
command defaults to that checksum-verified executable, leaving the Morningstar
updater application untouched. Set `CNTRL_TYCMD` to select an explicitly managed
executable instead; Linux requires that override because the pinned release has
no Linux archive. See [upload-helper.md](firmware/upload-helper.md) for startup
instructions. The helper snapshots the requested worktree image and preserves
the sole allowed pedal serial/tag. The bootloader
command discovers that serial number and sends the same 134-baud line-coding
request through pySerial, with no serial payload.

The helper uses authenticated local byte messages over named pipes on Windows
and Unix sockets on POSIX. It uses neither TCP nor pickle. Authentication keys
and image snapshots live in the user's platform cache directory (`LOCALAPPDATA`,
`~/Library/Caches`, or `XDG_CACHE_HOME`/`~/.cache`). Unix cache/key permissions
are restricted to the user. Named pipes use local access control plus the cache
authentication key. Requests remain bounded and accept only an image path and
digest; board/image checks run before uploader invocation. Restart an old helper
after updating its transport code.

Windows/Linux physical upload, serial permissions, reboot and MIDI qualification
remain unverified. Builds and pipe peers do not supply that evidence.
