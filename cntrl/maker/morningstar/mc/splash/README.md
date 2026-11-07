# MC6 Pro splash animation

Set `splash_movie` for startup/setup activation and `idle_movie` for the idle
screensaver. Both default to an empty list, which means no animation. An empty
string also disables that category. A string is split on colons; a list contains
non-empty filename strings. Empty entries within a non-empty list are rejected.

```toml
splash_movie = '../../../../../assets/spiral.gif'
idle_movie = ['../../../../../assets/spiral.gif', 'another.webp']
screensaver_timeout = '30 s'
```

Paths are relative to the setup source file. GIF/WebP animations, PNG/JPEG stills
and prepared `.cnsa` files are supported. Untimed stills use a one-second frame.
Compilation prepares every file with the existing Pillow dependency. Setup
copying uploads and verifies movies before installing the setup. Downloading a
setup saves its movies in `movies/` next to the editable TOML.

One movie is chosen at random from the relevant list whenever startup or idle
playback begins, then loops until dismissed. The two categories are independent.
Missing or invalid SD assets fall through to the normal interface. No selected
setup means no movie. These fields replace `splash`; recompile setups for
the current document version; the host converts legacy assets when rebuilding.

Movies stream from SD in packets of at most 320 bytes held in the player. Full
`.cnsa` sizes do not consume RAM2. Keys and portable filenames are included in
compiled setup sizes and therefore in the setup residency/cache budget.
`total_ram2_needs()` uses an estimated firmware allowance, not a measured
high-water report.

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
It uses `idle_movie` independently of `splash_movie`; an empty idle list disables it.
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

The source GIF is preserved in the repository's top-level `assets/spiral.gif`: 16 frames, 100 ms per frame,
1,600 ms per loop. The Mac scales it to cover a 192x154 area (120% of the
160x128 display, rounded to whole pixels), then crops centrally to 160x128, with
no margins or letterboxing. This preserves proportions while clipping the
edges of the enlarged image. The centre screen is horizontally mirrored;
both outer screens use the unmirrored image. Padding and show text colours do
not apply to this full-bleed splash.

## Portable implementation

Animation preparation, conversion, timing, transfer and playback are implemented
in `cntrl/animation/` and `firmware/include/cntrl/animation/`. The adapter's target
export declares 160x128 images and two streams: the first serves both outer
screens; the second is precomputed with horizontal mirroring for the centre.
Only `board.h` maps logical streams to physical screen masks and binds SD access.
The player retains one 320-byte packet until the output accepts the next region;
no complete animation framebuffer is allocated.

The prepared asset format is CNSA version 2. The compiler converts an existing
version-1 asset using the explicitly supplied target geometry before generating
its new checksum and setup reference. Original assets remain available for
backup and recovery. For an individual offline conversion, run the generic CLI:

```bash
uv run python -m cntrl.animation.cli convert old.cnsa --output new.cnsa --target exported-target.toml
```

Preparing a source image likewise uses the complete target descriptor:

```bash
uv run python -m cntrl.animation.cli prepare source.gif --output prepared.cnsa --target exported-target.toml
```

The generic format and output API are documented in
`cntrl/animation/README.md`. Generic tests live in `test/test_animation_assets.py`,
`test/test_animation_movies.py` and `firmware/test/animation/`.
