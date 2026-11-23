# Doom on the Morningstar MC6 Pro

Port an upstream Doom engine to run on the pedal itself. The first playable
build should start an original test map or a compatible Freedoom map, render
on the centre display, and support movement, turning, firing and using doors
from the six footswitches. Keep the first build silent and single-player.

## Starting point

- The MC6 Pro uses an i.MX RT1062 and has three 160×128 RGB565 drawing areas.
  Treat external PSRAM as unavailable until verified on the actual pedal.
  [Hardware notes](../cntrl/maker/morningstar/mc/mc6-pro-pin-map.md)
  [Display notes](../cntrl/maker/morningstar/mc/README.md)
- A centre-screen RGB565 frame costs 40 KiB. An 8-bit indexed frame at the same
  resolution costs 20 KiB. The engine may need additional working buffers.
  Measure the free space in each RAM bank while CNTRL is running. The linked
  heap budget includes space used by the active setup and display buffers.
  [CNTRL memory notes](../cntrl/maker/morningstar/mc/dynamic-memory.md)
- Start by evaluating the RP2040 Doom port. It targets 264 KiB of RAM and
  accesses compressed level data and graphics directly in flash. Establish
  how its data conversion works for the selected test map and whether the
  converted data can fit in the MC6 Pro application region. If the data must
  live on SD, check random-read latency before committing to that design.
  The Teensy 4.1 MCUME port is a useful reference for the processor and uses
  external PSRAM. [RP2040 Doom](https://kilograham.github.io/rp2040-doom/)
  [MCUME](https://github.com/Jean-MarcHarvengt/MCUME)

## Implementation sequence

1. **Qualify the engine and content.** Pin the engine and asset revisions.
   Verify that the asset conversion, resulting image and any upstream patches
   are reproducible. Use an original test map or compatible Freedoom content
   and retain the upstream notices. Confirm the GPL obligations and CNTRL's
   licensing before linking the engine into a distributable CNTRL build.
   [Doom source release](https://github.com/id-Software/DOOM)
   [Freedoom](https://github.com/freedoom/freedoom)
2. **Get a native frame and input trace.** Give the engine a small platform
   layer for timing, input, video and asset reads. Advance game time in fixed
   35 Hz ticks with deterministic input in tests, and allow rendering to run
   at a lower rate. Share the engine and game loop between firmware and the
   native emulator, with platform adapters for the actual display and storage.
   Put generic code under `firmware/` and MC6 Pro-specific code and
   qualification under `cntrl/maker/morningstar/mc/`.
3. **Connect one display and six switches.** Use the existing page lifecycle
   as demonstrated by Pong, with an explicit entry and exit. Start with A and B
   to turn, D and E to move forward and back, C to fire, and F to use. Check
   the exit gesture against the input router so it still works with a switch
   held. Derive captions from the target's physical labels. Convert the
   indexed frame to column-first, big-endian RGB565 in bounded strips using the
   existing display interface. Keep accepted transfer buffers valid until the
   display is ready again, and let button scans and MIDI take priority over
   drawing. [Page host](../firmware/include/cntrl/ui/page_host.h)
   [Display interface](../firmware/include/cntrl/ui/display.h)
4. **Prove it on the pedal.** Record the linked flash and RAM use, peak engine
   allocations, largest free block per RAM bank, game tick time and frame rate.
   Exercise sustained gameplay with USB and DIN traffic and compare button
   latency and MIDI jitter against a CNTRL-only baseline. Check that entering
   or leaving the game preserves the active setup, that gameplay presses emit
   no unintended MIDI, and that exit clears held inputs and restores the
   previous page. If the memory or timing budget fails, bring the measurements
   back for a decision on a separately built game image.

## First playable acceptance

- The same test map starts in the native emulator and on the MC6 Pro.
- A player can move, turn, fire, open a door and exit through the footswitches.
  Hold, release and exit are covered by a deterministic input test.
- The emulator captures a repeatable frame after a fixed input sequence.
  Aim for 15 rendered frames per second on the test map while maintaining
  35 Hz game ticks. Check for dropped MIDI messages and compare measured
  jitter with the same traffic and setup before entering the game.
- Missing or incompatible game data leaves CNTRL usable and shows a clear
  error. A successful exit restores the previous page and releases game
  buffers.

## Additional work beyond the prompt

None.
