# MC6 Pro splash animation

The patch-switcher target integrates this player and upload protocol. Set
`splash = true` in the show (`"splash": true` in JSON) to play the installed
animation on boot or show activation. The default is false. Upload the prepared
asset before uploading the show that enables it. A missing or invalid asset
falls through to the normal interface.

Any button dismisses playback. The complete dismissing gesture, including
release and other held buttons, is consumed without sending a patch. Dismissal
fades the animation to black over 1,000 ms, invalidates all three renderer caches
and repaints the normal interface while dark. After every screen is ready, it
fades back to the show's brightness over 1,000 ms. The total is two seconds of
fading plus the brief black repaint interval. Animation continues during fade-out;
USB, button scanning and MIDI servicing continue throughout. After releasing the
dismissing gesture, subsequent presses act normally even before the fade ends.
Uploads cancel the transition and restore the configured brightness.

## Idle screensaver

Set `screensaver_timeout = 3` in a TOML show, or `"screensaver_timeout": 3` in
JSON, for a three-second physical-input idle timeout. The default is 0 (disabled).
It uses the installed animation and does not require startup `splash` to be true.
The pman test show enables both. Presses and releases reset the timer, and it
never starts while a button remains held. Missing assets leave the normal UI up.

Entry fades the UI out over four seconds, paints a complete first animation
frame while dark, then fades the animation in over four seconds. Pressing any
button, even during entry, switches to the fast wake transition: 200 ms out,
complete UI repaint while dark, 200 ms in. Unlike startup dismissal, this press
also performs its normal musical action immediately. Both transitions include a
brief black repaint interval beyond the two ramp durations. Neither blocks USB,
button scanning or MIDI queues. Incoming MIDI and USB status queries do not reset
the physical-input timer. Show/asset transfers temporarily inhibit idle entry.

The setting is encoded in show format 5. Recompile older shows before upload;
there is no implicit conversion of saved binary shows.

The screensaver build was inspected and uploaded on 2026-10-23. Its HEX SHA-256
is `0aa952ade3ee6065339e7629c19ff5ee9a0a303031d2736101b39943d88b40cf`.
The recompiled three-second pman show verified as
`CNTRL READY 5 22246 bf83dc72`. All 151 Python tests, native controller,
display/renderer and splash runtime tests passed. Runtime coverage includes
held-button suppression, release activity, full slow entry, dark first-frame
painting, interruption during entry, immediate MIDI on wake, fast wake ramps,
timer wraparound, missing assets, disabled idle and transfer inhibition. Visual
entry and wake-up behaviour still require the user's check.

The fade build was inspected and uploaded on 2026-10-23. HEX/ELF comparison and
stock flash-configuration checks passed; the HEX SHA-256 is
`490b742af1b173eee4e9908bdd52db99d8132960be08edf006524537329ed715`.
USB confirmed the saved show as `CNTRL READY 4 22242 9afa8de1` after boot.
Native controller, display, renderer and splash tests passed, including fade
timing, millisecond wraparound, dark repaint gating and musical input during
the fade. Visual smoothness still requires the user's check.

The source GIF is preserved in `assets/spiral.gif`: 16 frames, 100 ms per frame,
1,600 ms per loop. The Mac scales it to cover a 192x154 area (120% of the
160x128 display, rounded to whole pixels), then crops centrally to 160x128, with
no margins or letterboxing. This preserves proportions while clipping the
edges of the enlarged image. The centre screen is horizontally mirrored;
both outer screens use the unmirrored image. Padding and show text colours do
not apply to this full-bleed splash.

## Prepare on the Mac

FFmpeg and ffprobe must already be available on PATH. No Python dependency was
added. GIF decoding/compositing, resizing, cropping, mirroring, colour conversion
and frame-timing conversion all happen here, not on the pedal.

From the repository root:

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.splash.cli prepare \
  cntrl/maker/morningstar/mc/splash/assets/spiral.gif \
  --output cntrl/maker/morningstar/mc/splash/build/spiral.cnsa
```

The supplied animation produces 1,310,800 bytes. Generated assets and binaries
stay under ignored `build/`; the source GIF is committed. There is no on-pedal
GIF decoder, decompression, RGB conversion, resizing or image mirroring. Rows
are stored in the exact RGB565 byte order needed by the display bus.

## Components

- `animation.h`: bounded row-streaming player. It loops while active, honours
  frame durations with a wrap-safe millisecond clock, and returns `dismissed`
  immediately when any button is pressed. That press must not reach the normal
  patch controller. One poll reads/transmits at most one 320-byte row.
- `receiver.h`: independent upload receiver and active-asset `Source`. It uses
  two slots and never overwrites the active slot. Chunks are acknowledged before
  the host sends more. It validates the asset geometry, durations and transfer
  CRC, flushes a final commit marker, reads back and verifies the stored asset,
  then activates it. Boot validation and commit readback are incremental, with
  at most 512 bytes per CRC read. Individual SD calls may still block.
- `board.h`: SD/SdFat slot backend, with persistent reader handles, and an output
  adapter that reuses the existing grouped SPI display driver. Reserved files
  are `/cntrl-splash-0.bin` and `/cntrl-splash-1.bin`, separate from show files.
  The application still initializes SD and sets `SdSlots.ready`.
- `cli.py`: Pydantic/Tyro preparation and chunked serial-upload commands. The
  uploader reuses the existing serial connection class without modifying it.

The adapter treats panel indices 0/2 as outer and 1 as centre. It broadcasts
identical rows to both outer panels simultaneously. All panels are deselected
before button scanning. `session.h` handles playback lifetime and dismissal;
`router.h` routes complete commands and exclusive binary payloads to the asset
or show receiver. Upload BEGIN stops playback before storage changes. Status
queries leave playback running. USB, button scanning and MIDI queue servicing
continue while the splash owns the screens.

At 8 MHz, broadcasting the two outer panels and separately writing the centre
requires at least 81.92 ms of pixel wire time per frame, before commands and SD
access. The original 10 fps remains a physical qualification target. If a transfer exceeds
a frame's duration, the player finishes that frame and starts the next without
an extra delay; it does not skip frames or issue catch-up bursts.

## Upload protocol

```bash
uv run --frozen -m cntrl.maker.morningstar.mc.splash.cli upload \
  cntrl/maker/morningstar/mc/splash/build/spiral.cnsa \
  --port /dev/cu.usbmodem144682801
```

The host validates the prepared asset before opening serial. All messages are
newline-terminated ASCII, except the known-length binary chunks:

1. Host: `CNTRL SPLASH BEGIN SIZE CRC32_HEX`
2. Pedal: `CNTRL SPLASH SEND`
3. Host: up to 512 raw bytes, then waits for `CNTRL SPLASH NEXT RECEIVED`.
4. After the final bytes and durable readback, pedal:
   `CNTRL SPLASH DONE SIZE CRC32_HEX`.
5. Host queries `CNTRL SPLASH STATUS` and verifies
   `CNTRL SPLASH READY SIZE CRC32_HEX`.

Failures use `CNTRL SPLASH ERROR REASON`. Incomplete transfers expire after
10 seconds without incoming data. The previous committed asset remains usable.
FAT/SD power-loss behaviour still requires hardware testing; simulated write
failures do not prove filesystem-level atomicity.

## Binary formats

All metadata is little-endian. RGB565 pixels themselves are big-endian, in bus
order. The 16-byte asset header is magic `CNSA`, u16 version 1, u16 width 160,
u16 height 128, u16 frame count, u32 total bytes. Every frame is u32 duration in
milliseconds followed by 128 pairs of rows: normal outer-screen row, then
pre-mirrored centre row. Each row is 320 bytes. Every frame is independent.

Each SD slot wraps the entire asset with a 16-byte header (`CNSP`, u32 generation,
u32 asset bytes, u32 whole-asset CRC32) and a final four-byte `DONE` marker.
Boot chooses the newest CRC-valid committed generation. No vendor data or show
files are written by this backend.

## Verification

```bash
uv run --frozen pytest cntrl/maker/morningstar/mc/splash/test/test_asset.py
make -C cntrl/maker/morningstar/mc/splash test check-board
```

Python tests exercise the byte-accurate mirrored orientation, frame timing,
invalid assets and actual Python-to-C++ uploads through a pseudo-terminal,
including a 16-frame asset. Native C++ tests cover row bounds, timer wraparound,
looping, dismissal, read failures, CRC rejection, interrupted writes, reboot
selection and timeouts under address/undefined-behaviour sanitizers. Runtime
tests exercise mixed show/asset commands, binary newlines, missing assets,
upload interruption of playback and consuming the dismissing gesture without MIDI.
`check-board` compiles the adapters against the installed Teensy/SD interfaces in
a separate build directory. It is an API check only, not executable firmware
for this pedal. No hardware access occurs in these tests.
