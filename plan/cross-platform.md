# Cross-platform host development and operation

## Objective and scope

Support macOS, Windows and Linux for installation, offline setup compilation,
font/movie preparation, device discovery, USB transfers, native tests and firmware
builds. Preserve the existing wire protocols, file formats and musical behavior.
There is no Windows or Linux development machine available locally, so GitHub
Actions supplies execution evidence for those hosts.

This is a plan, not an implementation. Do not flash a device, launch the upload
service, add dependencies or create workflows as part of writing it.

Additional work beyond the prompt: None.

The application/adapter extraction described in [abstract-split.md](abstract-split.md)
is already implemented; see [its progress record](abstract-split-progress.md).
Hardware independence does not establish host-OS independence. Extend the existing
[porting contract](../doc/porting.md) rather than repeating that extraction or
creating another application.

## Current blockers

Inventory taken from `origin/main` at `fbf4c7b` on 2026-11-09. There are no tracked
GitHub workflow files at this checkpoint.

| Area | Current assumption | Required result |
| --- | --- | --- |
| `cntrl/patch_switcher/transport.py` | Imports `termios`; opens POSIX descriptors and uses `select` on them | The same framed session and commands work through a host-supported serial backend |
| `cntrl/patch_switcher/storage_info.py` | Imports and catches `termios.error` directly | Import and error reporting work without POSIX modules |
| `cntrl/patch_switcher/devices.py` | macOS registry discovery and Linux sysfs discovery exist; Windows is explicitly rejected | Discovery and explicit port selection work on all three systems |
| `cntrl/build.py` and `test/fixtures/build_target/build` | Directly execute an extensionless shell script | One documented build-entry convention works on Windows without requiring a Unix shell |
| `firmware/Makefile` and native compilation inside Python tests | Assume `make`, `c++`, GNU-style flags and Unix executable names | One native test build path supports the selected host compilers and executable suffixes |
| `firmware/test/usb_peer.h` and `document_server.cpp` | Use `unistd.h`, `poll.h` and Unix descriptor I/O | The same production protocol is exercised by host-native test peers |
| Transport, song, font, animation and disk-speed tests | Import `pty` at collection time and communicate through Unix pseudo-terminals | Shared behavior tests collect and run on Windows; genuine POSIX tests remain explicitly identified |
| `scripts/qualify_portable.py` | Invokes `make`; finds a sibling `cntrl` executable without a Windows suffix | Portable qualification runs using the selected interpreter and common native test runner |
| `pyproject.toml` and `uv.lock` | Resolve editable `reccy` from `../reccy` | A fresh runner can reproduce the complete environment without the developer's filesystem |
| Publication and path handling | Hard-link publication, open-file replacement, symlinks and colon-separated movie strings | Preserve file safety and correctly handle Windows paths and filesystem behavior |
| Adapter build, upload and qualification tools | Host-specific executables, shell tools, cache paths and physical-device assumptions | Keep host bindings in their owning adapter and qualify each supported operation separately |

Do not rewrite archived experiments or historical deployment transcripts merely
because they mention macOS. Audit maintained commands and test collection first.
Keep device-specific implementation notes, uploader details and qualification
results under the existing adapter directory.

## Support and evidence

Start with native Windows x64, Linux x64 on a supported Ubuntu LTS runner, and
the existing macOS development architecture. Select explicit, currently available
runner labels when implementing the workflow, recording each runner's actual OS,
architecture and compiler. Do not equate a hosted Windows Server run with physical
Windows desktop USB qualification, or one Linux distribution with every Linux
distribution. WSL is not Windows evidence.

Use the repository's Python version first. Check dependency installation on that
version before expanding a Python-version matrix. Additional CPU architectures
and Python versions are separate coverage decisions, not implicit promises.

Distinguish three outcomes in reports:

1. **Automated host support:** imports, installed CLI, compilation, assets,
   persistence, protocol peers and native suites pass on the named runner.
2. **Firmware build support:** that host produces a validated device image and
   relocatable target export using the documented toolchain.
3. **Physical qualification:** real discovery, serial access, permissions,
   reconnects, reboot/upload and MIDI behavior have been observed on that OS.

Hosted runners establish the first two outcomes. They do not contain the pedal
and cannot establish the third. Record physical Windows/Linux checks as unverified
until a contributor with the hardware performs them.

## CI policy: explicit checkpoints, not every push

Create a cross-platform workflow triggered only by `workflow_dispatch`. Do not
add `push`, `pull_request`, `pull_request_target`, schedules or an indirect
per-commit trigger. Pushing intermediate commits must not launch these tests.
Do not use commit-message skip directives as the primary control.

The workflow must first exist on the default branch to become manually
dispatchable. Publish that bootstrap change without automatic triggers. Then
dispatch the selected branch at an implementation checkpoint through the Actions
UI or `gh workflow run cross-platform.yml --ref <branch>`. Record the resolved
commit SHA for the run, not just the moving branch name. This follows GitHub's
[manual workflow contract](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow).

Use two manual profiles: `core` for the portable package/native suites, and `full`
for the complete host suite plus adapter build qualification. The full profile
requires an explicitly selected target directory at the existing build boundary.
Neither profile uploads firmware. Default the final acceptance run to all three
OS jobs, rather than silently treating a Linux-only run as cross-platform proof.

At each useful checkpoint:

1. Run relevant checks locally on macOS and push a coherent set of commits.
2. Dispatch one selected matrix run, inspect every OS result and retain failures.
3. Fix the reported failures and dispatch the affected jobs again when ready.
4. Run the complete three-OS profile at final acceptance and before claiming
   support for a release.

Use `fail-fast: false` so one OS failure does not hide the other results. Bound
jobs and subprocesses, and cancel superseded runs for the same branch/profile.
Cache dependencies and SDK downloads by OS, architecture, Python/lockfile and
toolchain/patch identities. Never share compiled executables between OS caches.
Include a cold-cache final acceptance run, dispatched explicitly rather than scheduled.

Upload JUnit results, logs, compiler versions, fixture differences and build/export
artifacts even on failure. Store provenance in TOML. Pin action revisions and
dependency checkout revisions; retain enough information to reproduce the tested
environment. Keep orchestration in YAML and substantial build/test logic in
tracked repository files, not embedded scripts in workflow steps.

GitHub's [runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
is authoritative for available images. No workflow is dispatched for this plan-only
change.

## Implementation sequence

### 1. Establish reproducible setup and a manual CI baseline

Add the dispatch-only workflow first, with honest baseline results for each OS.
An import/collection failure is a failure to fix, not a reason to report an empty
Windows suite as passing. Record current collection counts and any independently
reproduced existing failures; do not carry old skips forward without rechecking.

Initially preserve the declared sibling dependency layout: check out cntrl and
the compatible `reccy` repository side by side, pin the dependency checkout to
an explicit commit, and run frozen dependency installation from cntrl. Confirm
repository access is available before depending on that checkout in a workflow.
Do not silently use an unrelated published version or the dependency's moving
main branch. No cross-repository changes are authorized by this plan.

Check wheel/native-library availability for Pillow, FreeType bindings, HarfBuzz
bindings and the remaining locked dependencies. Document any required native
libraries and provision them explicitly in the relevant job. Do not assume
Homebrew paths or libraries already present on a developer machine. Preserve
dependency versions unless an observed platform failure requires a separate,
reviewed dependency commit.

Gate: all three runners can reproduce installation and produce an import and
test-collection report, including useful diagnostics for remaining blockers.

### 2. Make serial access and discovery portable

Keep framing, acknowledgement, block-size negotiation, session identity, restart
handling, cancellation and existing retry behavior in `SerialConnection`. Replace
only the OS byte-port operations. Windows `select` cannot wait on ordinary serial
file descriptors, as documented by [Python](https://docs.python.org/3/library/select.html).

Proposed implementation: use pySerial for serial access and enumeration on all
three hosts, replacing the current direct OS implementations rather than adding
a Windows path alongside three competing discovery mechanisms. Its maintained
[port metadata API](https://pyserial.readthedocs.io/en/latest/tools.html) supports
Windows, Linux and macOS. This is a proposed dependency addition requiring approval
before implementation and a separate dependency commit; no dependency is added
by this document.

Preserve explicit `--port`, stable device identity where metadata provides it,
deterministic ordering, and the existing interactive/noninteractive multiple-device
behavior. Do not assume identical product/manufacturer strings on every OS or
identify a pedal by its current COM number alone. Ports are opaque endpoint names:
`COM10` must not be resolved as a file inside the working directory.

Remove direct POSIX imports from normal CLI import paths, including storage
reporting. Preserve existing device line-coding requests and connection ownership;
do not invent new reconnect behavior or change the wire protocol during the port.

Gate: all CLI modules import and display help on each OS without a connected
device. Byte-port doubles cover partial reads/writes, deadlines, errors, disconnect
and cancellation through the existing production protocol.

### 3. Port native builds and transport tests

Introduce one tracked Python native-test build driver under `scripts/`, used by
the Python test fixtures and native suite. Consolidate the repeated compiler
invocations into that driver; retain `make` only as a thin convenience wrapper
if needed. Do not introduce another build-system dependency unless this proves
insufficient.

Use C++17 with the selected host compiler: Clang on macOS, GCC on Linux and MSVC
on Windows initially. Select appropriate compiler arguments, runtime libraries,
output suffixes and diagnostic handling in one place. Preserve warnings and
assertions. Keep ASan/UBSan coverage on supported macOS/Linux configurations;
enable Windows sanitizer coverage only where the chosen toolchain supports it.
An unsupported sanitizer flag must not disable the ordinary Windows native suite.

Replace the shared protocol tests' dependence on PTYs with a binary subprocess
transport and a small injectable byte-I/O boundary. Keep the production framing
and C++ receivers under test. Supply only the necessary host bindings for peer
I/O, including Windows binary mode and bounded polling. Do not duplicate protocol
logic in a Python-only fake. Do not introduce TCP or async infrastructure merely
to make test peers communicate.

Keep real POSIX tty configuration tests on POSIX hosts, with platform imports
inside those tests so Windows collection succeeds. Test Windows serial settings
through the selected backend's controllable interface. Neither pipe peers nor
backend doubles prove physical USB-driver compatibility.

Every peer must have bounded startup/exchange/shutdown, captured stderr and
reliable cleanup. Preserve fragmented I/O, backpressure, lost-session, transfer,
backup/history and malformed-input coverage. Extend `qualify_portable.py` to use
the same driver and interpreter-based CLI invocation without requiring Unix
executable naming or a particular virtualenv scripts directory.

Gate: shared Python/C++ protocol and application tests run on all three hosts;
the Windows suite is not made green by skipping transport or native tests.

### 4. Resolve paths, file publication and installed-package behavior

Exercise absolute and relative paths, drive letters, UNC paths, spaces, non-ASCII
names, CRLF text, case-insensitive filesystems, reserved Windows filenames and
temporary-file ownership. Close files before operations Windows forbids on open
handles. Preserve overwrite protection, synced writes and final-content verification
in setup, asset and backup publication; test the existing hard-link approach on
supported filesystems before deciding it needs replacement.

Resolve a concrete grammar conflict: movie strings currently split on `:`, so
`C:\media\movie.gif` cannot be treated as an ordinary colon-separated list.
For the first supported Windows version, use the existing list form for
drive-qualified movie filenames and document it, while resolving each entry as
one filename. Do not guess how to split ambiguous drive-qualified string lists.
If accepting drive-qualified strings is required, agree an unambiguous grammar
before changing the parser and all affected round-trip tests.

Add a clean installed-wheel smoke check outside the source checkout. Include
the required packaged fonts/resources, use a compatible built dependency wheel,
and verify help plus an offline compile/asset round trip. Firmware building still
uses the explicit source-root contract; do not assume an installed wheel contains
the entire development checkout.

Gate: file and asset outputs are portable, path round trips succeed, and installed
commands work without importing an unrelated editable checkout.

### 5. Port the build-entry contract and adapter tooling

Replace the extensionless shell entry convention with one explicit interpreter-run
Python entry file per target, launched using `sys.executable`. Update the generic
build wrapper, fixture target, adapter entry and `doc/porting.md` together. Replace
the old launcher contract completely; do not add filename probing or several
fallback launcher names. This is an architectural contract change to explain
and approve before implementation.

Keep compiler/SDK selection, source patches, linking and uploader bindings local
to the adapter. Select downloads by supported host OS and CPU architecture and
verify each archive's digest. Arduino CLI distributes host-specific downloads;
check its [installation documentation](https://docs.arduino.cc/arduino-cli/installation/)
and the device SDK's actual host packages before promising a build lane.

Replace shell-dependent checksum, archive, copy and validation operations with
tracked host-portable code where necessary. Preserve SDK patch verification and
image/header checks. Do not require Git Bash, WSL, Homebrew or an application
bundle copied from macOS to make Windows pass. Use existing Python facilities
before adding tools; keep unchanged tool versions unless a demonstrated host
support gap requires approval to change them.

For the upload service, preserve the current image/device restrictions and request
checks while removing fixed local-device and application-installation paths.
Proposed local IPC is the standard library's byte-message connection API, with
named pipes on Windows and Unix sockets on POSIX. Keep one request handler and
CLI, use bounded byte messages rather than pickle, and preserve local access
restrictions. This changes the service transport and needs its own review before
implementation; it must not silently become a network listener.

Gate: each OS builds and inspects the image and produces a relocatable export;
an SDK-free copy of the export compiles setups/assets. Uploader invocation and
device selection have host-native automated tests. Actual uploads remain a
separate physical qualification activity. Adapter-specific instructions and
unresolved host SDK/uploader limitations stay in the adapter's documentation.

### 6. Final qualification and documentation

Run the full manually dispatched matrix against one recorded final revision.
Build images on each host, validate their structure and record size/memory
headroom. Compare deterministic document, bundle, font and movie fixtures across
hosts. Compare firmware bytes only under matched compiler/SDK inputs with known
timestamps/path metadata accounted for; explain differences rather than relaxing
functional fixtures or promising identical binaries without evidence.

Document installation, compiler/native-library prerequisites, available commands,
permissions and limitations for each OS. Explain Linux USB device permissions
without making routine commands run as root. Document Windows device naming and
driver requirements separately from automated protocol evidence. Keep the existing
macOS development path working throughout.

Check the source tree with the adapter and SDK unavailable, and test a relocated
export from a path containing spaces. Review every skipped test and every
platform-conditioned branch. No platform-wide `continue-on-error`, blanket
Windows skips or hidden developer-machine resources count as acceptance.

## Completion criteria

- Explicit workflow dispatch is the only test trigger introduced by this work;
  ordinary pushes and PR updates do not start the matrix.
- The recorded final revision passes all shared tests, installed-package checks
  and native suites on the three selected hosts. Any OS-specific tests and
  sanitizer gaps are listed with their actual coverage replacements.
- Serial/discovery code has one maintained backend and unchanged cntrl protocol
  behavior; Windows import succeeds without POSIX modules.
- Each host can produce validated firmware and a usable relocated export, or a
  specific unresolved toolchain blocker prevents declaring that part complete.
- All required development, test and conversion code is tracked. Disposable
  generated outputs, caches and downloaded SDKs are identified as such.
- Device operation on Windows/Linux is explicitly unverified until physical
  qualification supplies evidence. Passing GitHub jobs alone must not be described
  as a successful real-device upload, reboot or MIDI test.
