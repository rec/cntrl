# LFO qualification

The shared [LFO implementation](../../../../doc/lfos.md) was implemented in
`d183de4`, using the existing Morningstar target, Teensy 4.1 memory bank split,
and manually dispatched cross-platform workflow.

## Automated evidence

Local macOS arm64 qualification used Python 3.13.12, Apple Clang 16 and the
provisioned FreeType 2.14.3 backend:

- Full Python suite after updating from main and fixing the qualification
  blockers: 1142 passed, with seven existing contrast warnings.
- Focused LFO suite: 48 passed, including native ASan/UBSan waveform,
  transaction, clock, backpressure, lifecycle and full-resolution fixtures.
- Shared native suite, Ruff B/E/F/I, formatting, typing and Python 3.13
  pyupgrade checks passed.
- Target build passed.
- A relocated fine-CC LFO setup compiled to 5332 bytes and round-tripped through
  the setup bundle using the exported target's actual RAM2 limits.

The cold-cache Windows 2025, Ubuntu 24.04 and Intel macOS 15 full qualifications
for `fd1ad97` all passed in
[GitHub Actions](https://github.com/rec/cntrl/actions/runs/42780088610).
Each runner passed native tests, the target build, Python tests, lint, typing,
installed-wheel smoke checks, relocated source/native tests and adapter-free
export compilation with the LFO setup bundle validation.
The workflow has only `workflow_dispatch`, with no push trigger.

## Memory

The combined target build, including main's MIDI monitor and pickup support,
reported:

| Section or allowance | Bytes |
| --- | ---: |
| Executable ITCM section | 196464 |
| RAM1 instruction-bank reservation | 196608 |
| RAM1 data | 33472 |
| RAM1 BSS | 279648 |
| Remaining RAM1 stack space | 14560 |
| RAM2 DMA BSS | 118752 |
| RAM2 space after static DMA BSS | 405536 |

Page rendering and LFO configuration/diagnostic routines use the existing
flash-placement hook to retain the current bank split. These linker figures
describe static placement, not measured stack or heap high-water marks.

The native stress graph used 16 LFOs, two destinations, 256 routed note onsets
and a blocked destination during its first 500 ms. It retained 134386 bytes of
stream-container storage. The test checks that retained storage plus 32 KiB
stays within the LFO stream allowance of 192 KiB plus 4096 bytes plus four
times the compiled stream-section size. It also checks activation progress and
coalescing. This graph does not establish worst-case allocator overhead or
physical MIDI throughput. Opaque extension allocations and transient standard
library workspace are outside the retained-storage counter.

## Physical qualification still required

No firmware was flashed and no receiver was exercised for this change.
Use an independently timestamped receiver to measure sample spacing, jitter,
initial/final latency and transaction boundaries during scrolling, transfers and
competing MIDI traffic. Verify each receiver's fine-CC, NRPN and RPN resolution,
Program Change behavior, disconnect recovery and audible smoothness before
claiming a practical simultaneous-LFO performance limit.
