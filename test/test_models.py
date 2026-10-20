import pytest
from pydantic import ValidationError

from cntrl import enums, models


def test_controller_round_trip_preserves_control_types() -> None:
    controller = models.Controller(
        name='Example',
        controls=[
            models.Button(id='a', name='A'),
            models.AbsoluteControl(
                id='pedal',
                name='Pedal',
                value_range=models.ValueRange(minimum=0, maximum=127),
            ),
            models.RelativeControl(id='dial', name='Dial'),
        ],
    )
    restored = models.Controller.model_validate_json(controller.model_dump_json())
    assert restored == controller
    assert isinstance(restored.controls[0], models.Button)
    assert isinstance(restored.controls[1], models.AbsoluteControl)
    assert isinstance(restored.controls[2], models.RelativeControl)


@pytest.mark.parametrize('kind', list(enums.DisplayKind))
def test_display_describes_dimensions_and_colors(kind: enums.DisplayKind) -> None:
    display = models.Display(id='screen', name='Screen', kind=kind, width=16, colors=3)
    assert display.height is None
    assert display.colors == 3


@pytest.mark.parametrize('minimum,maximum', [(1, 1), (2, 1), (0, float('inf'))])
def test_ranges_reject_invalid_bounds(minimum: float, maximum: float) -> None:
    with pytest.raises(ValidationError):
        models.ValueRange(minimum=minimum, maximum=maximum)


@pytest.mark.parametrize('colors', [0, -1])
def test_display_rejects_unsupported_color_counts(colors: int) -> None:
    with pytest.raises(ValidationError):
        models.Display(
            id='screen',
            name='Screen',
            kind=enums.DisplayKind.NUMERIC,
            width=3,
            colors=colors,
        )


def test_display_preserves_unknown_details() -> None:
    display = models.Display(id='lcd', name='LCD', kind=enums.DisplayKind.GRAPHICAL)
    restored = models.Display.model_validate_json(display.model_dump_json())
    assert restored.width is None
    assert restored.height is None
    assert restored.colors is None


def test_display_accepts_more_than_three_colors() -> None:
    display = models.Display(
        id='lcd', name='LCD', kind=enums.DisplayKind.GRAPHICAL, colors=256
    )
    assert display.colors == 256


def test_configurable_port_keeps_one_physical_connector() -> None:
    port = models.Port(
        id='jack',
        name='Configurable port',
        physical=models.PhysicalSpecification(
            connector=enums.ConnectorKind.TRS, contacts=3, diameter_mm=6.35
        ),
        modes=[
            models.PortMode(
                direction=enums.Direction.OUTPUT,
                protocol=models.MidiProtocol(trs_wiring=enums.MidiWiring.TYPE_A),
            ),
            models.PortMode(
                direction=enums.Direction.INPUT,
                protocol=models.ExpressionProtocol(),
            ),
            models.PortMode(
                direction=enums.Direction.INPUT,
                protocol=models.SwitchProtocol(),
            ),
        ],
    )
    restored = models.Port.model_validate_json(port.model_dump_json())
    assert restored == port
    assert restored.configurable
    assert restored.physical.connector == enums.ConnectorKind.TRS
    assert isinstance(restored.modes[1].protocol, models.ExpressionProtocol)


@pytest.mark.parametrize(
    'protocol',
    [
        models.MidiProtocol(),
        models.EthernetProtocol(),
        models.CvProtocol(voltage_range=models.ValueRange(minimum=-5, maximum=5)),
        models.RelayProtocol(),
    ],
)
def test_protocols_round_trip_in_modes(
    protocol: models.MidiProtocol
    | models.EthernetProtocol
    | models.CvProtocol
    | models.RelayProtocol,
) -> None:
    mode = models.PortMode(direction=enums.Direction.BIDIRECTIONAL, protocol=protocol)
    restored = models.PortMode.model_validate_json(mode.model_dump_json())
    assert restored == mode
    assert type(restored.protocol) is type(protocol)


def test_single_mode_port_is_fixed() -> None:
    port = models.Port(
        id='midi-in',
        name='MIDI in',
        physical=models.PhysicalSpecification(
            connector=enums.ConnectorKind.DIN, contacts=5
        ),
        modes=[
            models.PortMode(
                direction=enums.Direction.INPUT,
                protocol=models.MidiProtocol(),
            )
        ],
    )
    assert not port.configurable


def test_port_requires_a_supported_mode() -> None:
    with pytest.raises(ValidationError):
        models.Port(
            id='port',
            name='models.Port',
            physical=models.PhysicalSpecification(connector=enums.ConnectorKind.TRS),
            modes=[],
        )


def test_absent_capabilities_need_no_placeholder_implementation() -> None:
    controller = models.Controller(name='Empty')
    assert not controller.controls
    assert not controller.displays
    assert not controller.ports


def test_usb_sockets_round_trip_as_strings() -> None:
    controller = models.Controller(name='USB', ports=list(enums.UsbSocket))
    assert controller.model_dump(mode='json')['ports'] == [
        s.value for s in enums.UsbSocket
    ]
    assert (
        models.Controller.model_validate_json(controller.model_dump_json())
        == controller
    )
    with pytest.raises(ValidationError):
        models.Controller.model_validate({'name': 'USB', 'ports': ['not-a-socket']})


def test_models_prevent_field_reassignment() -> None:
    button = models.Button(id='a', name='A')
    with pytest.raises(ValidationError, match='frozen'):
        button.name = 'B'  # ty: ignore[invalid-assignment]
