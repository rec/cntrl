"""Reject invalid scenarios before they can mutate a controller session."""

import pytest
from pydantic import ValidationError

from cntrl.emulator.backend import Session
from cntrl.emulator.replay import Event, Scenario
from cntrl.patch_switcher.models import Target


class RecordedSession(Session):
    def __init__(self) -> None:
        self.target = Target.model_construct(buttons=['a', 'b'], midi_endpoints=[])
        self.time_us = 0
        self.random_seed = 0
        self.inputs: list[tuple[int, str, bool]] = []

    def seed(self, value: int) -> None:
        self.random_seed = value

    def advance(self, time_us: int) -> None:
        self.time_us = time_us

    def button(self, control_id: str, pressed: bool) -> None:
        self.inputs.append((self.time_us, control_id, pressed))


@pytest.mark.parametrize(
    'kind,target,value',
    [
        ('button', 'a', 2),
        ('block', 'din', -1),
        ('expression', 'a', 50),
        ('expression', '0', 1024),
    ],
)
def test_invalid_inputs_are_rejected_when_loading(
    kind: str, target: str, value: int
) -> None:
    with pytest.raises(ValidationError):
        Event.model_validate({'at': 0, 'kind': kind, 'target': target, 'value': value})


def test_events_cannot_go_backwards_before_the_end() -> None:
    with pytest.raises(ValidationError, match='chronological'):
        Scenario.model_validate(
            {
                'until': 1,
                'events': [
                    {'at': 0.75, 'kind': 'button', 'target': 'a', 'value': 1},
                    {'at': 0.25, 'kind': 'button', 'target': 'a', 'value': 0},
                ],
            }
        )


def test_events_cannot_extend_beyond_the_end() -> None:
    with pytest.raises(ValidationError, match='chronological'):
        Scenario.model_validate(
            {
                'until': 1,
                'events': [
                    {'at': 2, 'kind': 'button', 'target': 'a', 'value': 1},
                ],
            }
        )


def test_equal_timestamp_events_keep_their_authored_order() -> None:
    scenario = Scenario.model_validate(
        {
            'until': '1 ms',
            'events': [
                {'at': '0 ms', 'kind': 'button', 'target': 'a', 'value': 1},
                {'at': '0 ms', 'kind': 'button', 'target': 'b', 'value': 1},
            ],
        }
    )
    session = RecordedSession()
    scenario.run(session)
    assert session.inputs == [(0, 'a', True), (0, 'b', True)]
    assert session.time_us == 1000 and session.random_seed == scenario.seed
