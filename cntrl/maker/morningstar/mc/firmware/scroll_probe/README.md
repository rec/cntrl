# Standalone controller scrolling diagnostic

This directory contains the older standalone experimental firmware. It is
separate from the patch-switcher `CNTRL DISPLAY EDGE` diagnostic.

All analysis, historical controls, timings, qualification results and uploaded
image records are in the [standalone diagnostic archive](../../scrolling-scan-timing.md#standalone-diagnostic-archive).
For the installed experiment, use the [current diagnostic instructions](../../scrolling-scan-timing.md#current-diagnostic-two-column-half-second-test).

## Build and test

```bash
make -C cntrl/maker/morningstar/mc/firmware test-scroll-probe inspect-scroll-probe
make -C cntrl/maker/morningstar/mc/firmware test
```

`SCROLL_GAP` configures both equal gaps in equivalent display pixels at build
time, default 72. For example, to test and build a 48-pixel estimate:

```bash
make -C cntrl/maker/morningstar/mc/firmware SCROLL_GAP=48 test-scroll-probe inspect-scroll-probe
```

The firmware status reports the compiled gap. This standalone diagnostic does
not load show configuration or modify saved settings.

These targets build and test only; they do not upload firmware. Historical
validation and image-staging records are in the consolidated investigation.
