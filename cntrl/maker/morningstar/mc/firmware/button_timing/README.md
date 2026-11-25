# A+F press timing diagnostic

This standalone firmware measures how closely together A and F are pressed.
It does not send MIDI, mount the SD card, or change stored setups. Displays stay
dark. Installing it temporarily replaces the performance firmware.

Build and inspect from the repository root:

```bash
make -C cntrl/maker/morningstar/mc/firmware button-timing
```

The image is `../build/button-timing/button_timing.ino.hex`. Save the matching
performance image before installing the diagnostic. Use TyTools on the
identified MC6 Pro in HalfKay mode to write each image.

Open the pedal's USB serial port at 115200 baud. Press A and F together using
two hands, then release both before each new attempt. No hold is required.
One line appears as soon as both debounced presses have registered:

```text
Trial 1: first=A gap_ms=12
```

`first` is A, F, or `=` when both presses register in the same scan. `gap_ms`
is the elapsed time between recognized presses, not a count of time held.
There is no maximum permitted gap: deliberately staggered presses can be used
to check the measurement. Non-overlapping taps do not count as a pair. After
reporting, the diagnostic requires both switches released before rearming.

Scanning is once per millisecond with the existing 15 ms per-switch debounce.
Resolution is approximately 1 ms, not submillisecond electrical accuracy;
mechanical bounce can affect when each press is recognized. The diagnostic's
light workload also differs from stock firmware's scheduling. Results help
choose a recognition window but do not establish stock's exact latency.

The focused sanitizer-enabled native test covers either press order, simultaneous
recognition, release/rearm behavior, non-overlapping taps and timer wraparound.

## First deployment, 2026-10-25

The focused native test, target build, HEX/ELF byte comparison and stock flash
configuration digest passed. Uploaded image SHA-256:
`86e18227ebd62c0d8805ffbbc1dd19c3ea163f7f7512a00e89477f22806b9517`.
USB serial confirmed the timing diagnostic's startup banner after upload.
Physical timing trials remain user-driven.

Before uploading, `pman` reported 26,866 bytes, CRC32 `36424cc1`. Its downloaded
bytes and the performance HEX were backed up under `../build/button-timing/`.
The normal fixed upload path was restored to the performance HEX on disk after
installing the diagnostic; restoring that firmware to the pedal is a separate
upload after the trials. Serial captures use `../build/button-timing/trials.log`.
