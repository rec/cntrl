# MC6 Pro dynamic-code feasibility and size reference

Supporting appendix for [the shared plan](../../../../plan/dynamic-code.md).
Recorded 2026-11-14 using the already built normal image associated with the
frame-buffer work in `6c6bd4c`, whose firmware source baseline is main through
`236b59f`. Later main changes are not included in these size measurements.
No new compilation, firmware execution or module-loading experiment was run.

## Existing measured baseline

The existing export is `/private/tmp/cntrl-frame-buffers-integrated-normal`.
Its `memory.txt` and `firmware.elf` report:

| Item | Bytes |
| --- | ---: |
| Flash executable section, `.text.code` | 207456 |
| Fast executable section, `.text.itcm` | 191600 |
| `.fini` | 4 |
| Executable section total | **399060** |
| Counted instruction memory, including unwind bytes | 191608 |
| Instruction-bank reservation, six banks | 196608 |
| Spare within those banks | 5000 |
| Font/constant section, `.text.progmem` | 859860 |
| RAM1 static reservation, including reusable frame pool | 309984 |
| RAM1 stack/local allowance | 17696 |
| RAM2 static reservation | 91008 |
| RAM2 linked heap budget | 433280 |
| Reusable RAM1 frame/runtime pool | 245776 |

The SDK size tool reports 399064 code bytes because its accounting includes the
eight unwind bytes while the three executable sections above include `.fini`.
Keep the accounting definitions explicit. Font/constant bytes are not all
optional executable code and must not be advertised as code-loading savings.

The same checkpoint's diagnostic build has 192472 counted instruction bytes and
4136 bytes spare, with a 433152-byte linked RAM2 heap budget. Its existing
four-KiB linker reserve must remain enforced when introducing a loader.

## Attribution from the existing ELF

Read the saved ELF with the pinned `arm-none-eabi-nm`, without relinking. Count
text/weak-text symbols once per address within each bucket; otherwise constructor
aliases double-count the same instructions. The following are selected named
function bodies, not complete independently linked features:

| Named owner bucket | Body bytes | Shared-plan module budget |
| --- | ---: | ---: |
| `midi::AdvancedProcessing` | 12764 | Advanced processing: 18–32 KiB |
| `midi::MidiLooper` | 9068 | Looper: 12–20 KiB |
| `midi::MidiFilePlayer`, `MidiFileRecorder`, `MidiFiles`, `StreamFilePlayer`, `CachedMidiFile`, `SmfIndex`/`MidiIndex` | 11854 | MIDI files: 16–28 KiB |
| `midi::MidiArpeggiator` | 8954 | Arpeggiator: 12–20 KiB |
| `Songs`, `SongsUi`, `SongsTransfer` | 15926 | Songs: 20–32 KiB |
| `Switches`, `SwitchGestures` | 9862 | Switch assignments: 12–18 KiB |
| `midi::Monitor`/`MonitorDecoder` | 1886 | Monitor: 4–8 KiB |
| `animation::Player`, `Session`, `Receiver`, `ColorCycle`, `Router` | 3908 | Movie playback: 6–12 KiB |
| `SetupUi`, `ModuleMenu`, `StorageListing`, `HistoryImport`, `DiskSpeed` | 6304 | Optional storage tools: 8–16 KiB; some ownership remains resident |
| `Keyboard` | 416 | Keyboard controls/page: 2–4 KiB |

Useful context: selected `Routing<...>` bodies occupy 29360 bytes, while
`ui::Renderer`/`Raster`/`Pages`/`PageHost` together occupy 25716 bytes. These
buckets mix required core and optional behaviour. They are not proposed modules
and cannot all be removed to obtain advanced-processing or scrolling savings.
Panic and Cable Test bodies together occupy 4978 bytes; Panic stays resident,
so the cable-test module's 3–6 KiB range requires a real separation/link check.
Scrolling's 4–10 KiB range likewise requires separation from ordinary rendering.

The gap between named bodies and planning ranges allows for associated helpers,
page adapters, small constants and interface glue. It is an estimate, not a
measurement of those additions. Inlined code has no separate owner symbol;
template helpers and out-of-line generic code can be shared. Some predicates
can include a referenced type rather than its owning class. Addresses are
deduplicated within each bucket, not across all buckets, and padding/veneers are
not attributed. Do not add the rows or treat their sums as removable bytes.

For example, one reproducible inspection of the saved image is:

```sh
cntrl/maker/morningstar/mc/firmware/.tools/data/packages/teensy/tools/teensy-compile/15.2.1/arm/bin/arm-none-eabi-nm \
  -S --size-sort --radix=d -C \
  /private/tmp/cntrl-frame-buffers-integrated-normal/firmware.elf
```

Filtering text/weak-text rows for `cntrl::midi::MidiLooper::`, deduplicating their
decimal address column, and summing their decimal size column gives 9068 bytes.
Return-type-prefixed template names need a qualified-owner match rather than
assuming the fourth whitespace field is always the function name. The generated
inspection table is disposable; its source ELF/toolchain and method are recorded
here. Independently linked artifacts are still needed for authoritative sizes.

## Executable-memory constraints

The pinned Teensy core 1.62.0 `cores/teensy4/startup.c::configure_cache()` configures:

- ITCM at `0x00000000` as executable and read-only.
- DTCM at `0x20000000` as read/write and execute-never.
- AXI RAM at `0x20200000` as cached read/write and execute-never.
- Application QSPI flash as executable read-only memory.

Therefore ordinary `malloc` storage, including freed display memory, cannot
simply receive bytes and become a callable module. The frame pool is DTCM data
storage, not an executable arena. A target backend must deliberately reserve
and protect executable storage, synchronize caches and preserve all other
protection regions. Do not make the whole heap executable.

Match the existing compiler's Thumb Cortex-M7 hard-float ABI, including
`-mfpu=fpv5-d16 -mfloat-abi=hard`. Validate Thumb entry points and their executable
ranges; kernel/module calls use the interface table rather than assuming direct
branches can reach between every memory region. Keep module writable data and
zero-initialized storage execute-never.

Prototype a cold module in a dedicated AXI-RAM arena only after checking the
available MPU regions, required power-of-two alignment/size, permissions and
cache maintenance against the actual pinned startup configuration. An arena
rounded to 64 KiB consumes 64 KiB even for a 6 KiB module. That is an illustrative
cost, not a selected production arena size or an allocation experiment.

For hot engines, qualify an explicitly reserved ITCM overlay region or retain
their code resident until that region exists. An ITCM overlay itself reserves
instruction banks. Do not repartition FlexRAM or borrow stack/data banks during
performance. Multiple active engines need simultaneous admitted storage; loading
one feature over another active feature is not an acceptable scheduling policy.

At this baseline, reducing counted resident ITCM to at most 159744 bytes would
both fit five banks and leave the existing 4096-byte reserve. From 191608 bytes,
that requires removing at least 31864 bytes, before adding loader or overlay
costs. A smaller flash payload alone does not release a bank. Conversely, a
32 KiB bank released to data is not a net RAM saving if the replacement module
and arena overhead consume more RAM elsewhere.

## Planning allowance and decision gate

Allow **8–16 KiB of resident executable code** for the bounded loader, lifecycle
dispatch, compatibility checks and target glue until a proof establishes its
size. Allow separate bounded transfer scratch and per-module metadata, then
charge actual executable-arena alignment and candidate/active peaks. This is
an engineering allowance, not a measured loader implementation.

Do not quote a final kernel size by subtracting module ranges from the measured
389.7 KiB executable total. The first independently linked kernel/modules must
show which shared code remains, how much fast code really disappears and how
much RAM is needed by a basic-forwarding setup, a recording-focused setup with
zero frames, and a setup combining advanced processing, looper and arpeggiator.

Proceed to production only if those complete profiles improve the intended
resource constraint while preserving MIDI deadlines and the mandatory reserves.
Cold modules primarily reduce resident flash; a recording-focused profile may
be better served by keeping cold code in flash and freeing data buffers instead.

## Additional work beyond the prompt

None.
