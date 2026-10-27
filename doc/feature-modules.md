# Optional feature modules

Status: initial MIDI cable-test module and static module menu implemented. The
broader module organization below remains guidance for subsequent features.

## Purpose

Organize features such as a MIDI monitor, MIDI cable tester and MIDI processor
as small optional modules around a shared core. Each module should work without
the others. Modules do not call one another directly.

## Ownership and dependencies

Each module owns its configuration, private state, pages, commands, button
actions, processing logic and tests.

The core owns physical MIDI ports, input decoding, output scheduling, buttons,
displays, storage and time. Modules access these through small interfaces rather
than accessing hardware directly. Hardware-specific implementations stay in the
existing maker directories.

Dependencies point from modules to shared core interfaces. The core coordinates
modules through an explicit registry; it does not depend on individual modules'
implementation details. Share functionality only when real modules need it.

## MIDI roles

| Role | Example | Behavior |
| --- | --- | --- |
| Observer | MIDI monitor | Receives events without consuming or changing them |
| Processor | Filter, remapper, arpeggiator | Receives events on an explicitly configured route and produces output |
| Active tool | MIDI cable tester | Temporarily claims selected ports and runs a test |

These roles describe participation, rather than requiring every module to
implement the same set of methods.

Enabling a monitor must not change routing. A processor must not automatically
receive everything another processor emits. A cable tester must not inject test
messages into live performance routes.

## Routing and resource ownership

The core determines which events reach each module, where generated messages
go, and whether ports are available. Processing chains are explicit routes,
not implicit consequences of module registration order.

A monitor can observe configured points before or after processing. Observation
does not consume an event or prevent its normal delivery.

An active tool explicitly claims the ports needed for its operation and releases
them when finished or deactivated. The core coordinates access so test traffic
does not interfere with performance traffic. Conflicting use must be resolved
before the operation starts.

## Installed and enabled

Keep build inclusion separate from setup activation:

- **Installed:** the firmware build includes the module. Excluded modules
  contribute no code or assets.
- **Enabled:** the setup selects an installed module. Disabled modules perform
  no work.

Start with static linking and an explicit module registry. Runtime plugin
loading, automatic discovery and a general dependency framework are unnecessary.

## Lifecycle and execution

A module exposes only the operations it needs: initialization, activation and
deactivation, event handling, and advancement of scheduled work where applicable.
Private state belongs to the module; shared hardware state belongs to the core.

Execution is cooperative and bounded. A busy monitor or processor must not stall
MIDI delivery or the UI. Scheduled processing uses core-provided time and output
scheduling rather than blocking waits.

## Establishing the interfaces

The MIDI cable tester is the first module. Its target adapter grants port access
and delivers physical input, while the module owns test behavior and its page.
Build the MIDI monitor against the shared core services next. Use those two
modules to establish which interfaces are actually shared before formalizing
more of the framework.

Verify each module's logic independently. Also verify that observers leave
routing unchanged, processors follow configured routes, active tools respect
port ownership, and deactivation releases resources. Hardware behavior requires
separate physical verification.

## Additional work beyond the prompt

None. The first implementation is documented in [midi-cable-test.md](midi-cable-test.md).
