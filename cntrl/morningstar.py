"""Documented MC6 Pro capabilities, not a hardware driver.

Sources:
https://www.morningstar.io/mc6-pro
https://help.morningstar.io/en/article/mc6-pro-user-manual-psevjy/
https://www.morningstar.io/post/mc6-pro-release
https://manuals.morningstar.io/mc-midi-controller/mc6-pro-user-manual

Active configurations are example selections, not asserted factory defaults or
settings read from a device. External pedals and switches are not built-in controls.
"""

from . import enums, models


def mc6_pro() -> models.Controller:
    """Create an independent description of the Morningstar MC6 Pro."""
    return models.Controller(
        name='Morningstar MC6 Pro',
        controls=[models.Button(id=i.lower(), name=i) for i in 'ABCDEF'],
        displays=[
            models.Display(
                id=f'lcd-{i}',
                name=f'LCD {i}',
                kind=enums.DisplayKind.GRAPHICAL,
                width=None,  # TODO: verify the documented pixel width.
                height=None,  # TODO: verify the documented pixel height.
                colors=None,  # TODO: verify the full-colour palette size.
            )
            for i in range(1, 4)
        ],
        ports=[
            *[
                models.Port(
                    id=f'din-midi-{i}',
                    name=f'DIN MIDI {i}',
                    physical=models.PhysicalSpecification(
                        connector=enums.ConnectorKind.DIN,
                        # The output accepts seven-pin phantom-power cables.
                        contacts=7 if i == 'out' else 5,
                    ),
                    configurations=[
                        models.PortConfiguration(
                            id='midi', direction=d, protocol=models.MidiProtocol()
                        )
                    ],
                    active_configuration='midi',
                )
                for i, d in [
                    ('in', enums.Direction.INPUT),
                    ('out', enums.Direction.OUTPUT),
                ]
            ],
            *[
                models.Port(
                    id=f'trs-midi-{i}',
                    name=f'3.5 mm TRS MIDI {i}',
                    physical=models.PhysicalSpecification(
                        connector=enums.ConnectorKind.TRS, contacts=3, diameter_mm=3.5
                    ),
                    configurations=[
                        models.PortConfiguration(
                            id='midi-type-a',
                            direction=d,
                            protocol=models.MidiProtocol(
                                trs_wiring=enums.MidiWiring.TYPE_A
                            ),
                        )
                    ],
                    active_configuration='midi-type-a',
                )
                for i, d in [
                    ('in', enums.Direction.INPUT),
                    ('out', enums.Direction.OUTPUT),
                ]
            ],
            models.Port(
                id='usb-device',
                name='USB device',
                physical=models.PhysicalSpecification(
                    connector=enums.ConnectorKind.USB_C
                ),
                configurations=[
                    models.PortConfiguration(
                        id=i,
                        direction=enums.Direction.BIDIRECTIONAL,
                        protocol=models.UsbProtocol(
                            role=enums.UsbRole.DEVICE, functions=f
                        ),
                    )
                    for i, f in [
                        ('midi', [enums.UsbFunction.MIDI]),
                        (
                            'midi-keyboard',
                            [enums.UsbFunction.MIDI, enums.UsbFunction.HID],
                        ),
                    ]
                ],
                active_configuration='midi',
            ),
            models.Port(
                id='usb-host',
                name='USB host',
                physical=models.PhysicalSpecification(
                    connector=enums.ConnectorKind.USB_C
                ),
                configurations=[
                    models.PortConfiguration(
                        id='midi-keyboard',
                        direction=enums.Direction.BIDIRECTIONAL,
                        protocol=models.UsbProtocol(
                            role=enums.UsbRole.HOST,
                            functions=[enums.UsbFunction.MIDI, enums.UsbFunction.HID],
                        ),
                    )
                ],
                active_configuration='midi-keyboard',
            ),
            *[
                models.Port(
                    id=f'configurable-{i}',
                    name=f'Configurable port {i}',
                    physical=models.PhysicalSpecification(
                        connector=enums.ConnectorKind.TRS, contacts=3, diameter_mm=6.35
                    ),
                    configurations=[
                        models.PortConfiguration(
                            id='expression',
                            direction=enums.Direction.INPUT,
                            protocol=models.ExpressionProtocol(),
                        ),
                        models.PortConfiguration(
                            id='switch',
                            direction=enums.Direction.INPUT,
                            protocol=models.SwitchProtocol(),
                        ),
                        *[
                            models.PortConfiguration(
                                id=f'midi-{w.value}',
                                direction=enums.Direction.OUTPUT,
                                protocol=models.MidiProtocol(trs_wiring=w),
                            )
                            for w in [
                                enums.MidiWiring.TYPE_A,
                                enums.MidiWiring.TYPE_B,
                                enums.MidiWiring.TIP_ACTIVE,
                                enums.MidiWiring.RING_ACTIVE,
                            ]
                        ],
                    ],
                    active_configuration='expression',
                )
                for i in range(1, 5)
            ],
            *[
                models.Port(
                    id=f'relay-{i.lower()}',
                    name=f'Relay port {i}',
                    physical=models.PhysicalSpecification(
                        connector=enums.ConnectorKind.TRS, contacts=3, diameter_mm=3.5
                    ),
                    configurations=[
                        models.PortConfiguration(
                            id='relay',
                            direction=enums.Direction.OUTPUT,
                            protocol=models.RelayProtocol(),
                        )
                    ],
                    active_configuration='relay',
                )
                for i in 'AB'
            ],
        ],
    )
