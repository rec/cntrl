# Host development and qualification

Use Python 3.13 and `uv`. Check out this repository beside the compatible
`reccy` checkout and run `uv sync --frozen --group dev`. The workflow pins that
dependency to `5d7e42b47ed8b14fba185a6be1581a013ccd9a67`.

Provision FreeType 2.14.3 after syncing dependencies so font pixels match the
committed assets. This uses CMake and the host compiler, downloads digest-checked
FreeType/HarfBuzz sources, and installs the library into the current environment
without modifying uv's cached wheel. It retains freetype-py 2.5.1.
Legacy bitmap fonts also use this library. A tracked source patch fixes the
monochrome raster's internal coordinates and profile pool to 64 bits on every
host, preserving the committed pixels on Windows without changing FreeType's
public ABI. Pillow remains available for image and animation conversion.

```sh
uv run --frozen python -m cntrl.fonts.provision
```

Generated sources/build outputs live in `build/freetype`; the tracked provisioning
module contains the complete build recipe. Re-run it after replacing the environment.

The qualification lanes are macOS 15, Ubuntu 24.04 x64 and Windows Server 2025
x64. Windows requires Visual Studio C++ build tools with the developer command
environment active. All hosts require CMake and Git. macOS requires Xcode command line tools; Linux requires
GCC/G++. No Unix shell or make is required on Windows.

Run common checks from the repository root:

```sh
uv run --frozen python -m scripts.ci --profile core
```

The full profile additionally requires `--target-dir path/to/implementation`.
It runs the complete Python suite, native suite, installed-wheel smoke check,
the selected implementation's build and relocated export qualification.
Reports, logs, JUnit output, wheels and fixture artifacts go into `reports/`.
The export check copies shared sources without implementation directories or SDKs
and recompiles from that copy. See [the build contract](porting.md).

The workflow has only `workflow_dispatch`. Pushes and pull requests do not run
tests. Select `core` or `full` and use `cold_cache=true` for clean-cache acceptance.
Each run records repository/dependency revisions, OS, architecture, compiler and
lockfile hash in TOML. Matrix jobs do not cancel one another on failure. A newer
dispatch for the same ref and profile supersedes an older run. Qualification
requires a passing complete matrix.

Serial access and enumeration use pySerial on all hosts. Endpoint names remain
opaque: use `--port COM10` on Windows or the appropriate `/dev/...` endpoint on
POSIX hosts. Linux users need read/write permission on the device, commonly
through their distribution's serial-device group or a device-specific udev rule.
Routine commands should run as the user. Windows needs the USB serial driver
installed and the port free of other applications.

Movie strings split on colons. Use the list form for Windows drive-qualified
paths, for example `splash_movie = ['C:\media\startup.gif']` in TOML. UNC paths
also work as list entries. Relative entries resolve from the setup file. Empty
strings and lists disable that category. Movies stay on SD; their pixel payloads
do not consume RAM2.

Setup names exclude Windows device names and unsafe filename characters. Use
user-writable directories for exports and backups. Publication closes temporary
handles before replacement and retains overwrite/readback checks.

Native tests use Clang/GCC with ASan and UBSan on POSIX, and MSVC C++17 with
warnings treated as errors on Windows. Windows has ordinary native coverage
without sanitizer flags. Shared transfer tests use binary subprocess peers with
the production C++ receivers. A POSIX tty test checks actual 134-baud line coding;
a backend test checks the request and device identity on every OS.

Windows native test executables declare UTF-8 process encoding and long-path
support. Enable Win32 long paths on development hosts to exercise the same
255-character filename tests as the hosted runner. Filesystem bindings explicitly
convert protocol names to and from UTF-8.

Hosted CI cannot establish physical USB discovery, reconnects, reboot/upload or
MIDI behavior. These remain unverified on Windows and Linux until tested with
the device. SDK and uploader instructions belong with the implementation.
