# Scroll pointer application and RAM reuse

Investigation follow-up, 2026-10-31. No firmware change or physical test.
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

## What the current code assumes

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
Neither experiment has been implemented or authorized for device execution.

Validation here was source inspection and independent arithmetic. No application,
device command, firmware upload, Python test suite, or hardware measurement ran.
