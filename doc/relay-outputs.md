# Relay outputs

Qualified targets declare independent contact names in `relay_outputs`. Setups
can configure their logical resting states and invoke relay commands through
ordinary patches, assignments and songs. No pages are added or changed.

```toml
[relays]
'amp.tip' = false
'amp.ring' = true

[patches.clean-channel]
operation = 'relay'
relay = 'amp.tip'
action = 'disengage'

[patches.drive-channel]
operation = 'relay'
relay = 'amp.tip'
action = 'engage'

[patches.channel-toggle]
operation = 'relay'
relay = 'amp.tip'
action = 'toggle'

[patches.analog-tap]
operation = 'relay'
relay = 'amp.tip'
action = 'pulse'
duration = '100 ms'
```

`true` means logically engaged; `false` means disengaged. The qualified target
adapter maps engagement to actual GPIO polarity. An omitted resting state
defaults to `false`. Unknown contacts and unsupported targets fail compilation;
firmware also validates every declared contact against its hardware driver.

`engage` and `disengage` set the contact until another command or setup exit.
`toggle` inverts its current commanded state. `pulse` temporarily sets the
opposite of its configured resting state, then restores that resting state.
Duration is required only for pulses: whole milliseconds from 1 through 65535,
expressed in seconds or with units such as `100 ms`.

Pulses complete in the application service loop, independently of MIDI output
queues. The operation returns immediately after starting the pulse. A later
command on the same contact replaces its pending deadline. Repeating a pulse
extends it from the new start time; it does not create an extra contact edge.
Independent contacts keep independent timers. Timing follows the service-loop
resolution and does not block switch scans or MIDI delivery.

Cancelling a calling patch does not undo relay commands already issued, just as
it does not undo delivered MIDI messages. An issued pulse still restores itself
on time. To cancel a pulse early, issue another command for that contact.
Momentary switch assignments can use an explicit `off` relay command for this.

On setup exit, old contacts return to their old resting states and their timers
are removed. Setup activation then applies the new resting states before patch
execution. Startup applies the selected setup's resting states; commanded
states and pulse deadlines are not persisted across restarts. A failed driver
write reports an error; the firmware does not claim electrical state feedback
or silently retry a failed restoration.

Target adapters implement `RelayDriver::supports_relay()` and `relay_write()`.
The portable `RelayOutputs` engine owns state, timing and setup lifecycle, and
the existing patch executor dispatches commands. A target must establish contact
polarity, isolation, voltage/current limits and power-up behavior before
declaring a contact. Software cannot qualify these electrical properties.

Automated tests use a fake qualified driver to verify all operations, inverted
resting states, setup reset, timer wraparound, command replacement and deadlines
under MIDI backpressure. Targets without qualified relay circuitry remain
disabled and reject relay configurations.
