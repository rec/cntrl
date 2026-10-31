# Leftward scrolling slivers

Investigation record, 2026-10-31. The leftward slivers are **not fixed**.
This report preserves results from the discarded investigation commits. Their
IDs below are historical references, not commits retained in the cleaned history.
This file is placed in `plan/scrolling-bug.md` at the user's explicit request.

## Retained baseline and fixes

`33c80cd` is the baseline. It uses hardware scrolling over a 162-column RAM
ring with 160 visible columns. Each animation step stages an incoming column
on all three panels and then changes the scroll offset. C moves left; F/Undo
moves right. The gap is 72 pixels and the transition pitch is 232 pixels.
The default duration is 500 ms. Normal progress advances in 60 Hz batches.

The user confirmed that this baseline removed Undo's slivers. C retained a
thin wrong-image strip at the left edge. Later reports described strips on
all three displays and estimated their width at approximately four pixels.
The artifact appears during motion and disappears after settling.

Only two behavioral fixes from the investigation are retained:

1. Original `17d8ef5`: permit a transition during a pending candidate repaint.
   The old `ready()` guard silently rejected the animation while the controller
   still changed the patch. The fix snapshots the current pixel cache and
   cancels unfinished repaint work. Invalid old frames and overlapping
   transitions still prevent animation. A focused renderer regression covers
   browsing followed by C during repaint. Its individual physical effect was
   not conclusively isolated from subsequent trials.
2. Original `4041c94`: cancelling an idle fade must not invalidate an unchanged
   UI. `Session::stop()` previously requested a redraw whenever a fade existed,
   even before splash painting. C's wake/brightness feedback could therefore
   cause the next display poll to invalidate and cancel its new transition.
   Request redraw only if splash playback was running, retaining any existing
   request. Runtime tests reproduced the unnecessary request before the fix
   and passed afterward. The user confirmed the first C press animates now.

Neither fix corrects the moving left-edge sliver.

## Physical experiments

### Refresh-order bit without address compensation

Forward MADCTL was changed from 88h to 98h, adding ML, while retaining the old
SSA conversion. C moved the wrong way and finished misaligned/incomplete.
Undo remained correct. Reverting restored the previous motion. This trial
demonstrated that changing ML also requires accounting for scroll mapping.

### Refresh-order bit with compensated SSA

Forward used MADCTL 98h and positive SSA; Undo used 88h and negative SSA.
The current logical offset was reapplied when changing mode. Native bus tests
modeled the conversion and preserved image position across mode changes.
Experimental image SHA-256:
`f7f393385e575bfcc7fb87e721d8d126a639d1e92654a7377471c8f3df454aaf`.
The user reported correct leftward movement and final alignment, but unchanged
slivers. The uncommitted experiment was removed. Baseline image restored:
`0ef7b595f3a60b68161e02385e8ca7be9bb7c80b100f076edb1a398575d55809`.

### Forward pacing instead of bursts

`b6bedfa` spread forward progress across the duration and prohibited steps less
than `duration_ms / display_pitch` milliseconds apart, two milliseconds at the
default duration. It left Undo's cadence unchanged. Tests covered delayed polls
and repeated calls at one timestamp. Build and native tests passed.
Image: `7acb3d85a71226f7d6a75c353c26bc72c0a64b0f1f25f5b8c0c2712efbcc4240`.
The same slivers remained. The user also reported that the first C transition
jumped directly to the new patch. That separate cancellation path led to the
retained splash fix above. The pacing trial was reverted in `54a33fe`.

### Slow forward-only diagnostic

`b4d0184` required at least 20 ms between forward column steps, producing an
approximately 4.6-second transition. Undo kept its existing timing. The user
explicitly approved this temporary slowdown. Image:
`18203e2026b3e59715362e37733cabd75b27ba6475e40c39e9eb9561b96d8a94`.
The user reported **no C slivers and clean Undo**. This demonstrates timing
sensitivity, but does not establish the precise cause or prove that a code
error is impossible. The diagnostic delay was subsequently removed.

### Software-rendered forward motion

`df4e934`, explicitly approved as an alternative rendering approach, held SSA
fixed for C and wrote changed vertical spans into the visible RAM addresses.
Frames completed before choosing another target position. Undo retained its
hardware-scroll path. No additional full-screen image buffer was introduced.
Native tests covered final output, partial-write addresses and Undo following C.
One colored-pane test sent 379424 pixel bytes in 1490 writes: at 8 MHz, its
minimum wire time including command bytes was 395814 microseconds. This excluded
CPU/driver overhead and was not a measured hardware frame rate. Image:
`e11e5c19e8c71bf2dadf00e7986866785bdd1d5977186f193cbf6839c372a521`.
The user reported clean C output but **much too choppy** motion. The replacement
was unacceptable and was removed by `d263411`.

An unflashed follow-up split separated changed spans, bridging unchanged gaps
only when cheaper than another command. The same test measured 327680 pixel
bytes in 1700 writes, 346380 microseconds minimum wire time, about 12.5% less.
That change was discarded. A synchronous bulk SPI transfer was inspected as
another optimization but was not implemented or flashed.

### Scroll command before incoming write

`a014c11` reversed only C's order: SSA first, incoming column second. Data,
addresses and speed were unchanged. Undo still staged data before SSA. Native
tests checked the moved interior after SSA and the incoming edge after its write.
Image: `0056889b4db3154eecd5bf95544246ee26cda12a0f1571aad701abf66be0b35c`.
The user reported the same left-edge sliver. Reverted in `31c8dfb`.

### Refresh-order retry with settling and readback

`75ef005` repeated compensated forward MADCTL 98h/positive SSA, now waiting
20 ms without blocking before starting C's configured animation duration.
It captured all three mode registers and restored 88h/negative SSA on completion.
Image: `65656eab916176e4c50729217696b6f223b1f995cc1a9acc1c82bbd4538b2507`.
The user again reported the same C sliver and working Undo. Captured bytes were
`989898`; afterward all panels read 88h. This confirms receipt of the requested
register setting, not the precise moment scanout applied it. The trial was
reverted in `49eb15f`.

## Video evidence

The user supplied `/Users/tom/Downloads/20261031_145523.mp4`, 1920x1080 at 30 fps,
approximately 13.22 seconds long. It is ordinary video, not high-speed capture.
During the first C transition, the centre display was initially blank. A white
window entered from the right while a narrow white strip appeared at the far
left. The strip stayed at the edge while its contents changed, then disappeared
after settling. Thus it was not simply a remnant of a previous window there.
It is consistent with incoming data becoming visible at the wrong edge during
RAM reuse, but does not distinguish mapping from scanout timing by itself.

Inspection artifacts were written under `/private/tmp`: `cntrl-video-contact.jpg`,
`cntrl-video-transition.jpg`, and `cntrl-sliver-closeup.jpg`. These temporary
files are not repository assets or guaranteed to persist.

## Address and timing audits

The first audit checked symmetry: all 162 starting ring offsets over 232 steps,
37584 pairs. Forward writes `(ring + step + 159) mod 162`; Undo from a reflected
origin writes `(-ring - step) mod 162`. Reflection makes these match. This only
establishes internal symmetry and was initially given too much weight.

`e873a8b` independently interpreted the recorded bus command bytes using the
datasheet's 132x162 geometry. MY=1 maps RASET r to physical RAM row 161-r.
ML=0 rolling scrolling maps gate g to `(g + SSA) mod 162`. With the assumed
mounted origin, visible gates run from 160 at the left to 1 at the right.
Unique logical-column labels were assigned to physical RAM rows; all 160
visible columns at all 162 offsets passed, 25920 comparisons. This did not
use the panorama sampling formula but remained conditional on physical geometry.

At neutral offset physical rows 0 and 161 are hidden. Forward writes rows
0, 161, 160 with requested SSA 161, 160, 159. Undo writes rows 161, 0, 1 with
SSA 1, 2, 3. The third step reuses the original left edge for C or right edge
for Undo. These addresses are safe relative to the requested viewport only;
the current tests do not establish safety relative to the live scanout.

A simple hypothetical timing model latched SSA every 17 ms across 17 starting
phases. At evenly spaced 500/232 ms steps, it counted 2919 writes into the
latched visible region; at 20 ms steps, none. This was a model using assumed
timing, not a measurement or proof of the panel's actual scan behavior.

## Register readback and synchronization probes

`17d2bbd` added `CNTRL DISPLAY READ`. Bit-banged reads released the shared SDA
wire before the final command clock fell, then preserved 32 raw response bits.
There is no separate MISO input in the stock display constructor, but actual
testing confirmed bidirectional communication over the shared wire.

All three panels returned the same values:

| State | RDDMADCTL (0Bh) | RDDCOLMOD (0Ch) | RDID2 (DBh) |
|---|---|---|---|
| Splash | a8000000 | 05ffffff | 89ffffff |
| Patch UI | 88000000 | 05ffffff | 89ffffff |

The leading bytes agree with the programmed modes and pixel format. ID byte
89h alone does not establish the exact controller revision. A read-only font
query was used to dismiss the splash; its missing-asset error did not modify
saved files. No observed addressing-mode discrepancy was found in these reads.

`5771f5f` added `CNTRL DISPLAY SYNC`: request ML=1 on panel 0 in native mode,
poll at most sixteen reads, restore the original requested mode, report elapsed
microseconds. Twenty physical trials all observed the changed bit on the first
read in 88–89 microseconds; all panels subsequently returned 88h. This provides
no usable frame-boundary signal and cannot acknowledge scanout application.

The documented RDDSM (0Eh) exposes TE enable and mode, not live TE level or a
scan-line counter. RDDSDR self-diagnostics are triggered by Sleep Out rather
than each frame. No documented live scan-line read was found in the examined
ST7735S command set. A TE connection to the MCU has not been identified; that
is not proof that none exists. No external TE measurement was performed.

## Stationary physical mapping check

`7285446` added `CNTRL DISPLAY MAP <offset>` and END. It painted one column per
poll, then held the requested offset still. Logical columns 0–159 used 001Fh,
160 F800h, 161 07E0h. The initial color names were wrong: the user observed a
red background, blue at 160 and green at 161. No color correction was applied
to ordinary UI rendering as part of this investigation.

The user confirmed the following on **all three panels**:

| Offset | Observed settled image |
|---:|---|
| 0 | Solid red, neither marker visible |
| 1 | Blue at the far right, green hidden |
| 2 | Blue then green in the two rightmost columns |
| 160 | Blue then green in the two leftmost columns |
| 161 | Green alone at the far left |

END restored the normal patch UI and the saved setup remained loaded. These
observations support static origin, hidden-column placement, direction and
wraparound at the tested positions. They do not reproduce concurrent writing
and scrolling. The test was poorly prioritized after substantial prior evidence
that settled images aligned correctly; its limited conclusion must not be
presented as resolving or exhaustively ruling out the reported failure.

## Stock firmware inspection

Stock v3.14.0 has no scrolling behavior to use as a reference. Inspection found
no TEON in initialization tables at flash 60013828h, 60013864h and 60013894h.
The software-SPI byte path at ITCM 48E48h uses fixed clock-edge delays, not a
TE input wait. Constructor 49858h passes MISO=-1. A changed-row drawing path
around 4955Eh compares old/new rows and submits changed rows. Flush dispatcher
AD4h invokes the three display objects sequentially. Alternative SPI peripheral
status waits concern transfer completion, not panel refresh. No panel sync wait
was found in the inspected paths; higher-level scheduling and every indirect
call were not exhaustively traced. This inspection did not supply a scrolling
solution and was another poorly targeted detour.

## Validation and operational limits

Native display/renderer and applicable splash tests passed for the retained
fixes and deployed trials; combined ARM firmware builds also passed. Native
scroll models apply SSA synchronously. They cannot prove physical scan timing,
absence of tearing, or visual smoothness. The marker checks and user animation
reports are separate physical evidence, not consequences of test success.

Uploads used the user-run upload_helper, with exact image digests. An initial
write error was commonly followed by a verified bootloader check and successful
retry ending in reset/disappearance status 1. Device status was then checked
separately. Repeated saved-setup status was `CNTRL READY 20 21411 fa5ba7e8`;
that confirms setup loading, not the identity of every experimental code change.
The register-capture trial supplied additional setting evidence as noted above.
No deliberate SD setup/library changes were part of these scrolling experiments.

## Conclusion and cleanup

The evidence establishes a direction-dependent, timing-sensitive moving-edge
artifact. It does **not** establish an exact root cause. Arithmetic was not
cleared by the first symmetry audit, slow success did not prove hardware
scrolling unusable, and software rendering did not satisfy smoothness needs.
The stationary checks support the tested static mapping but leave live RAM
reuse ordering unresolved. No normal-speed hardware-scroll fix was verified.

The cleaned history retains baseline 33c80cd plus the two transition fixes and
their focused regressions. All experimental algorithms, delays, command-order
changes, register probes, marker commands and additional address-audit tests
are removed. This report preserves their results without retaining the tools.

Primary references:

- [ST7735S v1.1 datasheet](https://www.orientdisplay.com/wp-content/uploads/2020/11/ST7735S.pdf), sections 9.11, 9.12, 9.19 and 10.1.
- [ST7735S v1.3 datasheet copy](https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/908/ST7735S_5F00_v1.3.pdf), used for the later physical-address review.
- Local stock image: `2026-10-16_MC6_PRO_Firmware_v_3_14_0_261016501_0.hex` under `cntrl/maker/morningstar/mc/`.
