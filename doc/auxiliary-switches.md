# Auxiliary switches

Auxiliary contacts use the existing switch assignments and patch bindings.
No page or navigation controls are added. Enable the socket's `aux` role:

```toml
[trs_ports]
configurable-1 = 'aux'

[switches.sustain]
button = 'configurable-1.tip'
mode = 'momentary'
on = {controller = 64, value = 127, channel = 1, output = 'din'}
off = {controller = 64, value = 0, channel = 1, output = 'din'}
```

Use the actual configurable socket ID declared by the target. Identifiers are
`socket.tip`, `socket.ring`, and, in combined mode, `socket.both`. They remain
stable when other sockets are disabled or assigned another role. An assignment
requires an auxiliary-capable socket in `aux` mode. Existing per-context limits
on the number of switch assignments still apply.

The default arrangement is two independent, normally open contacts from tip to
sleeve and ring to sleeve. Closing both operates both assignments. Inputs use
the existing switch debounce and `fast_switches` policy. The assignment's
gesture debounce applies when provided, otherwise 20 ms is used.

For three switches, with the third closing both contacts together:

```toml
[auxiliary_switches]
configurable-1 = 'combined'

[patches.song]
bindings = {'configurable-1.tip' = 'verse', 'configurable-1.ring' = 'chorus', 'configurable-1.both' = 'solo', F = {operation = 'back'}}
```

Combined mode settles the complete contact pair before deciding which switch
changed. It waits at least 20 ms, or the largest configured gesture debounce
on that socket. This delay applies even with `fast_switches = true`, preventing
an intermediate tip or ring contact from generating an unintended action.

Assignments run in the same active switch context as built-in assignments:
the Switches module or an active patch binding map. Press, momentary, latched,
gestures and cleanup retain their existing semantics. Held contacts are
disarmed on entry until released. The existing display uses the assignment's
name; the full input identifier remains available as `switch.button`.

Unplugging a passive switch appears as open contacts. It releases momentary
actions after debounce. A passive socket cannot distinguish an unplugged cable
from released switches, so latched actions remain latched until toggled or
the context exits. Removing the socket role releases recognized held inputs
immediately. Changing a socket away from `aux` clears its combined setting.

Target adapters supply contact readings; decoding and debounce are portable.
Only passive switches are supported by this input mode. Powered devices need
the target's documented electrical compatibility. Automated tests cover bounce,
combined transitions, simultaneous independent inputs, held-entry rearming and
role-removal cleanup. Physical wiring, cable behavior and unplugging still need
bench qualification for each target.
