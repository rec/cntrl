# Scroll pointer application and RAM reuse

Investigation follow-up, 2026-10-31. The initial analysis below describes the
hardware-scroll baseline; later video evidence and a replacement candidate are
recorded at the end.
Read alongside [the experiment record](../../../../plan/scrolling-bug.md).
The leftward sliver remains unresolved.

## Additional documented evidence

The [ST7735S v1.1 datasheet](https://www.orientdisplay.com/wp-content/uploads/2020/11/ST7735S.pdf),
section 10.1.30, printed page 137 (PDF page 145), says:

> When new Pointer position and Picture Data are sent, the result on the display
> will happen at the next Panel Scan to avoid tearing effect.

Thus immediate scanout application of command 37h is not a safe assumption.
This supports deferred pointer application, but does not specify a complete
cycle-level RAM-fetch model or prove that these particular panels implement it.
The exact controller revision remains unconfirmed.

## What the hardware-scroll baseline assumes

`firmware/patch_switcher/renderer.h`, in `animate()`, increments `ring_`, stages
three incoming columns, and sends the new pointer. Its comment that this never
overwrites an outgoing edge is true relative to the preceding **requested**
pointer, not necessarily the pointer currently used by scanout.

`firmware/test_renderer.cpp` immediately applies each pointer command to its
simulated screens. Its hidden-column assertion therefore cannot detect the
case where several requests arrive before the panel applies them.

At an initially settled logical offset zero, forward writes logical RAM columns
160, 161, 0, 1, 2, ... . If scanout still uses zero, the third write overwrites
the visible leftmost column. Reverse writes 161, 160, 159, 158, 157, ... and
has the corresponding hazard at the right edge. These are logical column
indices before the driver's RASET conversion.

The default progression requests 232 steps in 30 batches: eight batches of
seven steps and 22 of eight. If a batch completes with the previous batch's
pointer still active, five or six writes per panel address its visible region.
Across all 30 batches that is 172 such writes per panel, or 516 across three.
These are arithmetic counts under an explicit hypothetical scan schedule,
not measured corrupt pixels or a reproduction of the physical artifact.

## Timing and limits of the explanation

Initialization in `firmware/usb_probe/displays.cpp` programs B1h with
`05 3c 3c`. Applying the datasheet's section 10.2.1 formula at its nominal
850 kHz oscillator gives approximately 16.59 ms for LINE=160 or 16.71 ms for
LINE=162. These are estimates, not measurements or guaranteed timing limits.

This makes the successful 20 ms experiment consistent with waiting out a scan,
while 2 ms spacing still allows reuse before pointer application. It does not
prove that interpretation. In particular, the simple deferred-pointer model
predicts address exposure in **both** directions. Scan order and the times at
which individual rows are fetched still need evidence to explain clean Undo.
The failed compensated ML experiments are a constraint on that explanation.

Restricting each acknowledged viewport to its two hidden columns at 60 Hz
would take approximately 1.93 seconds for 232 pixels. Therefore that conservative
strategy alone cannot preserve the requested 500 ms motion. This is not a
general lower bound on hardware scrolling: safely reusing already-scanned rows
could permit more work, if their timing is known.

## Next discriminating experiment

The useful missing observation is the time between a pointer command and its
visible application, together with when overwritten edge RAM is scanned.
A narrowly scoped optical test could paint distinctive static columns, issue
one pointer change with no subsequent RAM writes, and measure the visible
response against a command-time reference. A second trial would rewrite the
first reused column at controlled delays after that command, in both directions.
High-speed capture or an optical sensor with a timing reference would make this
more discriminating than another ordinary animation video. Repeat across scan
phases; a single clean delay is not a bound.

A verified accessible TE signal would supply a better direct timing reference,
but no such connection is established. Register receipt is not that reference.
Neither experiment was implemented at the time of this initial analysis.

Validation here was source inspection and independent arithmetic. No application,
device command, firmware upload, Python test suite, or hardware measurement ran.

## Slow-motion video and fixed-origin candidate

The supplied `signal-2026-10-31-17-02-13-890.mp4` was inspected from
`/Users/tom/Desktop/`. It contains 2,792 frames at 1280x720, approximately
30 playback fps and 93.06 seconds. Its middle sections are visibly slowed.
The user selected 240 fps capture, but the exported file alone does not prove
that every source frame survived or establish the capture-time scale.

After rotating the image upright, consecutive frames around playback 55-56 s
show a left-edge strip on the initially blank centre display during C. Its
contents change while the main window enters from the right. Around 77.7-78.2 s,
Undo also has a narrow, broken strip at the right edge of the rightmost display,
separated from the main window by a dark gap. Thus the new video does not
support treating Undo as completely artifact-free. This strengthens the
two-direction RAM-reuse hypothesis, without proving a common physical cause.

The candidate replaces hardware offset changes in both directions with writes
at fixed display addresses. It preserves the existing renderer lifecycle,
panorama geometry, bitmap buffers, default 500 ms duration and final repaint.
It targets 30 completed frames per second, skips obsolete frame targets when
late, and never switches target partway through a frame. This removes dependence
on scanout acknowledging an offset before RAM is reused at the opposite edge.
Ordinary scanout tearing and LCD response time are still possible.

Compared with the earlier rejected software trial, the candidate splits changed
vertical spans, joins only gaps cheaper than another command, and uses blocking
16-bit pixel transfers at the existing 8 MHz clock. It does not use the bulk
buffer transfer previously associated with a stall, DMA, or another framebuffer.
The 30 Hz target is deliberate headroom for software rendering, not a measured
panel or firmware rate. Physical smoothness remains the acceptance criterion.

Native tests verify every pixel of all 15 coloured-pane frames in both
directions, patterned frames with stippled bands, delayed polls during a frame,
clock rollover, custom duration, cancellation, zero-duration transitions,
candidate repaint interruption and final destination styling. Bus tests verify
partial-column addresses, pixel byte order and splash mode restoration.
The coloured forward transition sends 263,580 bytes including commands, giving
a 263.58 ms wire-only lower bound at 8 MHz; each frame fits its 33.33 ms wire
budget. Host fonts are test doubles, so this is not a real-font benchmark.
The older 395.814 ms software-trial wire figure used a different frame schedule
and is not an isolated measurement of the transfer optimization.

Native sanitizer tests and the ARM build/image checks pass. No Python source or
Python-consumed data changed, so the Python checks were not run. The candidate
was subsequently uploaded through the existing helper. The first upload hit
a USB write error; after confirming HalfKay again, the retry completed and
reset the pedal. The saved setup returned `CNTRL READY 20 21411 fa5ba7e8`.

The user confirmed that the edge strips disappeared, but reported painfully
jerky motion. The candidate is rejected: the wire-only budget did not establish
acceptable physical smoothness. Commit `31a0458` is reverted, restoring the
previous hardware-scrolling implementation and its tests. The underlying
scrolling issue remains unresolved.

Candidate image: `firmware/build/patch-switcher.hex`, SHA-256
`19d9f3b80f5910c9b74904fc8fdbee4e446368cb65f5453db239948faeff4c32`.
The build reports 21,984 bytes of RAM1 free for local variables and 292,064
bytes of RAM2 free for allocation. Test and build logs from this session are
`/private/tmp/cntrl-scroll-fix-native.log` and
`/private/tmp/cntrl-scroll-fix-build.log`; these temporary files may not persist.

The restored hardware-scroll sources match `4ea3253` exactly. Native tests and
ARM build/image checks passed again. Restored image SHA-256:
`dca55d0cbfe3be31cd24b47ccf9c54c4baef3759376ad79f4f411f614ed76d20`.
It was uploaded through the helper, reset successfully, and returned
`CNTRL READY 20 21411 fa5ba7e8`. This confirms setup loading after restoration;
the original moving-edge artifact remains unresolved.

## Existing switch-input route checked for TE

A temporary command enabled vertical-blank TE output (35h, parameter 00h)
on one panel at a time, with a disabled baseline and TEOFF on all panels when
finished. It sampled only the existing eight-bit switch reader, without
configuring unidentified GPIOs or changing the scrolling algorithm. Each of
five trials had four 100 ms windows: disabled, left, centre, right. Each window
returned 1,832 samples. Every raw sample was 3Fh; neither unknown bit 6 nor 7
had an edge. No buttons were pressed during the measurements.

This supplies no TE timing reference through the tested reader inputs. It
does not prove that the TE command took effect, that TE is disconnected, or
that no other MCU connection exists. Register readback was not part of this
probe. It does not establish a scanout bound or fix the moving strip.

## TE readback and passive MCU input search, 2026-10-31

The preceding repeated optical recording,
`signal-2026-10-31-19-17-59-393.mp4`, showed a wrong-edge marker at the
5 ms delay in leftward repetitions 5 through 8. In particular, the frame at
45.733 seconds shows the L 5MS R5 label and the old left-edge marker during
onset, rather than during the later reset. A fixed 5 ms delay is therefore
not safe. The exported video contains speed changes, so playback-frame
spacing must not be treated as uniform capture timing.

A further temporary diagnostic closed the missing TE-enable readback check.
It reused the previously hardware-verified bidirectional SDA read routine,
enabled vertical-blank TE with 35h/00h, and read RDDSM (0Eh). All three panels
returned raw `80000000` while enabled and `00000000` after TEOFF (34h).
The [ST7735S datasheet](https://www.orientdisplay.com/wp-content/uploads/2020/11/ST7735S.pdf),
section 10.1.9, defines the leading bit as TE enable. This verifies the
controller setting, not a voltage waveform at its TE output.

The probe sampled `digitalRead(0..54)` without changing unknown pins' mux,
direction or pull configuration. Each run had seven 200 ms phases: all off,
left on/off, centre on/off, right on/off. Each phase performed 56,414 or
56,415 complete pin sweeps; the largest measured interval between sweep
starts was 5 microseconds. No pin transitions were observed. Interrupts
remained enabled; normal display rendering and switch polling were paused
during each synchronous capture. Reported intervals are firmware measurements,
not an independently measured acquisition bandwidth.

Four runs covered both read-back MADCTL A8h (initial landscape state) and
88h (native scrolling state). The final two added a positive control:
with all displays deselected, driving the established shared clock pin 13
high then low read back 1 then 0 in every phase. They also repeated the
existing switch-reader capture with TE enabled on the selected panel:
3,657 samples per 200 ms capture, final value 3Fh, and zero changes in all
eight bits. There were seven such captures per run, with panels 0/1/2
sampled three/two/two times respectively. This strengthens the earlier
negative switch-input result; it does not measure the TE output electrically.

Mux inventory, hexadecimal values:

- Peripheral mux 2: pins 0, 1, 6, 7, 8.
- Peripheral mux 0: pins 42 through 47 (SD interface).
- Mux 13h: pin 11 at the initial inventory, before bit-banged readback.
- GPIO mux 15h: pins 12, 13, 15, 20 through 25, 27 through 29, and 34.
- GPIO mux 5: all remaining logical pins. Their GPIO direction bits were input.

The non-GPIO mux readings cannot be treated as observations of those physical
pads. Even for GPIO inputs this is a software sampling result, not continuity
evidence. Pins outside the Teensy-style 55-entry map were not examined.
Nothing was remuxed to search for signals; in particular, peripheral and
unknown board connections were not repurposed.

Initial probe image SHA-256:
`ca540ad280886aa999b7e6236abaa193722353367900f9666ea8ffa0e65697da`.
Probe with positive control and switch capture:
`ba90296716925d7bb79b2ff1cd9fce1d9071138fa8de461302cec122a13f3cd9`.
Both ARM builds passed binary/HEX comparison and the stock flash-header check.
Local raw captures are `/private/tmp/cntrl-frame-probe-{1,2,3,4}.txt`;
temporary source is `/private/tmp/cntrl-frame-probe.h` and the associated diff
is `/private/tmp/cntrl-frame-probe.patch`. These local artifacts are not durable
repository fixtures. The temporary diagnostic was removed after testing.
The rebuilt previous firmware reproduced SHA-256
`7aeb13b8401ee11b9b32785ea4d181ddd3e860f4a878b47fbfddd6863eb4ef37`.

No usable frame reference has been found. The next discriminating evidence is
the display module/flex identification and continuity of its TE contact, if
exposed, to the board. The controller datasheet describes TE as a separate
output; register readback reports its configuration, not its live level.
Do not infer a frame boundary from successful TE enable, immediately readable
MADCTL changes, or a nominal refresh-period calculation. A connection to one
panel would also need qualification before being used to schedule the other
two independently initialized panels.

## Production batch audit and replacement fixture

The follow-up audit exercises `DisplayRenderer` directly. Four patterned
panes are rendered separately, then copied into a literal panorama with
72-column black gaps. That reference does not use `panorama_sample()`.
Every animation write is compared with the incoming reference column, and
every visible RAM pixel is checked after each commanded offset. Each offset
must follow exactly three writes in panel order 0, 1, 2. The initially hidden
columns are poisoned so exposing one without writing it fails the comparison.

Four complete 500 ms transitions pass: left from ring 0, left from 70,
right from 140, right from 70. Each has 232 one-column steps in 30 time
batches: eight batches of seven steps and 22 batches of eight steps. Tests
drive eight renderer polls per simulated millisecond; this checks scheduling
logic, not actual MCU throughput. Font rendering is the existing host test
double, so these are column/source checks rather than real glyph qualification.

Representative trace, logical RAM columns before the driver's native mapping:

| Direction / initial ring | Step | Written column on all panels | New commanded ring |
| --- | ---: | ---: | ---: |
| Left / 0 | 1 | 160 | 1 |
| Left / 0 | 2 | 161 | 2 |
| Left / 0 | 3 | 0 | 3 |
| Left / 0 | 7 | 4 | 7 |
| Left / 0 | 162 | 159 | 0 |
| Left / 0 | 163 | 160 | 1 |
| Left / 0 | 232 | 67 | 70 |
| Right / 140 | 1 | 139 | 139 |
| Right / 140 | 2 | 138 | 138 |
| Right / 140 | 3 | 137 | 137 |
| Right / 140 | 7 | 133 | 133 |
| Right / 140 | 162 | 140 | 140 |
| Right / 140 | 163 | 139 | 139 |
| Right / 140 | 232 | 70 | 70 |

The driver maps a logical RAM column to RASET `(column + 1) % 162`, and
the commanded ring to SSA `(162 - ring) % 162`. Existing bus tests check
these boundaries separately. A step stages 267 bytes per panel, then sends
three 3-byte scroll commands: 810 bytes total, or a wire-only minimum of
810 microseconds at the configured 8 MHz. Computation, GPIO, transaction
overhead and other work add time; no new physical timing measurement was made.

No wrong source column, unprepared commanded image, or wrap error was found
in these cases. This does not prove physical scanout safety: the test's panel
model applies each command immediately. Within a seven-step batch, five
rewritten columns belonged to the visible image at the batch's starting
offset; an eight-step batch reuses six. That is safe only if the earlier
scroll commands have displaced those columns before their writes become
visible. The old renderer comment claiming outgoing edges were never
overwritten has been qualified to refer to the last commanded offset.

The replacement [optical fixture](firmware/scroll-timing.md) uses the actual
renderer and its normal ordering, unlike the preceding pointer-jump test.
It provides pane/band identity and initially hidden four-pixel stripes, with
constant illumination. It does not change the normal animation algorithm.
Native tests pass, including the fixture's eight transitions and timer wrap.
The next physical recording is still required to determine whether the
reported artifact reproduces with these identifiable patterns and which
data appears at the wrong edge. These results do not establish a hardware
fault or the need for a hardware modification.

The working probe image SHA-256 was
`86f057907103f692a26d92984dc9c040e85ac7180dd091adca1d5af91fa53e18`.
An initial image rejected the diagnostic command because its router had not
been extended; it produced no timing measurements. The corrected image's ARM
build and flash configuration/HEX-ELF checks passed. The probe was then removed
from the sources rather than retained as a production command.

The baseline native tests and ARM image checks passed after removing the probe.
The normal hardware-scrolling image, SHA-256
`dca55d0cbfe3be31cd24b47ccf9c54c4baef3759376ad79f4f411f614ed76d20`,
was restored on the pedal and returned `CNTRL READY 20 21411 fa5ba7e8`.
