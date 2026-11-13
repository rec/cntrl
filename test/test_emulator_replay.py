"""Reject invalid scenarios before they can mutate a controller session."""

import pytest
from pydantic import ValidationError

from cntrl.emulator.replay import Event, Scenario


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


def test_events_cannot_go_backwards_or_extend_beyond_the_end() -> None:
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
    assert [e.target for e in scenario.events] == ['a', 'b']
