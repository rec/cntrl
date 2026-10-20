from cntrl import enums, models, morningstar


def test_mc6_pro_round_trip_preserves_documented_hardware() -> None:
    controller = morningstar.mc6_pro()
    restored = models.Controller.model_validate_json(controller.model_dump_json())
    assert restored == controller
    assert [c.name for c in restored.controls] == list('ABCDEF')
    assert all(isinstance(c, models.Button) for c in restored.controls)
    assert len(restored.displays) == 3
    assert all(d.kind == enums.DisplayKind.GRAPHICAL for d in restored.displays)
    assert all(
        d.width is None and d.height is None and d.colors is None
        for d in restored.displays
    )
    assert len(restored.ports) == 12
    assert len({p.id for p in restored.ports}) == 12


def test_configurable_ports_offer_input_and_all_midi_wiring_modes() -> None:
    ports = [p for p in morningstar.mc6_pro().ports if p.id.startswith('configurable-')]
    assert len(ports) == 4
    for port in ports:
        assert port.configurable
        assert port.physical.connector == enums.ConnectorKind.TRS
        assert port.physical.diameter_mm == 6.35
        assert port.active_configuration == 'expression'
        assert {c.protocol.kind for c in port.configurations} == {
            enums.ProtocolKind.MIDI,
            enums.ProtocolKind.EXPRESSION,
            enums.ProtocolKind.SWITCH,
        }
        midi = [
            c
            for c in port.configurations
            if isinstance(c.protocol, models.MidiProtocol)
        ]
        assert {
            c.protocol.trs_wiring
            for c in midi
            if isinstance(c.protocol, models.MidiProtocol)
        } == {
            enums.MidiWiring.TYPE_A,
            enums.MidiWiring.TYPE_B,
            enums.MidiWiring.TIP_ACTIVE,
            enums.MidiWiring.RING_ACTIVE,
        }
        assert all(c.direction == enums.Direction.OUTPUT for c in midi)
        assert all(
            c.direction == enums.Direction.INPUT
            for c in port.configurations
            if c not in midi
        )


def test_midi_and_relay_ports_have_distinct_protocols_and_directions() -> None:
    ports = {p.id: p for p in morningstar.mc6_pro().ports}
    for prefix in ['din', 'trs']:
        assert (
            ports[f'{prefix}-midi-in'].configurations[0].direction
            == enums.Direction.INPUT
        )
        assert (
            ports[f'{prefix}-midi-out'].configurations[0].direction
            == enums.Direction.OUTPUT
        )
    for name in ['trs-midi-in', 'trs-midi-out']:
        protocol = ports[name].configurations[0].protocol
        assert isinstance(protocol, models.MidiProtocol)
        assert protocol.trs_wiring == enums.MidiWiring.TYPE_A
    for name in ['relay-a', 'relay-b']:
        configuration = ports[name].configurations[0]
        assert isinstance(configuration.protocol, models.RelayProtocol)
        assert configuration.direction == enums.Direction.OUTPUT
        assert ports[name].physical.diameter_mm == 3.5
    assert ports['din-midi-in'].physical.contacts == 5
    assert ports['din-midi-out'].physical.contacts == 7


def test_usb_ports_have_separate_host_and_device_roles() -> None:
    ports = {p.id: p for p in morningstar.mc6_pro().ports}
    for name, role in [
        ('usb-device', enums.UsbRole.DEVICE),
        ('usb-host', enums.UsbRole.HOST),
    ]:
        port = ports[name]
        assert port.physical.connector == enums.ConnectorKind.USB_C
        for configuration in port.configurations:
            assert configuration.direction == enums.Direction.BIDIRECTIONAL
            assert isinstance(configuration.protocol, models.UsbProtocol)
            assert configuration.protocol.role == role
            assert enums.UsbFunction.MIDI in configuration.protocol.functions
    assert ports['usb-device'].configurable


def test_descriptions_do_not_share_mutable_collections() -> None:
    first = morningstar.mc6_pro()
    second = morningstar.mc6_pro()
    first.controls.clear()
    first.ports[6].configurations.clear()
    assert len(second.controls) == 6
    assert len(second.ports[6].configurations) == 6
