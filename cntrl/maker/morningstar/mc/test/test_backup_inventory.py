from pathlib import Path

import tomlkit
from pytest_regressions.file_regression import FileRegressionFixture

from cntrl.maker.morningstar.mc.backup_inventory import inspect_backup, inventory


def test_counts_ignore_empty_slots_and_keep_message_contexts(
    file_regression: FileRegressionFixture,
) -> None:
    document: dict[str, object] = {
        'schemaVersion': 1,
        'deviceModel': 6,
        'dumpType': 'singleBank',
        'data': {
            'bank': {
                'bankNumber': 0,
                'bankName': 'Example',
                'bankMsgArray': [{'t': 1, 'm': 0, 'a': 1, 'data': [0, 127]}],
                'presetArray': [
                    {
                        'presetNum': 0,
                        'isExp': False,
                        'toToggle': True,
                        'shortName': 'Counter %F3',
                        'msgArray': [
                            {'t': 0, 'm': 0, 'a': 1, 'data': [99, 127]},
                            {
                                't': 1,
                                'm': 1,
                                'a': 1,
                                'd1': 7,
                                'd2': 0,
                                'd3': 0,
                                'd4': 0,
                            },
                            {
                                't': 1,
                                'm': 2,
                                'a': 2,
                                'd1': 8,
                                'd2': 0,
                                'd3': 0,
                                'd4': 0,
                            },
                        ],
                    }
                ],
                'expPresetArray': [
                    {
                        'presetNum': 0,
                        'isExp': True,
                        'msgArray': [{'t': 1, 'm': 0, 'a': 0, 'data': [9, 127]}],
                    }
                ],
            },
        },
    }
    file_regression.check(tomlkit.dumps(inspect_backup(document)), extension='.toml')


def test_collected_bytes_and_inventory_match_published_evidence() -> None:
    directory = Path(__file__).parents[1] / 'backup_examples'
    assert inventory(directory) == tomlkit.parse(
        (directory / 'inventory.toml').read_text()
    )
