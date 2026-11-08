# Hosted MIDI forwarding

Set this top-level setup option in TOML:

```toml
forward_device_to_host = true
```

It defaults to true. There is no pedal-page toggle. Both `usb_device_midi` and
`usb_host_midi` must also be enabled. `is_midi_interface` continues to control the
physical MIDI interface independently.

When a supported hosted MIDI device connects, the pedal advertises an additional
bidirectional USB MIDI port named from the device's product descriptor. A device
without a product string is named `USB MIDI`. The computer's operating system or
application may include the pedal's product name in its displayed port name.

The pedal briefly detaches and reconnects its computer-facing USB connection to
publish descriptor changes. All functions on that connection, including serial
and keyboard, temporarily disconnect. Changes wait for an active configuration
session or setup/history transfer to finish. Messages for an absent device are
not saved for later playback, and pending traffic is cancelled on replacement.

This uses the current single hosted MIDI device and its first MIDI cable. Hub,
multiple-device and additional hosted-cable support are separate capabilities.
MIDI messages, including SysEx and real-time messages, are relayed through the
existing bounded queues and central MIDI state tracker.

Setting the option to false removes the advertised hosted port and disables
forwarding. The option survives compile/download and is included in history
comparison. Its disabled bit shares the existing input-policy byte with fast
switches; previous documents with only the fast-switch bit set retain the new
default of enabled forwarding. Document version and section sizes are unchanged.
