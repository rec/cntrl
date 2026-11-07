# Abstraction baseline

Source: `88f81e1`, captured before extraction on 2026-11-07.

The archived source passes 820 Python tests and the portable native tests,
including sanitizer checks. The combined CPU image builds successfully.

| Region | Usage |
| --- | --- |
| Flash code | 204,072 bytes |
| Flash data | 551,028 bytes |
| Flash headers | 8,800 bytes |
| RAM1 variables | 352,960 bytes |
| RAM1 code | 163,752 bytes |
| RAM1 padding | 88 bytes |
| RAM1 local-variable allowance | 7,488 bytes |
| RAM2 variables | 53,600 bytes |
| RAM2 available for allocation | 470,688 bytes |

Combined-image SHA256:
`516f9cd2a2ae7e6f28876948224609b7033be97c9bed06d0714d6eaa83845e20`.

The complete live backup is at
`/Users/tom/code/cntrl/build/abstract-split/before`.
It contains `readability-demo` (2,690 bytes), `simple-setup` (23,084 bytes),
their assets, songs and the saved selection. The selected setup is
`simple-setup`; the device reports format 34 and startup counter 2, ready.
The backup is the recovery source for on-pedal edits and format conversion.

## Paths that must retain behavior

A switch starts at `usb_probe/buttons.h`, where physical pins become input
indices. The board's poll loop samples each millisecond, passes raw transitions
to the portable controller, and dispatches the recognized binding through the
patch executor. MIDI messages enter the bridge's output queues and state tracker
before the device transports accept them. Pin identities, scan time and output
route mapping currently meet in `patch_switcher/target.cpp`.

Status starts with the board's status snapshot and `pages.h`, which constructs
text and graphical lines for the right-hand viewport. `renderer.h` measures the
selected fonts, clips to 160 by 128 pixels and prepares retained RGB565 column
buffers. `displays.cpp` submits them to SPI/DMA and releases the buffer at transfer
completion. Three physical panels and their orientation are adapter details;
layout and page behavior are application policy.

The current page text uses hinted Atkinson Next SemiBold outlines. Preserve
their pixel fixtures. Untimed animation frames use 100 ms, while still images
use 1,000 ms. The live startup movie has 16 frames and lasts 1,600 ms.

## Evidence

Logs and exact archived source are under `build/abstract-split/` in the
implementation worktree: `baseline-python.log`, `baseline-native.log`,
`baseline-arm.log`, and `baseline-source/`. These are local build artifacts.
Automated evidence does not establish unchanged appearance or physical timing;
the final device qualification must check those separately.
