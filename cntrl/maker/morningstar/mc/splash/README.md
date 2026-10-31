# MC6 Pro splash animation

The patch-switcher target integrates this player and upload protocol. Set
`splash = true` in the show (`"splash": true` in JSON) to play the installed
animation on boot or show activation. The default is true: omitting the setting
uses the installed animation, unless `splash = false` is explicit. With no active
setup, startup also uses the installed default animation. Upload the prepared
asset before relying on playback. A missing or invalid asset
falls through to the normal interface.

Startup playback turns the backlight off immediately, paints the first complete
animation frame while dark, then fades up to the show's brightness over 2,000 ms
using the quadratic curve. A button can dismiss the animation during this fade
without sending a patch. Idle-entry and wake-up timings are unchanged.

Any button dismisses playback. The complete dismissing gesture, including
release and other held buttons, is consumed without sending a patch. Dismissal
fades the animation to black over 1,000 ms, invalidates all three renderer caches
and repaints the normal interface while dark. After every screen is ready, it
fades back to the show's brightness over 1,000 ms. The total is two seconds of
fading plus the brief black repaint interval. Animation continues during fade-out;
USB, button scanning and MIDI servicing continue throughout. After releasing the
dismissing gesture, subsequent presses act normally even before the fade ends.
Uploads cancel the transition and restore the configured brightness.

All fade ramps use quadratic PWM brightness: the target brightness multiplied
by the square of the normalized level. Fade-out reverses that same curve.
Halfway through an uninterrupted ramp, brightness 192 therefore produces PWM 48,
not 96. Normal wake fades start at the current normalized level, without a
brightness jump. Cancelling idle entry instead bypasses the ramps and restores
the configured brightness immediately.

## Idle screensaver

Set `screensaver_timeout = 3` in a TOML show, or `"screensaver_timeout": 3` in
JSON, for a three-second physical-input idle timeout. The default is 0 (disabled).
It uses the installed animation and does not require startup `splash` to be true.
The pman test show enables both. Presses and releases reset the timer, and it
never starts while a button remains held. Missing assets leave the normal UI up.

Entry fades the UI out over four seconds, paints a complete first animation
frame while dark, then fades the animation in over four seconds. Throughout
these entry phases, any button cancels the transition immediately, stops the
animation, restores the configured brightness and requests a full UI repaint.
Normally the cancelling press, other held buttons and the complete release are
consumed without a MIDI or navigation action. Main-page C is the exception: it
cancels idle entry and performs its send immediately. A successful program-change
brightness boost stops any remaining fade and applies the backlight directly
until release. Startup dismissal still consumes its complete gesture.

Once entry has finished and the screensaver is fully running, pressing any button
retains the fast wake transition: 200 ms out, complete UI repaint while dark,
200 ms in. This press also performs its normal musical action immediately.
Waking cannot also open setup management: release the wake gesture and press
A+D a second time to open it. The uninterrupted entry and normal wake transitions include a
brief black repaint interval beyond the two ramp durations. Neither blocks USB,
button scanning or MIDI queues. Incoming MIDI and USB status queries do not reset
the physical-input timer. Show/asset transfers temporarily inhibit idle entry.

The setting is encoded in the current show format 9. Recompile older shows before upload;
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
GIF decoder, decompression, resizing or image mirroring. Base RGB conversion
happens on the Mac. Rows
are stored in the exact RGB565 byte order needed by the display bus.

## White-band colour walk

The board output adapter now recolours the white component at playback time,
using two smooth pseudorandom walks through RGB colour space: one for L/R and
one for the mirrored centre. Each walk chooses nearby targets, with each six-bit
channel moving by up to 20 levels per nominal colour segment, and interpolates
toward them. Each walk also has its own independent speed walk: a new speed
target is chosen every ten seconds and approached smoothly, bounded to 0.25..2
times the original pace, starting at 0.5 times the original base speed. The
fastest rate remains unchanged. The speed target moves by at most 0.25 times base speed
per segment, reflecting at its limits. Boundary reflections keep RGB channels
in 0..63 without
clipping them against the cube walls. The pace is comparable to the former
20-second hue cycle, but there is no fixed colour loop. Each pixel's
common RGB component is replaced by the tinted equivalent; its remaining
colour is retained. Thus black and pure green are unchanged, grey antialiasing
keeps its coverage, and white-to-green edge blends retain their green residue.
Other coloured pixels with a white component are also affected; this is not an
exact-white-only replacement.

Both tints are sampled together at the first outer-screen row and held for the
complete frame. The outer screens still share a single
broadcast transfer. One 320-byte temporary row is transformed before sending;
stored assets, checksums, geometry, frame timing and backlight fades are unchanged.
The effect is limited to splash/screensaver output, not the main UI. It is now
optional: the setup setting `animation_color_cycle` defaults to `false`.
When disabled, original asset pixels are sent directly without the tint transform
or color-walk calculations. Set it to `true` to enable the independent centre/outer
color walks. This controls both startup and idle animation and does not change
GIF motion, frame timing, or backlight fades. The current `simple-setup.toml`
explicitly sets it to `false`. This setting requires setup format 20 firmware.

`color_cycle.h` implements the walk and pure integer RGB565 transform. The walk
uses distinct seeds mixed with the first frame's uptime, continues between animation loops and
does not replay a long backlog after an idle pause. This is visual randomness,
not a cryptographic generator. Native tests check channel bounds and smoothness
over ten minutes for both walks, reproducibility without shared state, distinct
centre/outer paths, clock wraparound, black and green invariance, grey shading, green
edge blends and identity under a neutral tint for all 65,536 RGB565 values.

Screensaver playback still loops without a time limit until physical input;
normal wake/dismiss behaviour is unchanged.

The preceding fixed-hue-cycle build was flashed on 2026-10-24 after splash/runtime, display/renderer native
tests and stock flash-configuration/HEX-ELF checks passed. Image SHA-256:
`f847942f295e055f4454434c364802f45e74b031d5dc0cf2eee87ca7bd0f7c05`.
The uploader returned its usual reset/disappearance status 1; USB then confirmed
`CNTRL READY 7 23390 0718301d`, with pman still selected. Visual colour and
playback-speed confirmation remains a physical check.

After the USB/MIDI handoff, the combined random-walk firmware passed all 204
Python tests and native splash/runtime, controller/MIDI-bridge and display/
renderer tests. Stock flash-configuration and HEX/ELF checks passed. The image
was uploaded on 2026-10-24, SHA-256:
`0e9dd09363307bfdba2b5bb1bd78b93e11eb0c814be340f3b354df1fdd5726ad`.
After the updater's usual reset/disappearance status 1, USB confirmed
`CNTRL READY 8 23390 e12bd7da` with pman selected, matching the pre-upload show
length/checksum. No setup or animation assets were rewritten. Visual evaluation
of the walk remains a physical check; the screensaver loops until button input.

The independent centre/outer colour and speed walks were built, inspected and
flashed on 2026-10-24 after native splash/runtime and display/renderer tests
passed. HEX/ELF and stock flash-configuration checks passed. Image SHA-256:
`e07b74d4145f70d47e1a5238ab1bfa25134b3e027ceaeb216f4c3599d320e3f2`.
After the updater's usual reset/disappearance status 1, USB confirmed
`CNTRL READY 8 23390 e12bd7da`, with pman still selected. Assets and show data
were not rewritten. The actual colour/speed effect still requires observation.

On 2026-10-25 the initial colour speed and minimum were halved, while the 2x
maximum was retained. Speed-walk segments doubled from five to ten seconds.
GIF motion and UI transitions were unchanged. Native splash/runtime and display/
renderer tests and image checks passed; the fixup was flashed and USB confirmed
pman and `CNTRL READY 8 23390 e12bd7da` unchanged. Image SHA-256:
`fb5740e44e60ef21cbc24bc36b558c35d83034fade0508797bc39fb9c9d225e8`.

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
