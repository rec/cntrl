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

On macOS, `python -m cntrl.maker.morningstar.mc.upload_helper serve` defaults to the installed Morningstar
updater. Set `CNTRL_TYCMD` to override that path; other hosts currently require
an explicit override. The helper snapshots the requested worktree image and
preserves the sole allowed pedal serial/tag. The bootloader command discovers
that serial number and sends the same 134-baud line-coding request through
pySerial, with no serial payload.

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
